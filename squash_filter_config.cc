#include <regex>
#include <string>

#include "common/common/logger.h"

#include "squash_filter.h"
#include "squash_filter_config.h"

#include "squash.pb.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Solo {
namespace Squash {

namespace Protobuf = Envoy::Protobuf;

const std::string SquashFilterConfig::DEFAULT_ATTACHMENT_TEMPLATE(R"EOF(
  {
    "spec" : {
      "attachment" : {
        "pod": {{ POD_NAME }},
        "namespace": {{ POD_NAMESPACE }},
      },
      "match_request":true
    }
  }
  )EOF");

SquashFilterConfig::SquashFilterConfig(
    const solo::squash::pb::SquashConfig &proto_config,
    Envoy::Server::Configuration::FactoryContext &context)
    : squash_cluster_name_(proto_config.squash_cluster()),
      attachment_json_(getAttachment(proto_config.attachment_template())),
      attachment_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(proto_config, attachment_timeout, 60000)),
      attachment_poll_every_(PROTOBUF_GET_MS_OR_DEFAULT(
          proto_config, attachment_poll_every, 1000)),
      squash_request_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(
          proto_config, squash_request_timeout, 1000)) {
  if (attachment_json_.empty()) {
    attachment_json_ = getAttachment(DEFAULT_ATTACHMENT_TEMPLATE);
  }
  if (!context.clusterManager().get(squash_cluster_name_)) {
    throw Envoy::EnvoyException(fmt::format(
        "squash filter: unknown cluster '{}' in squash config", squash_cluster_name_));
  }
}

std::string
SquashFilterConfig::getAttachment(const std::string &attachment_template) {
  std::string s;

  const std::regex env_regex("\\{\\{ ([a-zA-Z_]+) \\}\\}");

  auto end_last_match = attachment_template.begin();

  auto callback =
      [&s, &attachment_template, &end_last_match](
          const std::match_results<std::string::const_iterator> &match) {
        auto start_match = attachment_template.begin() + match.position(0);

        s.append(end_last_match, start_match);

        std::string envar_key = match[1].str();
        const char *env = std::getenv(envar_key.c_str());
        s.append("\"");
        if (env == nullptr) {
          ENVOY_LOG(warn, "Squash: no env for {}.", envar_key);
        } else {
          s.append(env);
        }
        s.append("\"");

        end_last_match = start_match + match.length(0);
      };

  std::sregex_iterator begin(attachment_template.begin(),
                             attachment_template.end(), env_regex),
      end;
  std::for_each(begin, end, callback);

  s.append(end_last_match, attachment_template.end());

  return s;
}

} // namespace Squash
} // namespace Solo
