#pragma once

#include <brpc/channel.h>
#include <gflags/gflags.h>

#include "healthcheck.pb.h"

class HealthCheckServiceImpl : public HealthCheckService {
public:
    HealthCheckServiceImpl() = default;
    virtual ~HealthCheckServiceImpl() = default;

    void HealthCheck(google::protobuf::RpcController* cntl_base,
                     const HttpRequest*, HttpResponse*,
                     google::protobuf::Closure* done) override;

private:
    static void CallAfterHttp(brpc::Controller* cntl,
                              const google::protobuf::Message*,
                              const google::protobuf::Message*);
};