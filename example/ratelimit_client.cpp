#include <brpc/channel.h>
#include <gflags/gflags.h>
#include <chrono>
#include <thread>

#include "ratelimit.pb.h"

DEFINE_string(server, "consul://ratelimit_service", "RateLimit service address");
DEFINE_string(token, "test_token", "Token to be checked");
DEFINE_int32(interval_ms, 500, "Interval in milliseconds");
DEFINE_int32(count, 10, "Number of requests");
DEFINE_int32(threads, 1, "Number of threads");

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::vector<std::thread> threads;

    for (int i = 0; i < FLAGS_threads; ++i) {
        threads.push_back(std::thread([]() {
            brpc::Channel channel;
            if (channel.Init(FLAGS_server.c_str(), "rr", NULL) != 0) {
                LOG(ERROR) << "Fail to initialize channel";
                return;
            }

            RateLimitService_Stub stub(&channel);

            RateLimitRequest request;
            request.set_token(FLAGS_token);

            RateLimitResponse response;
            brpc::Controller cntl;

            for (int i = 0; i < FLAGS_count; i++) {
                stub.CheckLimit(&cntl, &request, &response, NULL);
                if (cntl.Failed()) {
                    LOG(ERROR) << "Fail to send request: " << cntl.ErrorText();
                    return;
                }

                if (response.allowed()) {
                    LOG(INFO) << "thread " << std::this_thread::get_id() << ",Request " << i << " allowed";
                } else {
                    LOG(INFO) << "thread " << std::this_thread::get_id() << ",Request " << i << " denied";
                }

                cntl.Reset();
                std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_interval_ms));
            }
        }));
    }

    for (auto& t : threads) {
        t.join();
    }

    return 0;
}