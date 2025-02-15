#include "conf/config_manager.h"

#include <brpc/channel.h>
#include <brpc/controller.h>
#include <gflags/gflags.h>

#include <thread>

#include "etcd/api/etcdserverpb/rpc.pb.h"
#include "simdjson.h"

DEFINE_int32(scan_interval_seconds, 10, "Scan Etcd Ratelimit Config Interval");

ConfigManager::ConfigManager(const std::string& etcd_addr,
                             const std::string& limit_conf_prefix)
    : _etcd_addr(etcd_addr), _limit_conf_prefix(limit_conf_prefix) {
    std::atomic_store(&_configMap, std::make_shared<ConfigMap>());

    loadInitialConfig();

    startPeriodicScan();
}

std::string ConfigManager::getPrefixRangeEnd(const std::string& prefix) {
    std::string end = prefix;
    if (!end.empty()) {
        end.back() = static_cast<char>(end.back() + 1);
    }
    return end;
}

void ConfigManager::loadInitialConfig() {
    auto new_map = std::make_shared<ConfigMap>();

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "h2:grpc";
    if (channel.Init(_etcd_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Failed to initialize etcd channel.";
        throw std::runtime_error("Failed to initialize etcd channel.");
    }

    etcdserverpb::RangeRequest range_request;
    range_request.set_key(_limit_conf_prefix);
    range_request.set_range_end(getPrefixRangeEnd(_limit_conf_prefix));

    etcdserverpb::RangeResponse range_response;
    brpc::Controller cntl;

    etcdserverpb::KV::Stub etcd_stub(&channel);
    etcd_stub.Range(&cntl, &range_request, &range_response, nullptr);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to get range from etcd: " << cntl.ErrorText();
        throw std::runtime_error("Fail to get range from etcd: " +
                                 cntl.ErrorText());
    }

    for (int i = 0; i < range_response.kvs_size(); ++i) {
        const auto& kv = range_response.kvs(i);
        std::string full_key = kv.key();
        if (full_key.size() <= _limit_conf_prefix.size()) continue;
        std::string token = full_key.substr(_limit_conf_prefix.size());
        std::string value = kv.value();

        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        simdjson::error_code error = parser.parse(value).get(doc);
        if (error) {
            LOG(ERROR) << "Failed to parse config for token " << token << ": "
                       << simdjson::error_message(error);
            continue;
        }

        TokenBucketConfig config;
        if (doc["burst"].get(config.burst) != simdjson::SUCCESS) {
            LOG(ERROR) << "Missing 'burst' in config for token " << token;
            continue;
        }

        if (doc["rate"].get(config.rate) != simdjson::SUCCESS) {
            LOG(ERROR) << "Missing 'rate' in config for token " << token;
            continue;
        }

        new_map->emplace(token, config);
    }

    std::atomic_store(&_configMap, new_map);
}

void ConfigManager::startPeriodicScan() {
    std::thread([this] {
        while (true) {
            std::this_thread::sleep_for(
                std::chrono::seconds(FLAGS_scan_interval_seconds));
            loadInitialConfig();
        }
    }).detach();
}

bool ConfigManager::getTokenBucketConfig(const std::string& token,
                                         TokenBucketConfig& config) {
    std::shared_ptr<ConfigMap> currentMap = std::atomic_load(&_configMap);
    auto iter = currentMap->find(token);
    if (iter == currentMap->end()) {
        LOG(ERROR) << "Token not found: " << token;
        return false;
    }
    config = iter->second;
    return true;
}