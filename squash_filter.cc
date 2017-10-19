#include <string>
#include <algorithm>
#include <vector>
#include <list>

#include "squash_filter.h"

#include "server/config/network/http_connection_manager.h"

#include "envoy/http/header_map.h"

#include "common/common/hex.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"

namespace Solo {
namespace Squash {

SquashFilter::SquashFilter(Envoy::Upstream::ClusterManager& cm) :
  state_(SquashFilter::INITIAL),
  timeout_(std::chrono::milliseconds(1000)), 
  

SquashFilter::~SquashFilter() {}

void SquashFilter::onDestroy() {}

Envoy::Http::FilterHeadersStatus SquashFilter::decodeHeaders(Envoy::Http::HeaderMap& headers, bool end_stream) {

  // check for squash header
  squashheader = headers.get(Envoy::Http::LowerCaseString("x-squash-debug"));

  if (squashheader == nullptr) {
    return Envoy::Http::FilterHeadersStatus::Continue;    
  }
  
  // get pod and container name
  
  std::string pod(std::getenv("POD_NAME"))
  std::string container(std::getenv("CONTAINER_NAME"))
  std::string image(std::getenv("IMAGE_NAME"))
  
  if (pod.empty()) {
    return Envoy::Http::FilterHeadersStatus::Continue;    
 }
 if (container.empty()) {
  return Envoy::Http::FilterHeadersStatus::Continue;    
}


  // get squash service cluster object
  // async client to create debug config at squash server
  // when it is done, issue a request and check if it is attached.
  // retry until it is. or until we timeout
  // continue decoding.
  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertContentType().value("application/json");
  request->headers().insertPath().value("/debugconfigs");
  request->headers().insertMethod().value("POST");
  std::string body = "{ \"attachment\":{\"type\":\"container\",\"name\":"  + pod + "/" + container   "}, \"immediatly\" : true, \"debugger\":\"dlv\", \"image\" : \""+image+"\"}"
  request->body().reset(new Envoy::Buffer::OwnedImpl(body));

  state_ = CREATE_CONFIG
  cm_.httpAsyncClientForCluster("squash").send(std::move(request), *this, timeout_);

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

void SquashFilter::onSuccess(Envoy::Http::MessagePtr&& m) override {
 // if state === create config; state = creaed; if state == created ; state = null
 switch state_ {

  case INITIAL: {
    // Should never happen..
    break;
  }
  case CREATE_CONFIG: {
    state_ = CHECK_ATTACHMENT;
    Buffer::InstancePtr& data = m.body();
    uint64_t num_slices = data.getRawSlices(nullptr, 0);
    Envoy::Buffer::RawSlice slices[num_slices];
    data.getRawSlices(slices, num_slices);
    std::string jsonbody;
    for (Envoy::Buffer::RawSlice& slice : slices) {
      jsonbody += std::string(slice.mem_, slice.len_);
    }
    Envoy::Json::ObjectPtr json_config = Envoy::Json::Factory::loadFromString(jsonbody);
    debugConfigId_ = json_config.getString("id");
    retry_count_ = 0;
    pollForAttachment();
    break;
  }
  case CHECK_ATTACHMENT: {
    bool attached = true;
    if (attached || (rety_count > 10)) {
      state_ = INITIAL;
      decoder_callbacks_->continueDecoding();
    } else {
      delay_timer_ = callbacks_->dispatcher().createTimer([this]() -> void { pollForAttachment(); });
      delay_timer_->enableTimer(std::chrono::milliseconds(1000)));
    }
    break;
  }
 }
}

void SquashFilter::onFailure(Envoy::Http::AsyncClient::FailureReason) override {
  // increase retry count and try again.
  if ((state_ != CHECK_ATTACHMENT) || (rety_count > 10)) {
    state_ = INITIAL;
    decoder_callbacks_->continueDecoding();
  } else {
    pollForAttachment();
  }
}

void SquashFilter::pollForAttachment() {
  retry_count_++;  
  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertPath().value("/debugconfigs/"+debugConfigId_);
  cm_.httpAsyncClientForCluster("squash").send(std::move(request), *this, timeout_);
}


Envoy::Http::FilterDataStatus SquashFilter::decodeData(Envoy::Buffer::Instance& data, bool end_stream) {
    return Envoy::Http::FilterDataStatus::Continue;    
}

Envoy::Http::FilterTrailersStatus SquashFilter::decodeTrailers(Envoy::Http::HeaderMap&) {
  return Envoy::Http::FilterTrailersStatus::Continue;
}

void SquashFilter::setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

} // Squash
} // Solo
