#include <algorithm>
#include <list>
#include <string>
#include <vector>

#include "squash_filter.h"

#include "server/config/network/http_connection_manager.h"

#include "common/http/message_impl.h"
#include "envoy/http/header_map.h"

#include "common/common/empty_string.h"
#include "common/common/hex.h"
#include "common/common/utility.h"

namespace Solo {
namespace Squash {

SquashFilter::SquashFilter(SquashFilterConfigSharedPtr config,
                           Envoy::Upstream::ClusterManager &cm)
    : config_(config), cm_(cm), decoder_callbacks_(nullptr),
      state_(SquashFilter::INITIAL), debugConfigPath_(), delay_timer_(nullptr), 
      attachment_timeout_timer_(nullptr), in_flight_request_(nullptr) {}

SquashFilter::~SquashFilter() {}

void SquashFilter::onDestroy() {
  if (in_flight_request_ != nullptr) {
    in_flight_request_->cancel();
    in_flight_request_ = nullptr;
  }

  if (attachment_timeout_timer_) {
    attachment_timeout_timer_->disableTimer();
    attachment_timeout_timer_.reset();
  }

  if (delay_timer_.get() != nullptr) {
    delay_timer_.reset();
  }
}

Envoy::Http::FilterHeadersStatus
SquashFilter::decodeHeaders(Envoy::Http::HeaderMap &headers, bool) {

  // check for squash header
  if (!headers.get(squashHeaderKey())) {
    ENVOY_LOG(warn, "Squash: no squash header. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG(info, "Squash:we need to squash something");

  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertContentType().value().setReference(
      Envoy::Http::Headers::get().ContentTypeValues.Json);
  request->headers().insertPath().value().setReference(postAttachmentPath());
  request->headers().insertHost().value().setReference(severAuthority());
  request->headers().insertMethod().value().setReference(
      Envoy::Http::Headers::get().MethodValues.Post);
  request->body().reset(
      new Envoy::Buffer::OwnedImpl(config_->attachment_json()));

  state_ = CREATE_CONFIG;
  in_flight_request_ =
      cm_.httpAsyncClientForCluster(config_->squash_cluster_name())
          .send(std::move(request), *this, config_->squash_request_timeout());

  if (in_flight_request_ == nullptr) {
    state_ = INITIAL;
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  attachment_timeout_timer_ = decoder_callbacks_->dispatcher().createTimer(
      [this]() -> void { doneSquashing(); });
  attachment_timeout_timer_->enableTimer(config_->attachment_timeout());
  // check if the timer expired inline.
  if (state_ == INITIAL) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

void SquashFilter::onSuccess(Envoy::Http::MessagePtr &&m) {
  in_flight_request_ = nullptr;
  Envoy::Buffer::InstancePtr &data = m->body();
  uint64_t num_slices = data->getRawSlices(nullptr, 0);
  Envoy::Buffer::RawSlice slices[num_slices];
  data->getRawSlices(slices, num_slices);
  std::string jsonbody;
  for (Envoy::Buffer::RawSlice &slice : slices) {
    jsonbody += std::string(static_cast<const char *>(slice.mem_), slice.len_);
  }
  
  switch (state_) {

  case INITIAL: {
    // Should never happen..
    break;
  }
  case CREATE_CONFIG: {
    // get the config object that was created
    if (m->headers().Status()->value() != "201") {
      ENVOY_LOG(
          info,
          "Squash: can't create attachment object. status {} - not squashing",
          m->headers().Status()->value().c_str());
      doneSquashing();
    } else {
      state_ = CHECK_ATTACHMENT;

      std::string debugConfigId;
      try {
        Envoy::Json::ObjectSharedPtr json_config =
            Envoy::Json::Factory::loadFromString(jsonbody);
        debugConfigId =
            json_config->getObject("metadata", true)->getString("name", "");
      } catch (Envoy::Json::Exception &) {
        debugConfigId = "";
      }

      if (debugConfigId.empty()) {
        doneSquashing();
      } else {
        debugConfigPath_ = "/api/v2/debugattachment/" + debugConfigId;
        pollForAttachment();
      }
    }

    break;
  }
  case CHECK_ATTACHMENT: {

    std::string attachmentstate;
    try {
      Envoy::Json::ObjectSharedPtr json_config =
          Envoy::Json::Factory::loadFromString(jsonbody);
      attachmentstate =
          json_config->getObject("status", true)->getString("state", "");
    } catch (Envoy::Json::Exception &) {
      // no state yet.. leave it empty for the retry logic.
    }

    bool attached = attachmentstate == "attached";
    bool error = attachmentstate == "error";
    bool finalstate = attached || error;

    if (finalstate) {
      doneSquashing();
    } else {
      retry();
    }
    break;
  }
  }
}

void SquashFilter::onFailure(Envoy::Http::AsyncClient::FailureReason) {
  bool cleanupneeded = in_flight_request_ != nullptr;
  in_flight_request_ = nullptr;
  switch (state_) {
    case INITIAL: {
      break;
    }
    case CREATE_CONFIG: {
      // no retries here, as we couldnt create the attachment object.
      if (cleanupneeded) {
        // cleanup not needed if onFailure called inline in async client send.
        // this means that decodeHeaders is down the stack and will return Continue.
        doneSquashing();
      }
      break;
    }
    case CHECK_ATTACHMENT: {
      retry();
      break;
    }
  }
}

void SquashFilter::retry() {

  if (delay_timer_.get() == nullptr) {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this]() -> void { pollForAttachment(); });
  }
  delay_timer_->enableTimer(config_->attachment_poll_every());

}

void SquashFilter::pollForAttachment() {
  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertMethod().value().setReference(
      Envoy::Http::Headers::get().MethodValues.Get);
  request->headers().insertPath().value().setReference(debugConfigPath_);
  request->headers().insertHost().value().setReference(severAuthority());

  in_flight_request_ =
      cm_.httpAsyncClientForCluster(config_->squash_cluster_name())
          .send(std::move(request), *this, config_->squash_request_timeout());
  // no need to check in_flight_request_ is null as onFailure will take care of
  // that.
}

Envoy::Http::FilterDataStatus
SquashFilter::decodeData(Envoy::Buffer::Instance &, bool) {
  if (state_ == INITIAL) {
    return Envoy::Http::FilterDataStatus::Continue;
  } else {
    return Envoy::Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Envoy::Http::FilterTrailersStatus
SquashFilter::decodeTrailers(Envoy::Http::HeaderMap &) {
  if (state_ == INITIAL) {
    return Envoy::Http::FilterTrailersStatus::Continue;
  } else {
    return Envoy::Http::FilterTrailersStatus::StopIteration;
  }
}

void SquashFilter::setDecoderFilterCallbacks(
    Envoy::Http::StreamDecoderFilterCallbacks &callbacks) {
  decoder_callbacks_ = &callbacks;
}

const Envoy::Http::LowerCaseString &SquashFilter::squashHeaderKey() {
  static Envoy::Http::LowerCaseString *key =
      new Envoy::Http::LowerCaseString("x-squash-debug");
  return *key;
}

const std::string &SquashFilter::postAttachmentPath() {
  static std::string *val = new std::string("/api/v2/debugattachment");
  return *val;
}

const std::string &SquashFilter::severAuthority() {
  static std::string *val = new std::string("squash-server");
  return *val;
}

void SquashFilter::doneSquashing() {
  state_ = INITIAL;
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }

  if (attachment_timeout_timer_) {
    attachment_timeout_timer_->disableTimer();
    attachment_timeout_timer_.reset();
  }

  if (in_flight_request_ != nullptr) {
    in_flight_request_->cancel();
    in_flight_request_ = nullptr;
  }

  decoder_callbacks_->continueDecoding();
}

} // namespace Squash
} // namespace Solo
