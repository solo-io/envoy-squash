
#include <chrono>

#include "squash_filter.h"
#include "squash_filter_config.h"

#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Solo {
namespace Squash {

TEST(SoloFilterTest, DecodeHeaderContinuesOnClientFail) {

  NiceMock<Envoy::Upstream::MockClusterManager> cm;
  solo::squash::pb::SquashConfig p;
  p.set_squash_cluster("squash");
  SquashFilterConfigSharedPtr config(new SquashFilterConfig(p));
  EXPECT_CALL(cm, httpAsyncClientForCluster("squash"))
      .WillOnce(ReturnRef(cm.async_client_));

  EXPECT_CALL(cm.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Envoy::Http::MessagePtr &,
                           Envoy::Http::AsyncClient::Callbacks &callbacks,
                           const Envoy::Optional<std::chrono::milliseconds> &)
                           -> Envoy::Http::AsyncClient::Request * {
        callbacks.onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);
        return nullptr;
      }));

  SquashFilter filter(config, cm);

  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-squash-debug", "true"},
                                         {":path", "/getsomething"}};

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter.decodeHeaders(headers, false));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue,
            filter.decodeTrailers(headers));
}

} // namespace Squash
} // namespace Solo