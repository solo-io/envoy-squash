#include <stdlib.h>
#include <cstdlib> 

#include "test/integration/integration.h"
#include "test/integration/utility.h"

#include "test/integration/http_integration.h"

const char* envval = "somerandomevalue";

namespace Solo {
class SquashFilterIntegrationTest : public Envoy::HttpIntegrationTest,
                                        public testing::TestWithParam<Envoy::Network::Address::IpVersion> {
public:
  SquashFilterIntegrationTest() : Envoy::HttpIntegrationTest(Envoy::Http::CodecClient::Type::HTTP1, GetParam()) {}
  /**
   * Initializer for an individual integration test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new Envoy::FakeUpstream(0, Envoy::FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_0", fake_upstreams_[0]->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new Envoy::FakeUpstream(0, Envoy::FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_squash", fake_upstreams_[1]->localAddress()->ip()->port());

  ::setenv("SQUASH_ENV_TEST", envval, 1);

    createTestServer("envoy-test.conf", {"http"});
  }

  /**
   * Destructor for an individual integration test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, SquashFilterIntegrationTest,
                        testing::ValuesIn(Envoy::TestEnvironment::getIpVersionsForTest()));

TEST_P(SquashFilterIntegrationTest, Test1) {
  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"}, {":authority", "www.solo.io"}, {"x-squash-debug", "true"}, {":path", "/"}};

  Envoy::IntegrationCodecClientPtr codec_client;
  Envoy::FakeHttpConnectionPtr fake_upstream_connection;
  Envoy::IntegrationStreamDecoderPtr response(new Envoy::IntegrationStreamDecoder(*dispatcher_));
  Envoy::FakeStreamPtr request_stream;

  codec_client = makeHttpConnection(lookupPort("http"));
  codec_client->makeHeaderOnlyRequest(headers, *response);


  fake_upstream_connection = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  request_stream = fake_upstream_connection->waitForNewStream(*dispatcher_);
  request_stream->waitForEndStream(*dispatcher_);
  
  // finish the request
  fake_upstream_connection = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
  Envoy::FakeStreamPtr original_request_stream = fake_upstream_connection->waitForNewStream(*dispatcher_);
  original_request_stream->waitForEndStream(*dispatcher_);
  
  response->waitForEndStream();

  EXPECT_EQ(std::string("POST"), request_stream->headers().Method()->value().c_str());

  EXPECT_NE(-1, request_stream->body().search(envval, std::strlen(envval), 0));

  codec_client->close();
}


} // Solo
