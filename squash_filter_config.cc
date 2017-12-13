#include <string>
#include <regex>

#include "common/common/logger.h"

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
      },
      "attachment_template": {
        "type" : "string"
      }
    },
    "required": ["squash_cluster"],
    "additionalProperties" : false
  }
  )EOF");


  const std::string DEFAULT_ATTACHMENT_TEMPLATE(R"EOF(
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
  
      
class SquashFilterConfig : public Envoy::Server::Configuration::NamedHttpFilterConfigFactory,  Envoy::Logger::Loggable<Envoy::Logger::Id::config> {
public:
  Envoy::Server::Configuration::HttpFilterFactoryCb createFilterFactory(const Envoy::Json::Object& json_config, const std::string&,
    Envoy::Server::Configuration::FactoryContext& context) override {
      json_config.validateSchema(SQUASH_FILTER_SCHEMA);
      
    std::shared_ptr<std::string> squash_cluster_name = std::make_shared<std::string>(json_config.getString("squash_cluster"));
    std::shared_ptr<std::string> attachment = std::make_shared<std::string>(getattachment(json_config.getString("attachment_template", DEFAULT_ATTACHMENT_TEMPLATE)));
    
    return [&context, squash_cluster_name, attachment](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
      auto filter = new Squash::SquashFilter(context.clusterManager(), *squash_cluster_name, *attachment);
      callbacks.addStreamDecoderFilter(
          Envoy::Http::StreamDecoderFilterSharedPtr{filter});
    };
  }
  std::string name() override { return "squash"; }

static std::string getattachment(const std::string& attachment_template) {
  
        std::string s;
  
        const std::regex env_regex("\\{\\{ ([a-zA-Z_]+) \\}\\}");
        
    
        typename std::match_results<std::string::iterator>::difference_type
            positionOfLastMatch = 0;
        auto endOfLastMatch = attachment_template.begin();
    
        auto callback = [&](const std::match_results<std::string::const_iterator>& match)
        {
            auto positionOfThisMatch = match.position(0);
            auto diff = positionOfThisMatch - positionOfLastMatch;
    
            auto startOfThisMatch = endOfLastMatch;
            std::advance(startOfThisMatch, diff);
    
            s.append(endOfLastMatch, startOfThisMatch);
  
            std::string envar_key = match[1].str();
            const char* env = std::getenv(envar_key.c_str());
            s.append("\"");
            if (env == nullptr) {
              ENVOY_LOG(warn, "Squash: no env for {}.", envar_key);
            } else {
              s.append(env);
            }
            s.append("\"");
  
            auto lengthOfMatch = match.length(0);
    
            positionOfLastMatch = positionOfThisMatch + lengthOfMatch;
    
            endOfLastMatch = startOfThisMatch;
            std::advance(endOfLastMatch, lengthOfMatch);
        };
    
        std::sregex_iterator begin(attachment_template.begin(), attachment_template.end(), env_regex), end;
        std::for_each(begin, end, callback);
    
        s.append(endOfLastMatch, attachment_template.end());
    
        return s;
  
  }
};

/**
 * Static registration for this sample filter. @see RegisterFactory.
 */
static Envoy::Registry::RegisterFactory<SquashFilterConfig, Envoy::Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // Configuration
} // Squash
} // Solo
