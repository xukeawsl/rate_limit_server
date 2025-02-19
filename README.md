# 功能

提供一个通用的限流控制服务，用户请求限流服务来判断是否触发指定 token 配置的限流。服务使用令牌桶算法进行限流判断，每个 token 名称对应各自的限流配置，所有的限流配置通过 etcd 搭建的配置中心进行集中化管理，限流服务内部会监听配置中心的变更，当发生限流配置变更时，能够进行配置热更新。

限流服务通过 brpc 框架搭建，consul 作为服务注册中心，当限流服务单点性能存在瓶颈时，可以通过水平扩容+负载均衡提高整体的请求处理能力，每个 token 对应桶的状态通过 redis 进行维护，可以通过部署 redis 集群保证高可用，同样 etcd 也能集群部署。



# 选型

+ 限流服务：brpc、redis
+ 配置中心：etcd
+ 配置解析：simdjson
+ 服务注册/发现：consul



# 接口


## 限流接口

```protobuf
message RateLimitRequest {
    string token = 1; // 
}

message RateLimitResponse {
    bool allowed = 1;
}

service RateLimitService {
    rpc CheckLimit(RateLimitRequest) returns (RateLimitResponse);
}
```



## token 限流配置

以 **test_token** 为例，burst 为令牌桶容量，rate 为每秒生成的令牌数

+ 新增或修改限流配置：`etcdctl put conf/ratelimit/test_token '{"burst":5,"rate":1}'`
+ 查询所有 token 限流配置：`etcdctl get conf/ratelimit/ --prefix`
+ 删除指定 token 限流配置：`etcdctl del conf/ratelimit/test_token`



# 优化点

+ 配置中心管理类通过**双缓冲机制**避免读取时加锁
+ brpc 进行限流判断时可以考虑使用 lua 脚本来替换 redis 分布式锁的开销
+ ectd 存储 token 对应的配置的格式，为了可读性存储为 json 格式，使用 simdjson 库来解析配置，存储格式为 conf/ratelimit/<token>，可以根据前缀**周期扫描**所有 token 并更新配置（etcd 的 Watch 机制对于 C++ 来说不友好，service 名称和方法名相同，导致需要设置编译选项并且手动去掉 void 才能编译通过，而且 brpc 貌似不支持 grpc 的流式接口，而是自己的一套）
+ 是否提供熔断机制，如下游发生整体服务不可用的时候，使用默认的配置或者使用本地限流等策略（未实现）

# 压测
使用 brpc 自带的压测工具 rpc_press 进行压力测试，压测机器为 4 核的腾讯云服务器
```bash
./rpc_press -proto=proto/ratelimit.proto -method=RateLimitService.CheckLimit -server=127.0.0.1:50051 -input='{"token":"test_token"}' -qps=100

# 100qps 压力下，平均耗时 202us
[Latency]
  avg            195 us
  50%            183 us
  70%            199 us
  90%            241 us
  95%            271 us
  97%            296 us
  99%            334 us
  99.9%          432 us
  99.99%         432 us
  max            432 us

# 1000qps 压力下，平均耗时 168us
[Latency]
  avg            168 us
  50%            165 us
  70%            171 us
  90%            183 us
  95%            194 us
  97%            206 us
  99%            243 us
  99.9%          398 us
  99.99%         839 us
  max            839 us

# 10000qps 压力下，平均耗时 198us
[Latency]
  avg            198 us
  50%            187 us
  70%            204 us
  90%            239 us
  95%            259 us
  97%            277 us
  99%            345 us
  99.9%         1669 us
  99.99%        3019 us
  max           3628 us

# 20000qps 压力下，平均耗时 296us
# 30000qps 压力下，平均耗时 460us
# 40000qps 压力下，平均耗时 763us
# 50000qps 压力下，平均耗时 1241us
[Latency]
  avg           1241 us
  50%           1036 us
  70%           1262 us
  90%           1846 us
  95%           2679 us
  97%           3685 us
  99%           5518 us
  99.9%         8228 us
  99.99%        9331 us
  max          10781 us

# 60000qps 压力下，平均耗时达到 3924 us，整机 cpu 利用率接近 70%
[Latency]
  avg           3924 us
  50%           2586 us
  70%           3637 us
  90%           7469 us
  95%          10502 us
  97%          12136 us
  99%          25485 us
  99.9%        39333 us
  99.99%       41231 us
  max          41372 us

# 70000qps 压力会出现请求超时的情况，即超过默认的 500ms 超时时间
```

# 相关

**etcd：**

+ [https://github.com/etcd-io/etcd/releases/tag/v3.5.13](https://github.com/etcd-io/etcd/releases/tag/v3.5.13)

**brpc：**

+ [https://github.com/apache/brpc](https://github.com/apache/brpc)
+ [https://brpc.apache.org/zh/docs/rpc-in-depth/load-balancing/#%E5%91%BD%E5%90%8D%E6%9C%8D%E5%8A%A1](https://brpc.apache.org/zh/docs/rpc-in-depth/load-balancing/#%E5%91%BD%E5%90%8D%E6%9C%8D%E5%8A%A1)
+ [https://brpc.apache.org/zh/docs/rpc-in-depth/locality-aware/#doublybuffereddata](https://brpc.apache.org/zh/docs/rpc-in-depth/locality-aware/#doublybuffereddata)

**simdjson:**

+ [simdjson/doc/basics.md at master · simdjson/simdjson](https://github.com/simdjson/simdjson/blob/master/doc/basics.md#using-simdjson-as-a-cmake-dependency)

**consul:**

+ [https://kingfree.gitbook.io/consul](https://kingfree.gitbook.io/consul)
+ [https://www.liwenzhou.com/posts/Go/consul/](https://www.liwenzhou.com/posts/Go/consul/)

