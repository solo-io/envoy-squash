#pragma once

#include <string>
#include <map>

#include "server/config/network/http_connection_manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

#include "aws_authenticator.h"

namespace Solo {
namespace Squash {

struct Function {
  std::string func_name_;
  std::string hostname_;
  std::string region_;
};

typedef std::map<std::string, Function> ClusterFunctionMap;


class SquashFilter : public Envoy::Http::StreamDecoderFilter,  public Envoy::Logger::Loggable<Envoy::Logger::Id::filter> , public Envoy::Http::AsyncClient::Callbacks{
public:
  SquashFilter(Envoy::Upstream::ClusterManager& cm);
  ~SquashFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::HeaderMap& headers, bool) override;
  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance&, bool) override;
  Envoy::Http::FilterTrailersStatus decodeTrailers(Envoy::Http::HeaderMap&) override;
  void setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Envoy::Http::MessagePtr&&) override;
  void onFailure(Envoy::Http::AsyncClient::FailureReason) override;

private:
  Envoy::Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  Envoy::Upstream::ClusterManager& cm_;
  enum State {
    INITIAL,
    CREATE_CONFIG,
    CHECK_ATTACHMENT,
  };
  State state_;
  std::string debugConfigId_;
  Optional<std::chrono::milliseconds> timeout_;
  
  pollForAttachment();
};

} // Http
} // Envoy