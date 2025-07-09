#pragma once

#include <brpc/callback.h>
#include <brpc/channel.h>
#include <brpc/redis.h>
#include <gflags/gflags.h>

#include "conf/config_manager.h"
#include "ratelimit.pb.h"

class RateLimitServiceImpl : public RateLimitService {
public:
    explicit RateLimitServiceImpl(const std::string& limit_script);
    virtual ~RateLimitServiceImpl() = default;

    void CheckLimit(::google::protobuf::RpcController* controller,
                    const ::RateLimitRequest* request,
                    ::RateLimitResponse* response,
                    ::google::protobuf::Closure* done) override;

    std::string service_id() const { return _service_id; }

private:
    static bool initRedisChannel(brpc::Channel& channel);

    static void onRedisCallComplete(std::string token, butil::Timer* timer,
                                    brpc::Controller* redis_cntl,
                                    brpc::RedisResponse* redis_response,
                                    brpc::Controller* cntl,
                                    ::RateLimitResponse* response,
                                    ::google::protobuf::Closure* done,
                                    brpc::Channel* redis_channel);

private:
    brpc::Channel _redis_channel;
    static std::string _s_lua_script;
    std::string _lua_script_sha1;
    std::string _service_id;
};