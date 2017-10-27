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

const char* squash_cluster_name = "out.bcd9053e66896f879e365719292f3da2be930f77";

SquashFilter::SquashFilter(Envoy::Upstream::ClusterManager& cm) :
  cm_(cm),
  state_(SquashFilter::INITIAL),
  timeout_(std::chrono::milliseconds(1000)),
  retry_count_(0),
  delay_timer_(nullptr) {}

SquashFilter::~SquashFilter() {}

void SquashFilter::onDestroy() {}

Envoy::Http::FilterHeadersStatus SquashFilter::decodeHeaders(Envoy::Http::HeaderMap& headers, bool ) {

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
  
  const char* containerc = std::getenv("CONTAINER_NAME");
  if (containerc == nullptr) {
    ENVOY_LOG(warn, "Squash: no containerc. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;    
  }
  
  std::string container(containerc);
  const char* imagec = std::getenv("IMAGE_NAME");
  if (imagec == nullptr) {
    ENVOY_LOG(warn, "Squash: no imagec. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;    
  }
  std::string image(imagec);
  
  if (pod.empty()) {
    ENVOY_LOG(warn, "Squash: no pod string. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;    
  }
  
  if (container.empty()) {
    ENVOY_LOG(warn, "Squash: no container string. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;    
  }
  
  if (image.empty()) {
    ENVOY_LOG(warn, "Squash: no image string. ignoring.");
    return Envoy::Http::FilterHeadersStatus::Continue;    
  }

  if (squasheader->value() != image.c_str()) {

    ENVOY_LOG(warn, "Squash: image; {} != {}. ignoring.", squasheader->value().c_str(), image);
    
    return Envoy::Http::FilterHeadersStatus::Continue;        
  }
  
  ENVOY_LOG(warn, "Squash:we need to squash something");

  // get squash service cluster object
  // async client to create debug config at squash server
  // when it is done, issue a request and check if it is attached.
  // retry until it is. or until we timeout
  // continue decoding.
  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertContentType().value(std::string("application/json"));
  request->headers().insertPath().value(std::string("/api/v1/debugconfig"));
  request->headers().insertHost().value(std::string("squash"));
  request->headers().insertMethod().value(std::string("POST"));
  std::string body = "{ \"attachment\":{\"type\":\"container\",\"name\":\""  + pod + "/" + container  + 
  "\"}, \"immediatly\" : true, \"debugger\":\"dlv\", \"image\" : \""+image+"\"}";
  request->body().reset(new Envoy::Buffer::OwnedImpl(body));

  state_ = CREATE_CONFIG;
  cm_.httpAsyncClientForCluster(squash_cluster_name).send(std::move(request), *this, timeout_);

  return Envoy::Http::FilterHeadersStatus::StopIteration;
}

void SquashFilter::onSuccess(Envoy::Http::MessagePtr&& m) {
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
    state_ = CHECK_ATTACHMENT;
    Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(jsonbody);
    debugConfigId_ = json_config->getString("id");
    retry_count_ = 0;
    pollForAttachment();
    break;
  }
  case CHECK_ATTACHMENT: {
    Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(jsonbody);
    bool attached = json_config->getBoolean("attached", false);
    if (attached || (retry_count_ > 10)) {
      state_ = INITIAL;
      decoder_callbacks_->continueDecoding();
    } else {
      delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void { pollForAttachment(); });
      delay_timer_->enableTimer(std::chrono::milliseconds(1000));
    }
    break;
  }
 }
}

void SquashFilter::onFailure(Envoy::Http::AsyncClient::FailureReason) {
  // increase retry count and try again.
  if ((state_ != CHECK_ATTACHMENT) || (retry_count_ > 10)) {
    state_ = INITIAL;
    decoder_callbacks_->continueDecoding();
  } else {
    pollForAttachment();
  }
}

void SquashFilter::pollForAttachment() {
  retry_count_++;  
  Envoy::Http::MessagePtr request(new Envoy::Http::RequestMessageImpl());
  request->headers().insertPath().value("/api/v1/debugconfig/"+debugConfigId_);
  request->headers().insertHost().value(std::string("squash"));
  request->headers().insertMethod().value(std::string("GET"));
  
  cm_.httpAsyncClientForCluster(squash_cluster_name).send(std::move(request), *this, timeout_);
}

Envoy::Http::FilterDataStatus SquashFilter::decodeData(Envoy::Buffer::Instance& , bool ) {
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
