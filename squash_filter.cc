#include <string>
#include <algorithm>
#include <vector>
#include <list>

#include "squash_filter.h"

#include "server/config/network/http_connection_manager.h"

#include "common/http/message_impl.h"
#include "envoy/http/header_map.h"

#include "common/common/hex.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"

namespace Solo {
namespace Squash {

const int MAX_RETRY = 60;

SquashFilter::SquashFilter(Envoy::Upstream::ClusterManager& cm, const std::string& squash_cluster_name, const std::string& attachment_json) :
  cm_(cm),
  squash_cluster_name_(squash_cluster_name),
  attachment_json_(attachment_json),
  state_(SquashFilter::INITIAL),
  timeout_(std::chrono::milliseconds(1000)),
  retry_count_(0),
  delay_timer_(nullptr),
  in_flight_request_(nullptr) {}

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

Envoy::Http::FilterHeadersStatus SquashFilter::decodeHeaders(Envoy::Http::HeaderMap& headers, bool ) {

  if (squash_cluster_name_.empty()) {
    ENVOY_LOG(warn, "Squash: cluster not configured. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // check for squash header
  const Envoy::Http::HeaderEntry* squasheader = headers.get(squashHeaderKey());

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
  request->headers().insertContentType().value().setReference(Envoy::Http::Headers::get().ContentTypeValues.Json);
  request->headers().insertPath().value().setReference(postAttachmentPath());
  request->headers().insertHost().value().setReference(severAuthority());
  request->headers().insertMethod().value().setReference(Envoy::Http::Headers::get().MethodValues.Post);
  request->body().reset(new Envoy::Buffer::OwnedImpl(attachment_json_));

  state_ = CREATE_CONFIG;
  in_flight_request_ = cm_.httpAsyncClientForCluster(squash_cluster_name_).send(std::move(request), *this, timeout_);

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

void SquashFilter::onSuccess(Envoy::Http::MessagePtr&& m) {
  in_flight_request_ = nullptr;
  Envoy::Buffer::InstancePtr& data = m->body();
  uint64_t num_slices = data->getRawSlices(nullptr, 0);
  Envoy::Buffer::RawSlice slices[num_slices];
  data->getRawSlices(slices, num_slices);
  std::string jsonbody;
  for (Envoy::Buffer::RawSlice& slice : slices) {
    jsonbody += std::string(static_cast<const char*>(slice.mem_), slice.len_);
  }
 // if state === create config; state = creaed; if state == created ; state = null
 switch (state_) {

  case INITIAL: {
    // Should never happen..
    break;
  }
  case CREATE_CONFIG: {
    // get the config object that was created
    if (m->headers().Status()->value() != "201") {
      ENVOY_LOG(info, "Squash: can't create attachment object. status {} - not squashing", m->headers().Status()->value().c_str());
  
      state_ = INITIAL;
      delay_timer_.reset();
      decoder_callbacks_->continueDecoding();
    } else {
      state_ = CHECK_ATTACHMENT;

      std::string debugConfigId;
      try {
        Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(jsonbody);
        debugConfigId = json_config->getObject("metadata",true)->getString("name","");
      } catch (Envoy::Json::Exception&) {
        debugConfigId = "";
      }

      if (debugConfigId.empty()) {
        state_ = INITIAL;
        delay_timer_.reset();
        decoder_callbacks_->continueDecoding();
      } else {
        debugConfigPath_ = "/api/v2/debugattachment/" + debugConfigId;
        retry_count_ = 0;
        pollForAttachment();
      }
    }

    break;
  }
  case CHECK_ATTACHMENT: {

    std::string state;
    try {
      Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(jsonbody);
      state = json_config->getObject("status", true)->getString("state", "");
    } catch (Envoy::Json::Exception&) {
      // no state yet.. leave it empty
    }

    bool attached = state == "attached";
    bool error = state == "error";
    bool finalstate = attached || error;

    if (finalstate || (retry_count_ > MAX_RETRY)) {
      state_ = INITIAL;
      delay_timer_.reset();
      decoder_callbacks_->continueDecoding();
    } else {
      if (delay_timer_.get() == nullptr) {
        delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void { pollForAttachment(); });
      }
      delay_timer_->enableTimer(std::chrono::milliseconds(1000));
    }
    break;
  }
 }
}

void SquashFilter::onFailure(Envoy::Http::AsyncClient::FailureReason) {
  in_flight_request_ = nullptr;
  // increase retry count and try again.
  if ((state_ != CHECK_ATTACHMENT) || (retry_count_ > MAX_RETRY)) {
    state_ = INITIAL;
    delay_timer_.reset();
    decoder_callbacks_->continueDecoding();
  } else {
    pollForAttachment();
  }
}

void SquashFilter::pollForAttachment() {
  retry_count_++;
  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertMethod().value().setReference(Envoy::Http::Headers::get().MethodValues.Get);
  request->headers().insertPath().value().setReference(debugConfigPath_);
  request->headers().insertHost().value().setReference(severAuthority());
  
  in_flight_request_ = cm_.httpAsyncClientForCluster(squash_cluster_name_).send(std::move(request), *this, timeout_);
}

Envoy::Http::FilterDataStatus SquashFilter::decodeData(Envoy::Buffer::Instance& , bool ) {
  if (state_ == INITIAL) {
    return Envoy::Http::FilterDataStatus::Continue;
  } else {
    return Envoy::Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Envoy::Http::FilterTrailersStatus SquashFilter::decodeTrailers(Envoy::Http::HeaderMap&) {
  if (state_ == INITIAL) {
    return Envoy::Http::FilterTrailersStatus::Continue;
  } else {
    return Envoy::Http::FilterTrailersStatus::StopIteration;
  }
}

void SquashFilter::setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

const Envoy::Http::LowerCaseString& SquashFilter::squashHeaderKey() {
  static Envoy::Http::LowerCaseString* key = new Envoy::Http::LowerCaseString("x-squash-debug");
  return *key;
}

const std::string& SquashFilter::postAttachmentPath() {
  static std::string* val = new std::string("/api/v2/debugattachment");
  return *val;
}

const std::string& SquashFilter::severAuthority() {
  static std::string* val = new std::string("squash-server");
  return *val;
}

} // Squash
} // Solo
