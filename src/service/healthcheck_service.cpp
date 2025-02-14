#include "service/healthcheck_service.h"

void HealthCheckServiceImpl::HealthCheck(google::protobuf::RpcController* cntl_base,
                                         const HttpRequest*,
                                         HttpResponse*,
                                         google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);

    brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

    cntl->set_after_rpc_resp_fn(std::bind(&HealthCheckServiceImpl::CallAfterHttp,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    cntl->response_attachment().append("OK");
    cntl->http_response().set_status_code(brpc::HTTP_STATUS_OK);
}

void HealthCheckServiceImpl::CallAfterHttp(brpc::Controller*,
                                           const google::protobuf::Message*,
                                           const google::protobuf::Message*) {
    LOG(INFO) << "health check completed";
}