#include <string>

#include "squash_filter.h"

#include "envoy/registry/registry.h"

namespace Solo {
namespace Squash {
namespace Configuration {


const std::string SQUASH_FILTER_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "type" : "object",
    "properties" : {
      "squash_cluster": {
        "type" : "string"
      }
    },
    "required": ["squash_cluster"],
    "additionalProperties" : false
  }
  )EOF");


class SquashFilterConfig : public Envoy::Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Envoy::Server::Configuration::HttpFilterFactoryCb createFilterFactory(const Envoy::Json::Object& json_config, const std::string&,
    Envoy::Server::Configuration::FactoryContext& context) override {
      json_config.validateSchema(SQUASH_FILTER_SCHEMA);
      
    std::string squash_cluster_name = json_config.getString("squash_cluster");

    return [&context, squash_cluster_name](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
      auto filter = new Squash::SquashFilter(context.clusterManager(), squash_cluster_name);
      callbacks.addStreamDecoderFilter(
          Envoy::Http::StreamDecoderFilterSharedPtr{filter});
    };
  }
  std::string name() override { return "squash"; }
};

/**
 * Static registration for this sample filter. @see RegisterFactory.
 */
static Envoy::Registry::RegisterFactory<SquashFilterConfig, Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // Configuration
} // Squash
} // Solo
