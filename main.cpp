#include <brpc/server.h>
#include <gflags/gflags.h>

#include "service/healthcheck_service.h"
#include "service/ratelimit_service_impl.h"

DEFINE_int32(port, 50051, "RateLimit service port");
DEFINE_int32(num_threads, 9, "Overall number of threads for the server");
DEFINE_string(consul_health_check_path, "/healthcheck", "Consul health check path");

namespace brpc {
namespace policy {

DECLARE_string(consul_agent_addr);

}  // namespace policy
} // namespace brpc

static bool RegisterToConsul(const std::string& service_id);
static void DeregisterFromConsul(const std::string& service_id);

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (!gflags::ReadFromFlagsFile("../conf/gflags.conf", argv[0], true)) {
        LOG(ERROR) << "Failed to read gflags from file";
        return -1;
    }

    brpc::Server server;

    HealthCheckServiceImpl health_check_service;
    if (server.AddService(&health_check_service, brpc::SERVER_DOESNT_OWN_SERVICE,
                          FLAGS_consul_health_check_path + " => HealthCheck")) {
        LOG(ERROR) << "Failed to add HealthCheck service";
        return -1;
    }

    RateLimitServiceImpl rate_limit_service("../conf/ratelimit.lua");
    if (server.AddService(&rate_limit_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add RateLimit service";
        return -1;
    }

    brpc::ServerOptions options;
    options.num_threads = FLAGS_num_threads;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Failed to start server";
        return -1;
    }

    RegisterToConsul(rate_limit_service.service_id());

    server.RunUntilAskedToQuit();

    DeregisterFromConsul(rate_limit_service.service_id());

    return 0;
}

bool RegisterToConsul(const std::string& service_id) {
    brpc::Channel consul_channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_HTTP;

    if (consul_channel.Init(brpc::policy::FLAGS_consul_agent_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Failed to initialize Consul HTTP channel";
        return false;
    }

    // 构造 HTTP 请求
    brpc::Controller cntl;
    cntl.http_request().uri() = "/v1/agent/service/register";
    cntl.http_request().set_method(brpc::HTTP_METHOD_PUT);
    cntl.http_request().set_content_type("application/json");

    // 构建 JSON 请求体（手动构造或使用 JSON 库）
    std::string json_body = R"({
        "Name": "ratelimit_service",
        "ID": ")" + service_id + R"(",
        "Address": "127.0.0.1",
        "Port": )" + std::to_string(FLAGS_port) + R"(,
        "Check": {
            "HTTP": "http://127.0.0.1:)" + std::to_string(FLAGS_port) + FLAGS_consul_health_check_path + R"(",
            "Interval": "10s",
            "Timeout": "1s"
        }
    })";

    cntl.request_attachment().append(json_body);

    // 发送请求
    consul_channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    if (cntl.Failed()) {
        LOG(ERROR) << "Consul registration failed: " << cntl.ErrorText();
        return false;
    }

    // 检查 HTTP 状态码（Consul 成功注册返回 200）
    if (cntl.http_response().status_code() != brpc::HTTP_STATUS_OK) {
        LOG(ERROR) << "Consul registration rejected, code=" 
                  << cntl.http_response().status_code()
                  << ", body=" << cntl.response_attachment().to_string();
        return false;
    }

    LOG(INFO) << "Service registered to Consul via brpc HTTP client";
    return true;
}

void DeregisterFromConsul(const std::string& service_id) {
    brpc::Channel consul_channel;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_HTTP;

    if (consul_channel.Init(brpc::policy::FLAGS_consul_agent_addr.c_str(), &options) != 0) {
        LOG(ERROR) << "Failed to initialize Consul HTTP channel";
        return;
    }

    brpc::Controller cntl;
    cntl.http_request().uri() = "/v1/agent/service/deregister/" + service_id;
    cntl.http_request().set_method(brpc::HTTP_METHOD_PUT);

    consul_channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    if (cntl.Failed()) {
        LOG(ERROR) << "Consul deregistration failed: " << cntl.ErrorText();
    } else if (cntl.http_response().status_code() != brpc::HTTP_STATUS_OK) {
        LOG(ERROR) << "Consul deregistration rejected, code=" 
                  << cntl.http_response().status_code();
    } else {
        LOG(INFO) << "Service deregistered from Consul";
    }
}