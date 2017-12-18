
#include <chrono>

#include "squash_filter_config.h"
#include "squash_filter_config_factory.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <stdlib.h>

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
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

TEST(SoloFilterConfigTest, NoCluster) {
  std::string json = R"EOF(
    {
      "squash_cluster" : "fake_cluster"
    }
    )EOF";

  Envoy::Json::ObjectSharedPtr config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  EXPECT_CALL(factory_context.cluster_manager_, get("fake_cluster")).WillOnce(Return(nullptr));

  EXPECT_THROW(constructSquashFilterConfigFromJson(*config, factory_context),
               Envoy::EnvoyException);
}

TEST(SoloFilterConfigTest, ParsesEnvironment) {
  std::string json = R"EOF(
    {
      "squash_cluster" : "squash",
      "attachment_template" : "{\"a\":\"{{ MISSING_ENV }}\"}"
    }
    )EOF";
  std::string expected_json = "{\"a\":\"\"}";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_EQ(expected_json, config.attachment_json());
}


TEST(SoloFilterConfigTest, ParsesAndEscapesEnvironment) {
  ::setenv("ESCAPE_ENV", "\"", 1);

  std::string json = R"EOF(
    {
      "squash_cluster" : "squash",
      "attachment_template" : "{\"a\" : \"{{ ESCAPE_ENV }}\"}"
    }
    )EOF";

  std::string expected_json = "{\"a\" : \"\\\"\"}";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_EQ(expected_json, config.attachment_json());
}

TEST(SoloFilterConfigTest, ParsesDefaultEnvironment) {
  ::setenv("POD_NAME", "pod1", 1);
  ::setenv("POD_NAMESPACE", "namespace1", 1);

  std::string json = R"EOF(
    {
      "squash_cluster" : "squash"
    }
    )EOF";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  
  auto attachment_json = config.attachment_json();
  Envoy::Json::ObjectSharedPtr attachment_json_obj = Envoy::Json::Factory::
      loadFromString(attachment_json)->getObject("spec")->getObject("attachment");

  EXPECT_EQ("pod1", attachment_json_obj->getString("pod"));
  EXPECT_EQ("namespace1", attachment_json_obj->getString("namespace"));
}
} // namespace Squash
} // namespace Solo