#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

struct TokenBucketConfig {
    int64_t burst;
    int64_t rate;
};

class ConfigManager {
public:
    ConfigManager(const std::string& etcd_addr,
                  const std::string& limit_conf_prefix);
    ~ConfigManager() = default;

    bool getTokenBucketConfig(const std::string& token,
                              TokenBucketConfig& config);

private:
    using ConfigMap = std::unordered_map<std::string, TokenBucketConfig>;

    std::string getPrefixRangeEnd(const std::string& prefix);

    void loadInitialConfig();

    void startPeriodicScan();

private:
    std::string _etcd_addr;
    std::string _limit_conf_prefix;
    std::shared_ptr<ConfigMap> _configMap;
};