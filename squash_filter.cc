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
      in_flight_request_(nullptr), request_deadline_() {}

SquashFilter::~SquashFilter() {}

void SquashFilter::onDestroy() {
  if (in_flight_request_ != nullptr) {
    in_flight_request_->cancel();
    in_flight_request_ = nullptr;
  }

  if (delay_timer_.get() != nullptr) {
    delay_timer_.reset();
  }
}

Envoy::Http::FilterHeadersStatus
SquashFilter::decodeHeaders(Envoy::Http::HeaderMap &headers, bool) {

  if (config_->squash_cluster_name().empty()) {
    ENVOY_LOG(warn, "Squash: cluster not configured. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // check for squash header
  const Envoy::Http::HeaderEntry *squasheader = headers.get(squashHeaderKey());

  if (squasheader == nullptr) {
    ENVOY_LOG(warn, "Squash: no squash header. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG(info, "Squash:we need to squash something");

  // get squash service cluster object
  // async client to create debug config at squash server
  // when it is done, issue a request and check if it is attached.
  // retry until it is. or until we timeout
  // continue decoding.
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
  // if state === create config; state = creaed; if state == created ; state =
  // null
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
      request_deadline_ =
          std::chrono::steady_clock::now() + config_->attachment_timeout();

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
      maybeRetry();
    }
    break;
  }
  }
}

void SquashFilter::onFailure(Envoy::Http::AsyncClient::FailureReason) {
  bool cleanupneeded = in_flight_request_ != nullptr;
  in_flight_request_ = nullptr;
  if (state_ == CREATE_CONFIG) {
    // no retries here, as we couldnt create the attachment object.
    if (cleanupneeded) {
      // cleanup not needed of onFailure called inline in async client send.
      doneSquashing();
    }
    return;
  }
  maybeRetry();
}

void SquashFilter::maybeRetry() {

  if ((state_ != CHECK_ATTACHMENT) ||
      (request_deadline_ <= std::chrono::steady_clock::now())) {
    doneSquashing();
  } else {
    if (delay_timer_.get() == nullptr) {
      delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
          [this]() -> void { pollForAttachment(); });
    }
    delay_timer_->enableTimer(config_->attachment_poll_every());
  }
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

  if (in_flight_request_ != nullptr) {
    in_flight_request_->cancel();
    in_flight_request_ = nullptr;
  }

  decoder_callbacks_->continueDecoding();
}

} // namespace Squash
} // namespace Solo
