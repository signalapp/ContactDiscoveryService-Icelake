local bucketId = KEYS[1]

local bucketSize = tonumber(ARGV[1])
local leakRatePerMillis = tonumber(ARGV[2])
local currentTimeMillis = tonumber(ARGV[3])
local amount = tonumber(ARGV[4])

local spaceUsed
local lastUpdateTimeMillis

if redis.call("EXISTS", bucketId) == 1 then
  spaceUsed, lastUpdateTimeMillis = unpack(redis.call("HMGET", bucketId, "spaceUsed", "lastUpdateTimeMillis"))
  spaceUsed = tonumber(spaceUsed)
  lastUpdateTimeMillis = tonumber(lastUpdateTimeMillis)
else
  spaceUsed = 0
  lastUpdateTimeMillis = currentTimeMillis
end

local elapsedTimeMillis = currentTimeMillis - lastUpdateTimeMillis
local currentSpaceUsed = math.max(0, math.ceil(spaceUsed - (elapsedTimeMillis * leakRatePerMillis)))
local bucketSpaceRemaining = bucketSize - currentSpaceUsed

if bucketSpaceRemaining >= amount then
  currentSpaceUsed = currentSpaceUsed + amount
  redis.call("HMSET", bucketId, "spaceUsed", tostring(currentSpaceUsed), "lastUpdateTimeMillis", tostring(currentTimeMillis))

  -- Once the bucket has fully emptied, we can just discard it since an empty bucket is the same as no bucket as a
  -- starting point.
  redis.call("PEXPIRE", bucketId, currentSpaceUsed / leakRatePerMillis)

  return 0
else
  -- return bucket overflow amount
  return amount - bucketSpaceRemaining
end
