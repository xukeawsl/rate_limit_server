#include <brpc/server.h>
#include <gflags/gflags.h>

#include "service/ratelimit_service_impl.h"
#include "etcd/api/etcdserverpb/rpc.pb.h"

DEFINE_int32(port, 50051, "RateLimit service port");
DEFINE_int32(num_threads, 9, "Overall number of threads for the server");

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (!gflags::ReadFromFlagsFile("../conf/gflags.conf", argv[0], true)) {
        LOG(ERROR) << "Failed to read gflags from file";
        return -1;
    }

    brpc::Server server;

    RateLimitServiceImpl rate_limit_service("../conf/ratelimit.lua");
    if (server.AddService(&rate_limit_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add RateLimit service";
        return -1;
    }

    brpc::ServerOptions options;
    options.num_threads = FLAGS_num_threads;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start RateLimit service";
        return -1;
    }

    server.RunUntilAskedToQuit();

    return 0;
}