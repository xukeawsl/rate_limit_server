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
    static void onRedisCallComplete(brpc::Controller* redis_cntl,
                                    brpc::RedisResponse* redis_response,
                                    brpc::Controller* cntl,
                                    ::RateLimitResponse* response,
                                    ::google::protobuf::Closure* done);

private:
    brpc::Channel _redis_channel;
    std::string _lua_script_sha1;
    std::string _service_id;
    ConfigManager _conf_manager;
};