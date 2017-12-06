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

SquashFilter::SquashFilter(Envoy::Upstream::ClusterManager& cm, const std::string& squash_cluster_name) :
  cm_(cm),
  squash_cluster_name_(squash_cluster_name),
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
  const Envoy::Http::HeaderEntry* squasheader = headers.get(Envoy::Http::LowerCaseString("x-squash-debug"));

  if (squasheader == nullptr) {
    ENVOY_LOG(warn, "Squash: no squash header. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // get pod and container name
  const char* podc = std::getenv("POD_NAME");
  if (podc == nullptr) {
    ENVOY_LOG(warn, "Squash: no podc. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  std::string pod(podc);
  if (pod.empty()) {
    ENVOY_LOG(warn, "Squash: no pod string. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }


  const char* podnamespacec = std::getenv("POD_NAMESPACE");
  if (podnamespacec == nullptr) {
    ENVOY_LOG(warn, "Squash: no podnamespacec. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }
  std::string podnamespace(podnamespacec);
  if (podnamespace.empty()) {
    ENVOY_LOG(warn, "Squash: no container string. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG(info, "Squash:we need to squash something");

  // get squash service cluster object
  // async client to create debug config at squash server
  // when it is done, issue a request and check if it is attached.
  // retry until it is. or until we timeout
  // continue decoding.
  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertContentType().value(std::string("application/json"));
  request->headers().insertPath().value(std::string("/api/v2/debugattachment"));
  request->headers().insertHost().value(std::string("squash-server"));
  request->headers().insertMethod().value(std::string("POST"));
  std::string body = "{\"spec\":{\"attachment\":{\"pod\":\""  + pod + "\",\"namespace\":\""  + podnamespace + "\"}, \"match_request\":true}}";
  request->body().reset(new Envoy::Buffer::OwnedImpl(body));

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
    const char* status = m->headers().Status()->value().c_str();
    if (status != std::string("201")) {
      ENVOY_LOG(info, "Squash: can't create attachment object. status {} - not squashing",status);
  
      state_ = INITIAL;
      delay_timer_.reset();
      decoder_callbacks_->continueDecoding();
    } else {
      state_ = CHECK_ATTACHMENT;

      try {
        Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(jsonbody);
        debugConfigId_ = json_config->getObject("metadata",true)->getString("name","");
      } catch (Envoy::Json::Exception&) {
        debugConfigId_ = "";
      }

      if (debugConfigId_.empty()) {
        state_ = INITIAL;
        delay_timer_.reset();
        decoder_callbacks_->continueDecoding();
      } else {
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
  request->headers().insertPath().value("/api/v2/debugattachment/"+debugConfigId_);
  request->headers().insertHost().value(std::string("squash-server"));
  request->headers().insertMethod().value(std::string("GET"));

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

} // Squash
} // Solo
