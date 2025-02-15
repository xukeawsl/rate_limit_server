#include "service/ratelimit_service_impl.h"

#include <fstream>

DEFINE_string(etcd_address, "127.0.0.1:2379", "Etcd server address");
DEFINE_string(limit_conf_prefix, "conf/ratelimit/",
              "RateLimiter config prefix");
DEFINE_string(redis_address, "127.0.0.1:6379", "Redis server address");
DEFINE_string(redis_password, "", "Redis server password");

RateLimitServiceImpl::RateLimitServiceImpl(const std::string& limit_script)
    : _conf_manager(FLAGS_etcd_address, FLAGS_limit_conf_prefix) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    options.max_retry = 3;
    options.connect_timeout_ms = 500;

    if (_redis_channel.Init(FLAGS_redis_address.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize redis channel";
        throw std::runtime_error("Fail to initialize redis channel");
    }

    if (!FLAGS_redis_password.empty()) {
        brpc::Controller cnt;
        brpc::RedisRequest req;
        brpc::RedisResponse resp;

        req.AddCommand("AUTH " + FLAGS_redis_password);
        _redis_channel.CallMethod(nullptr, &cnt, &req, &resp, nullptr);

        if (cnt.Failed()) {
            LOG(ERROR) << "Failed to authenticate redis: " << cnt.ErrorText();
            throw std::runtime_error("Failed to authenticate redis");
        }

        if (resp.reply_size() == 0 ||
            resp.reply(0).type() != brpc::REDIS_REPLY_STATUS) {
            LOG(ERROR) << "Invalid response from redis";
            throw std::runtime_error("Invalid response from redis");
        }

        if (resp.reply(0).data() != "OK") {
            LOG(ERROR) << "Failed to authenticate redis: "
                       << resp.reply(0).data();
            throw std::runtime_error("Failed to authenticate redis");
        }
    }

    std::ifstream ifs(limit_script, std::ios::in | std::ios::binary);
    if (!ifs) {
        LOG(ERROR) << "Failed to open ratelimit.lua";
        throw std::runtime_error("Failed to open ratelimit.lua");
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string lua_script = oss.str();

    {
        brpc::Controller cnt;
        brpc::RedisRequest req;
        brpc::RedisResponse resp;

        req.AddCommand("SCRIPT LOAD %b", lua_script.data(), lua_script.size());
        _redis_channel.CallMethod(nullptr, &cnt, &req, &resp, nullptr);

        if (cnt.Failed()) {
            LOG(ERROR) << "Failed to load lua script: " << cnt.ErrorText();
            exit(EXIT_FAILURE);
        }

        if (resp.reply_size() == 0 ||
            resp.reply(0).type() != brpc::REDIS_REPLY_STRING) {
            LOG(ERROR) << "Invalid response from redis";
            exit(EXIT_FAILURE);
        }

        _lua_script_sha1 = resp.reply(0).data().as_string();
        LOG(INFO) << "Lua script loaded: " << _lua_script_sha1;
    }

    {
        brpc::Controller cnt;
        brpc::RedisRequest req;
        brpc::RedisResponse resp;

        req.AddCommand("SETNX ratelimit_service_id 0");
        _redis_channel.CallMethod(nullptr, &cnt, &req, &resp, nullptr);

        if (cnt.Failed()) {
            LOG(ERROR) << "Failed to set unique service ID: "
                       << cnt.ErrorText();
            throw std::runtime_error("Failed to set unique service ID");
        }

        cnt.Reset();
        req.Clear();
        req.AddCommand("INCR ratelimit_service_id");
        _redis_channel.CallMethod(nullptr, &cnt, &req, &resp, nullptr);

        if (cnt.Failed()) {
            LOG(ERROR) << "Failed to increment unique service ID: "
                       << cnt.ErrorText();
            throw std::runtime_error("Failed to increment unique service ID");
        }

        if (resp.reply_size() == 0 ||
            resp.reply(0).type() != brpc::REDIS_REPLY_INTEGER) {
            LOG(ERROR) << "Invalid response from redis";
            throw std::runtime_error("Invalid response from redis");
        }

        _service_id = "ratelimit_service_instance_" +
                      std::to_string(resp.reply(0).integer());
        LOG(INFO) << "Generated Service ID: " << _service_id;
    }
}

void RateLimitServiceImpl::CheckLimit(
    ::google::protobuf::RpcController* cntl_base,
    const ::RateLimitRequest* request, ::RateLimitResponse* response,
    ::google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

    brpc::Controller* redis_cntl = new brpc::Controller;
    brpc::RedisResponse* redis_resp = new brpc::RedisResponse;

    std::string token = request->token();

    TokenBucketConfig config;
    if (!_conf_manager.getTokenBucketConfig(token, config)) {
        cntl_base->SetFailed("Token config not found in etcd: " + token);
        return;
    }

    brpc::RedisRequest redis_req;
    redis_req.AddCommand("EVALSHA %s 1 %s %lld %lld", _lua_script_sha1.c_str(),
                         token.c_str(), config.burst, config.rate);

    auto callback = brpc::NewCallback(
        &RateLimitServiceImpl::onRedisCallComplete, redis_cntl, redis_resp,
        cntl, response, done_guard.release());

    _redis_channel.CallMethod(nullptr, redis_cntl, &redis_req, redis_resp,
                              callback);
}

void RateLimitServiceImpl::onRedisCallComplete(
    brpc::Controller* redis_cntl, brpc::RedisResponse* redis_response,
    brpc::Controller* cntl, ::RateLimitResponse* response,
    ::google::protobuf::Closure* done) {
    std::unique_ptr<brpc::Controller> redis_cntl_guard(redis_cntl);
    std::unique_ptr<brpc::RedisResponse> redis_resp_guard(redis_response);
    brpc::ClosureGuard done_guard(done);

    if (redis_cntl->Failed()) {
        cntl->SetFailed("Failed to call redis: " + redis_cntl->ErrorText());
        return;
    }

    if (redis_response->reply_size() == 0) {
        cntl->SetFailed("Invalid response from redis");
        return;
    }

    const auto& reply = redis_response->reply(0);

    if (reply.type() != brpc::REDIS_REPLY_INTEGER) {
        cntl->SetFailed("Invalid response type from redis");
        return;
    }

    if (reply.integer() == 0) {
        response->set_allowed(false);
    } else {
        response->set_allowed(true);
    }
}