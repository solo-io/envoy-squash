#pragma once

#include <map>
#include <string>

#include "envoy/common/optional.h"

#include "server/config/network/http_connection_manager.h"

#include "envoy/http/async_client.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "squash_filter_config.h"

namespace Solo {
namespace Squash {

class SquashFilter : public Envoy::Http::StreamDecoderFilter,
                     public Envoy::Logger::Loggable<Envoy::Logger::Id::filter>,
                     public Envoy::Http::AsyncClient::Callbacks {
public:
  SquashFilter(SquashFilterConfigSharedPtr config,
               Envoy::Upstream::ClusterManager &cm);
  ~SquashFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus
  decodeHeaders(Envoy::Http::HeaderMap &headers, bool) override;
  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance &,
                                           bool) override;
  Envoy::Http::FilterTrailersStatus
  decodeTrailers(Envoy::Http::HeaderMap &) override;
  void setDecoderFilterCallbacks(
      Envoy::Http::StreamDecoderFilterCallbacks &callbacks) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Envoy::Http::MessagePtr &&) override;
  void onFailure(Envoy::Http::AsyncClient::FailureReason) override;

private:
  enum State {
    INITIAL,
    CREATE_CONFIG,
    CHECK_ATTACHMENT,
  };
  SquashFilterConfigSharedPtr config_;
  Envoy::Upstream::ClusterManager &cm_;
  Envoy::Http::StreamDecoderFilterCallbacks *decoder_callbacks_;

  State state_;
  std::string debugConfigPath_;
  Envoy::Event::TimerPtr delay_timer_;
  Envoy::Http::AsyncClient::Request *in_flight_request_;
  std::chrono::time_point<std::chrono::steady_clock> request_deadline_;

  void pollForAttachment();
  void doneSquashing();
  const Envoy::Http::LowerCaseString &squashHeaderKey();
  const std::string &postAttachmentPath();
  const std::string &severAuthority();
  void maybeRetry();
};

} // namespace Squash
} // namespace Solo