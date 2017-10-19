#include <string>

#include "squash_filter.h"

#include "envoy/registry/registry.h"

namespace Solo {
namespace Squash {
namespace Configuration {

class SquashFilterConfig : public Envoy::Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Envoy::Server::Configuration::HttpFilterFactoryCb createFilterFactory(const Envoy::Json::Object& json_config, const std::string&,
    Envoy::Server::Configuration::FactoryContext& context) override {

    return [context](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
      auto filter = new Squash::SquashFilter(context.clusterManager());
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
