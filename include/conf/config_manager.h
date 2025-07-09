#pragma once

#include <butil/containers/doubly_buffered_data.h>

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
    static ConfigManager& instance() {
        static ConfigManager instance;
        return instance;
    }

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    bool init(const std::string& etcd_addr,
              const std::string& limit_conf_prefix);

    bool getTokenBucketConfig(const std::string& token,
                              TokenBucketConfig& config);

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    using ConfigMap = std::unordered_map<std::string, TokenBucketConfig>;

    std::string getPrefixRangeEnd(const std::string& prefix);

    bool loadInitialConfig();

    void startPeriodicScan();

    static bool Modify(ConfigMap& bg_map, const ConfigMap& new_map) {
        bg_map = new_map;
        return true;
    }

private:
    std::string _etcd_addr;
    std::string _limit_conf_prefix;
    butil::DoublyBufferedData<ConfigMap> _configMap;
};