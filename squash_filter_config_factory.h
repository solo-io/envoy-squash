#pragma once

#include <regex>
#include <string>

#include "common/common/logger.h"

#include "squash_filter.h"
#include "squash_filter_config.h"

namespace Solo {
namespace Squash {
namespace Configuration {

class SquashFilterConfigFactory
    : public Envoy::Server::Configuration::NamedHttpFilterConfigFactory,
      Envoy::Logger::Loggable<Envoy::Logger::Id::config> {
public:
  Envoy::Server::Configuration::HttpFilterFactoryCb createFilterFactory(
      const Envoy::Json::Object &json_config, const std::string &,
      Envoy::Server::Configuration::FactoryContext &context) override;

  Envoy::Server::Configuration::HttpFilterFactoryCb
  createFilterFactoryFromProto(
      const Envoy::Protobuf::Message &proto_config, const std::string &,
      Envoy::Server::Configuration::FactoryContext &context) override;

  std::string name() override { return "squash"; }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return Envoy::ProtobufTypes::MessagePtr{
        new solo::squash::pb::SquashConfig()};
  }

private:
  const static std::string SQUASH_FILTER_SCHEMA;

  Envoy::Server::Configuration::HttpFilterFactoryCb
  createFilter(const solo::squash::pb::SquashConfig &proto_config,
               Envoy::Server::Configuration::FactoryContext &context);
};

} // namespace Configuration
} // namespace Squash
} // namespace Solo
