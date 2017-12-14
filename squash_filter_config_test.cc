
#include <chrono>

#include "squash_filter_config.h"
#include "squash_filter_config_factory.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Solo {
namespace Squash {

namespace {
SquashFilterConfig constructSquashFilterConfigFromJson(
    const Envoy::Json::Object &json, Envoy::Server::Configuration::FactoryContext &context) {
  solo::squash::pb::SquashConfig proto_config;
  Configuration::SquashFilterConfigFactory::translateSquashFilter(json,
                                                                  proto_config);
  return SquashFilterConfig(proto_config, context);
}
} // namespace

TEST(SoloFilterConfigTest, DecodeHeaderContinuesOnClientFail) {
  std::string json = R"EOF(
    {
    }
    )EOF";

  Envoy::Json::ObjectSharedPtr config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructSquashFilterConfigFromJson(*config, factory_context),
               Envoy::EnvoyException);
}

} // namespace Squash
} // namespace Solo