#include "service/ratelimit_service_impl.h"

#include <fstream>

DEFINE_string(etcd_address, "127.0.0.1:2379", "Etcd server address");
DEFINE_string(limit_conf_prefix, "conf/ratelimit/",
              "RateLimiter config prefix");
DEFINE_string(redis_address, "127.0.0.1:6379", "Redis server address");
DEFINE_string(redis_password, "", "Redis server password");

bvar::LatencyRecorder g_latency_pass("limit_pass");
bvar::LatencyRecorder g_latency_reject("limit_reject");

std::string RateLimitServiceImpl::_s_lua_script = "";

bool RateLimitServiceImpl::initRedisChannel(brpc::Channel& channel) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    options.max_retry = 3;
    options.connect_timeout_ms = 500;

    if (channel.Init(FLAGS_redis_address.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize redis channel";
        throw std::runtime_error("Fail to initialize redis channel");
    }

    if (!FLAGS_redis_password.empty()) {
        brpc::Controller cnt;
        brpc::RedisRequest req;
        brpc::RedisResponse resp;

        req.AddCommand("AUTH " + FLAGS_redis_password);
        channel.CallMethod(nullptr, &cnt, &req, &resp, nullptr);

        if (cnt.Failed()) {
            LOG(ERROR) << "Failed to authenticate redis: " << cnt.ErrorText();
            return false;
        }

        if (resp.reply_size() == 0 ||
            resp.reply(0).type() != brpc::REDIS_REPLY_STATUS) {
            LOG(ERROR) << "Invalid response from redis";
            return false;
        }

        if (resp.reply(0).data() != "OK") {
            LOG(ERROR) << "Failed to authenticate redis: "
                       << resp.reply(0).data();
            return false;
        }
    }

    return true;
}

RateLimitServiceImpl::RateLimitServiceImpl(const std::string& limit_script) {

    if (!ConfigManager::instance().init(FLAGS_etcd_address, FLAGS_limit_conf_prefix)) {
        LOG(ERROR) << "Failed to initialize ConfigManager";
        throw std::runtime_error("Failed to initialize ConfigManager");
    }
    
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    options.max_retry = 3;
    options.connect_timeout_ms = 500;

    if (_redis_channel.Init(FLAGS_redis_address.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize redis channel";
        throw std::runtime_error("Fail to initialize redis channel");
    }

    if (!initRedisChannel(_redis_channel)) {
        LOG(ERROR) << "Failed to initialize Redis channel";
        throw std::runtime_error("Failed to initialize Redis channel");
    }

    std::ifstream ifs(limit_script, std::ios::in | std::ios::binary);
    if (!ifs) {
        LOG(ERROR) << "Failed to open ratelimit.lua";
        throw std::runtime_error("Failed to open ratelimit.lua");
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    _s_lua_script = oss.str();

    {
        brpc::Controller cnt;
        brpc::RedisRequest req;
        brpc::RedisResponse resp;

        req.AddCommand("SCRIPT LOAD %b", _s_lua_script.data(), _s_lua_script.size());
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
        
        _service_id = "ratelimit_service_instance_" + std::to_string(butil::gettimeofday_ms()) + "_" +
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

    std::string token = request->token();

    TokenBucketConfig config;
    if (!ConfigManager::instance().getTokenBucketConfig(token, config)) {
        cntl_base->SetFailed("Token config not found in etcd: " + token);
        return;
    }

    butil::Timer* timer = new butil::Timer;
    brpc::Controller* redis_cntl = new brpc::Controller;
    brpc::RedisResponse* redis_resp = new brpc::RedisResponse;

    timer->start();

    brpc::RedisRequest redis_req;
    redis_req.AddCommand("EVALSHA %s 1 %s %lld %lld %lld", _lua_script_sha1.c_str(),
                         token.c_str(), config.burst, config.rate, butil::gettimeofday_ms());

    auto callback = brpc::NewCallback(
        &RateLimitServiceImpl::onRedisCallComplete, token, timer, redis_cntl, redis_resp,
        cntl, response, done_guard.release(), &_redis_channel);

    _redis_channel.CallMethod(nullptr, redis_cntl, &redis_req, redis_resp,
                              callback);
}

void RateLimitServiceImpl::onRedisCallComplete(std::string token, butil::Timer* timer,
    brpc::Controller* redis_cntl, brpc::RedisResponse* redis_response,
    brpc::Controller* cntl, ::RateLimitResponse* response,
    ::google::protobuf::Closure* done, brpc::Channel* redis_channel) {
    std::unique_ptr<butil::Timer> timer_guard(timer);
    std::unique_ptr<brpc::Controller> redis_cntl_guard(redis_cntl);
    std::unique_ptr<brpc::RedisResponse> redis_resp_guard(redis_response);
    brpc::ClosureGuard done_guard(done);

    if (redis_cntl->Failed()) {
        cntl->SetFailed("Failed to call redis: " + redis_cntl->ErrorText());
        return;
    }

    if (redis_response->reply_size() == 0) {
        cntl->SetFailed("Invalid response from redis test");
        return;
    }

    if (redis_response->reply(0).is_error()) {
        LOG(WARNING) << "Redis error: " << redis_response->reply(0).error_message();

        redis_cntl->Reset();
        redis_response->Clear();

        TokenBucketConfig config;
        if (!ConfigManager::instance().getTokenBucketConfig(token, config)) {
            cntl->SetFailed("Token config not found in etcd: " + token);
            return;
        }

        std::vector<butil::StringPiece> components;
        components.emplace_back("EVAL");
        components.emplace_back(_s_lua_script);
        components.emplace_back("1");
        components.emplace_back(token);
        components.emplace_back(std::to_string(config.burst));
        components.emplace_back(std::to_string(config.rate));
        components.emplace_back(std::to_string(butil::gettimeofday_ms()));

        brpc::RedisRequest redis_req;
        redis_req.AddCommandByComponents(components.data(), components.size());

        redis_channel->CallMethod(
            nullptr, redis_cntl, &redis_req, redis_response, nullptr);

        if (redis_cntl->Failed()) {
            cntl->SetFailed("Failed to call redis: " + redis_cntl->ErrorText());
            return;
        }

        if (redis_response->reply_size() == 0 ||
            redis_response->reply(0).is_error()) {
            cntl->SetFailed("Invalid response from redis test2");
            return;
        }
    }

    if (redis_response->reply(0).type() != brpc::REDIS_REPLY_INTEGER) {
        cntl->SetFailed("Invalid response type from redis");
        return;
    }

    timer->stop();

    if (redis_response->reply(0).integer() == 0) {
        response->set_allowed(false);
        g_latency_reject << timer->n_elapsed();
    } else {
        response->set_allowed(true);
        g_latency_pass << timer->n_elapsed();
    }
}