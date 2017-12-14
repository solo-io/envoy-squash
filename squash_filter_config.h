#pragma once

#include <string>
#include <regex>

#include "common/common/logger.h"

#include "squash.pb.h"

#include "common/protobuf/protobuf.h"

namespace Solo {
namespace Squash {
      
class SquashFilterConfig :  Envoy::Logger::Loggable<Envoy::Logger::Id::config> {
public:
  SquashFilterConfig(const solo::squash::pb::SquashConfig& proto_config);
  const std::string& squash_cluster_name() { return squash_cluster_name_; }
  const std::string& attachment_json() { return attachment_json_; }
  const std::chrono::milliseconds& attachment_timeout() { return attachment_timeout_; }
  const std::chrono::milliseconds& attachment_poll_every() { return attachment_poll_every_; }
  const std::chrono::milliseconds& squash_request_timeout() { return squash_request_timeout_; }
  
private:
  const static std::string DEFAULT_ATTACHMENT_TEMPLATE;

  std::string getAttachment(const std::string& attachment_template);

  std::string squash_cluster_name_;
  std::string attachment_json_;
  std::chrono::milliseconds attachment_timeout_;
  std::chrono::milliseconds attachment_poll_every_;
  std::chrono::milliseconds squash_request_timeout_;
};

typedef std::shared_ptr<SquashFilterConfig> SquashFilterConfigSharedPtr;

} // Squash
} // Solo
