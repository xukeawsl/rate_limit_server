local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local rate = tonumber(ARGV[2])

-- 设置键的基准过期时间（单位：秒），例如1小时
local base_ttl = 3600

-- 获取Redis服务器时间（秒 + 微秒）
local redis_time = redis.call('TIME')
local now = tonumber(redis_time[1]) * 1000 + math.floor(tonumber(redis_time[2]) / 1000)  -- 毫秒时间戳

-- 检查键是否存在
local exists = redis.call('EXISTS', key)

-- 如果键不存在，初始化令牌桶并设置TTL
if exists == 0 then
    redis.call('HMSET', key, 'tokens', capacity, 'last_refill', now)
    redis.call('EXPIRE', key, base_ttl)  -- 初始TTL
end

-- 获取当前令牌状态
local data = redis.call('HMGET', key, 'tokens', 'last_refill')
local tokens = tonumber(data[1])
local last_refill = tonumber(data[2])

-- 计算新令牌
local delta = math.max(now - last_refill, 0) / 1000  -- 转换为秒
local new_tokens = delta * rate
tokens = math.min(tokens + new_tokens, capacity)

-- 判断是否允许请求
if tokens >= 1 then
    tokens = tokens - 1
    redis.call('HMSET', key, 'tokens', tokens, 'last_refill', now)
    -- 每次成功消费令牌时续期TTL（示例续期30分钟）
    redis.call('EXPIRE', key, 1800)
    return 1  -- 允许
else
    -- 令牌不足时，计算剩余TTL（避免频繁设置）
    local remaining_ttl = redis.call('TTL', key)
    if remaining_ttl < 300 then  -- 剩余TTL小于5分钟时续期
        redis.call('EXPIRE', key, 600)  -- 续期10分钟
    end
    return 0  -- 拒绝
end