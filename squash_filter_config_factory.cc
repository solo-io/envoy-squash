#include <string>

#include "common/common/logger.h"

#include "squash_filter.h"
#include "squash_filter_config.h"
#include "squash_filter_config_factory.h"

#include "common/config/json_utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "envoy/registry/registry.h"

namespace Solo {
namespace Squash {
namespace Configuration {

namespace Protobuf = Envoy::Protobuf;

const std::string SquashFilterConfigFactory::SQUASH_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "type" : "object",
    "properties" : {
      "squash_cluster": {
        "type" : "string"
      },
      "attachment_template": {
        "type" : "string"
      },
      "attachment_timeout_ms": {
        "type" : "number"
      },
      "attachment_poll_every_ms": {
        "type" : "number"
      },
      "squash_request_timeout_ms": {
        "type" : "number"
      }
    },
    "required": ["squash_cluster"],
    "additionalProperties" : false
  }
  )EOF");

Envoy::Server::Configuration::HttpFilterFactoryCb
SquashFilterConfigFactory::createFilterFactory(
    const Envoy::Json::Object &json_config, const std::string &,
    Envoy::Server::Configuration::FactoryContext &context) {
  json_config.validateSchema(SQUASH_FILTER_SCHEMA);
  solo::squash::pb::SquashConfig proto_config;

  translateSquashFilter(json_config, proto_config);

  return createFilter(proto_config, context);
}

Envoy::Server::Configuration::HttpFilterFactoryCb
SquashFilterConfigFactory::createFilterFactoryFromProto(
    const Envoy::Protobuf::Message &proto_config, const std::string &,
    Envoy::Server::Configuration::FactoryContext &context) {
  return createFilter(
      //    Envoy::MessageUtil::downcastAndValidate<const
      //    solo::squash::pb::SquashConfig&>(proto_config), // yuval-k: use this
      //    when using new version of envoy
      dynamic_cast<const solo::squash::pb::SquashConfig &>(proto_config),
      context);
}

Envoy::Server::Configuration::HttpFilterFactoryCb
SquashFilterConfigFactory::createFilter(
    const solo::squash::pb::SquashConfig &proto_config,
    Envoy::Server::Configuration::FactoryContext &context) {

  SquashFilterConfigSharedPtr config = std::make_shared<SquashFilterConfig>(
      SquashFilterConfig(proto_config, context));

  return [&context,
          config](Envoy::Http::FilterChainFactoryCallbacks &callbacks) -> void {
    auto filter = new Squash::SquashFilter(config, context.clusterManager());
    callbacks.addStreamDecoderFilter(
        Envoy::Http::StreamDecoderFilterSharedPtr{filter});
  };
}

void SquashFilterConfigFactory::translateSquashFilter(const Envoy::Json::Object &json_config,
                           solo::squash::pb::SquashConfig &proto_config) {

  JSON_UTIL_SET_STRING(json_config, proto_config, squash_cluster);
  JSON_UTIL_SET_STRING(json_config, proto_config, attachment_template);
  JSON_UTIL_SET_DURATION(json_config, proto_config, attachment_timeout);
  JSON_UTIL_SET_DURATION(json_config, proto_config, attachment_poll_every);
  JSON_UTIL_SET_DURATION(json_config, proto_config, squash_request_timeout);
}

/**
 * Static registration for this sample filter. @see RegisterFactory.
 */
static Envoy::Registry::RegisterFactory<
    SquashFilterConfigFactory,
    Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Configuration
} // namespace Squash
} // namespace Solo
