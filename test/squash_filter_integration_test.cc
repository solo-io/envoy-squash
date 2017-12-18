#include <cstdlib>
#include <stdlib.h>

#include "test/integration/integration.h"
#include "test/integration/utility.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"

#define ENV_VAR_VALUE "somerandomevalue"

namespace Solo {

class SquashFilterIntegrationTest
    : public Envoy::HttpIntegrationTest,
      public testing::TestWithParam<Envoy::Network::Address::IpVersion> {
public:
  SquashFilterIntegrationTest()
      : Envoy::HttpIntegrationTest(Envoy::Http::CodecClient::Type::HTTP1,
                                   GetParam()) {}

  /**
   * Initializer for an individual integration test.
   */
  void SetUp() override {
    fake_upstreams_.emplace_back(new Envoy::AutonomousUpstream(
        0, Envoy::FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream", fake_upstreams_[0]->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new Envoy::FakeUpstream(
        0, Envoy::FakeHttpConnection::Type::HTTP1, version_));
    registerPort("upstream_squash",
                 fake_upstreams_[1]->localAddress()->ip()->port());
    fake_upstreams_.back()->set_allow_unexpected_disconnects(true);

    ::setenv("SQUASH_ENV_TEST", ENV_VAR_VALUE, 1);

    createTestServer("test/envoy-test.yaml", {"http"});

    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  /**
   * Destructor for an individual integration test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  Envoy::IntegrationStreamDecoderPtr
  sendDebugRequest(Envoy::IntegrationCodecClientPtr &codec_client) {
    Envoy::IntegrationStreamDecoderPtr response(
        new Envoy::IntegrationStreamDecoder(*dispatcher_));
    Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                           {":authority", "www.solo.io"},
                                           {"x-squash-debug", "true"},
                                           {":path", "/getsomething"}};
    codec_client->makeHeaderOnlyRequest(headers, *response);
    return response;
  }

  Envoy::FakeStreamPtr
  sendSquash(Envoy::FakeHttpConnectionPtr &fake_squash_connection,
             std::string status, std::string body) {

    Envoy::FakeStreamPtr request_stream =
        fake_squash_connection->waitForNewStream(*dispatcher_);
    request_stream->waitForEndStream(*dispatcher_);
    if (body.empty()) {
      request_stream->encodeHeaders(
          Envoy::Http::TestHeaderMapImpl{{":status", status}}, true);
    } else {
      request_stream->encodeHeaders(
          Envoy::Http::TestHeaderMapImpl{{":status", status}}, false);
      Envoy::Buffer::OwnedImpl creatrespbuffer(body);
      request_stream->encodeData(creatrespbuffer, true);
    }
    return request_stream;
  }

  Envoy::FakeStreamPtr
  sendSquashCreate(Envoy::FakeHttpConnectionPtr &fake_squash_connection,
                   std::string body) {
    return sendSquash(fake_squash_connection, "201", body);
  }

  Envoy::FakeStreamPtr
  sendSquashOk(Envoy::FakeHttpConnectionPtr &fake_squash_connection,
               std::string body) {
    return sendSquash(fake_squash_connection, "200", body);
  }

  Envoy::IntegrationCodecClientPtr codec_client_;
};

INSTANTIATE_TEST_CASE_P(
    IpVersions, SquashFilterIntegrationTest,
    testing::ValuesIn(Envoy::TestEnvironment::getIpVersionsForTest()));

TEST_P(SquashFilterIntegrationTest, TestHappyPath) {

  Envoy::IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);

  Envoy::FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  Envoy::FakeStreamPtr create_stream = sendSquashCreate(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                              "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                              "\"image\":\"debug\",\"node\":\"debug-node\"},"
                              "\"status\":{\"state\":\"none\"}}");

  // respond to read attachment request
  Envoy::FakeStreamPtr get_stream =
      sendSquashOk(fake_squash_connection,
                   "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                   "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                   "\"debug-node\"},\"status\":{\"state\":\"attached\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("POST", create_stream->headers().Method()->value().c_str());
  // make sure the env var was replaced
  const char *expectedbody =
      "{\"spec\": { \"attachment\" : { \"env\": \"" ENV_VAR_VALUE "\" } } }";
  EXPECT_EQ(0, create_stream->body().search(expectedbody,
                                            std::strlen(expectedbody), 0));
  EXPECT_EQ(std::strlen(expectedbody), create_stream->body().length());

  // The second request should be fore the created object
  EXPECT_STREQ("GET", get_stream->headers().Method()->value().c_str());
  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, ErrorAttaching) {

  Envoy::IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  Envoy::FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  Envoy::FakeStreamPtr create_stream = sendSquashCreate(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                              "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                              "\"image\":\"debug\",\"node\":\"debug-node\"},"
                              "\"status\":{\"state\":\"none\"}}");
  // respond to read attachment request with error!
  Envoy::FakeStreamPtr get_stream =
      sendSquashOk(fake_squash_connection,
                   "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                   "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                   "\"debug-node\"},\"status\":{\"state\":\"error\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, TimeoutAttaching) {

  Envoy::IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  Envoy::FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  Envoy::FakeStreamPtr create_stream = sendSquashCreate(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                              "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                              "\"image\":\"debug\",\"node\":\"debug-node\"},"
                              "\"status\":{\"state\":\"none\"}}");
  // respond to read attachment. since attachment_timeout is smaller than the squash
  // attachment_poll_every  config, just one response is enough
  Envoy::FakeStreamPtr get_stream =
      sendSquashOk(fake_squash_connection,
                   "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},\"spec\":{"
                   "\"attachment\":{\"a\":\"b\"},\"image\":\"debug\",\"node\":"
                   "\"debug-node\"},\"status\":{\"state\":\"attaching\"}}");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, ErrorNoSquashServer) {

  Envoy::IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);

  // Don't respond to anything. squash filter should timeout within
  // squash_request_timeout and continue the request.
  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
}

TEST_P(SquashFilterIntegrationTest, BadCreateResponse) {

  Envoy::IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  Envoy::FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  Envoy::FakeStreamPtr create_stream =
      sendSquashCreate(fake_squash_connection, "not json...");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

TEST_P(SquashFilterIntegrationTest, BadGetResponse) {

  Envoy::IntegrationStreamDecoderPtr response = sendDebugRequest(codec_client_);
  Envoy::FakeHttpConnectionPtr fake_squash_connection =
      fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);

  // respond to create request
  Envoy::FakeStreamPtr create_stream = sendSquashCreate(
      fake_squash_connection, "{\"metadata\":{\"name\":\"oF8iVdiJs5\"},"
                              "\"spec\":{\"attachment\":{\"a\":\"b\"},"
                              "\"image\":\"debug\",\"node\":\"debug-node\"},"
                              "\"status\":{\"state\":\"none\"}}");
  // respond to read attachment request with error!
  Envoy::FakeStreamPtr get_stream =
      sendSquashOk(fake_squash_connection, "not json...");

  response->waitForEndStream();

  EXPECT_STREQ("200", response->headers().Status()->value().c_str());

  codec_client_->close();
  fake_squash_connection->close();
  fake_squash_connection->waitForDisconnect();
}

} // namespace Solo
