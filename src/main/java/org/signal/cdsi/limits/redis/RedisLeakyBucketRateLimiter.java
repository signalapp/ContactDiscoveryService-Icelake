/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.redis;

import static org.signal.cdsi.metrics.MetricsUtil.name;

import com.google.common.annotations.VisibleForTesting;
import io.lettuce.core.RedisException;
import io.lettuce.core.RedisNoScriptException;
import io.lettuce.core.ScriptOutputType;
import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Timer;
import io.micronaut.context.annotation.EachBean;
import io.micronaut.context.annotation.Requires;
import io.micronaut.retry.annotation.CircuitBreaker;
import jakarta.inject.Named;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import org.apache.commons.codec.binary.Hex;
import org.signal.cdsi.limits.LeakyBucketRateLimitConfiguration;
import org.signal.cdsi.limits.LeakyBucketRateLimiter;
import org.signal.cdsi.limits.RateLimitExceededException;
import org.signal.cdsi.util.CompletionExceptions;

@Requires(bean = StatefulRedisClusterConnection.class)
@EachBean(LeakyBucketRateLimitConfiguration.class)
public class RedisLeakyBucketRateLimiter implements LeakyBucketRateLimiter {

  private final StatefulRedisClusterConnection<String, String> redisClusterConnection;
  private final LeakyBucketRateLimitConfiguration configuration;
  private final MeterRegistry meterRegistry;
  private final Clock clock;

  private final static String VALIDATE_COUNTER_NAME = name(RedisLeakyBucketRateLimiter.class, "validate");
  private final Timer validateTimer;

  private final String script;
  private final String sha;

  private static final String SCRIPT_RESOURCE_NAME = "validate_rate_limit.lua";

  private static final String[] STRING_ARRAY = new String[0];

  public RedisLeakyBucketRateLimiter(@Named(LeakyBucketRedisClientFactory.CONNECTION_NAME) final StatefulRedisClusterConnection<String,String> redisClusterConnection,
      final LeakyBucketRateLimitConfiguration configuration,
      final MeterRegistry meterRegistry,
      final Clock clock) throws IOException {

    this.redisClusterConnection = redisClusterConnection;
    this.configuration = configuration;
    this.clock = clock;
    this.meterRegistry = meterRegistry;
    validateTimer = meterRegistry.timer(name(getClass(), "validate"), "name", configuration.getName());

    try (final InputStream inputStream = getClass().getResourceAsStream(SCRIPT_RESOURCE_NAME)) {
      if (inputStream == null) {
        // This should never happen for a statically-defined script
        throw new AssertionError("Script not found: " + SCRIPT_RESOURCE_NAME);
      }

      this.script = new String(inputStream.readAllBytes(), StandardCharsets.UTF_8);
    }

    try {
      this.sha = Hex.encodeHexString(MessageDigest.getInstance("SHA-1").digest(script.getBytes(StandardCharsets.UTF_8)));
    } catch (final NoSuchAlgorithmException e) {
      // All Java implementations are required to support SHA-1, so this should never happen
      throw new AssertionError(e);
    }
  }

  @CircuitBreaker(attempts = "${redis-leaky-bucket.circuit-breaker.attempts:3}",
      delay = "${redis-leaky-bucket.circuit-breaker.delay:500ms}",
      reset = "${redis-leaky-bucket.circuit-breaker.reset:5s}")
  CompletableFuture<Object> executeScript(final List<String> keys, final List<String> args) {
    return redisClusterConnection.async()
        .evalsha(sha, ScriptOutputType.INTEGER, keys.toArray(STRING_ARRAY), args.toArray(STRING_ARRAY))
        .toCompletableFuture()
        .exceptionallyCompose(throwable -> {
          if (throwable instanceof RedisNoScriptException) {
            return redisClusterConnection.async()
                .eval(script, ScriptOutputType.INTEGER, keys.toArray(STRING_ARRAY), args.toArray(STRING_ARRAY));
          } else if (throwable instanceof final RedisException redisException) {
            throw redisException;
          }

          throw new RedisException(throwable);
        }).toCompletableFuture();
  }

  @Override
  public CompletableFuture<Void> validate(final String key, final int amount) {
    final Instant start = clock.instant();
    final double leakRatePerMillis = (double)configuration.getLeakRateScalar() / configuration.getLeakRateDuration().toMillis();
    final List<String> keys = List.of(getBucketKey(key));
    final List<String> arguments = List.of(
        String.valueOf(configuration.getBucketSize()),
        String.valueOf(leakRatePerMillis),
        String.valueOf(this.clock.instant().toEpochMilli()),
        String.valueOf(amount));

    return executeScript(keys, arguments)
        .thenApply(overflowObj -> {
          Long overflow = (Long) overflowObj;
          meterRegistry.counter(VALIDATE_COUNTER_NAME, "outcome", overflow > 0L ? "rateLimitExceeded" : "success")
              .increment();
          if (overflow > 0L) {
            final Duration retryDuration = Duration.ofMillis(
                (long) Math.ceil((double) overflow / leakRatePerMillis));
            // in a general case, overflow could be larger than the bucket size, and might need a special
            // exception (e.g. "bad request"). However, this limiter is only validated with amount = 1, so overflow will
            // always be smaller than bucket size
            throw new CompletionException(new RateLimitExceededException(retryDuration));
          }
          return null;
        })
        .exceptionally(e -> {
          if (CompletionExceptions.unwrap(e) instanceof RateLimitExceededException) {
            throw CompletionExceptions.wrap(e);
          }
          // If the leaky bucket data store is unavailable, allow the request to proceed. This is just a rate limit
          // for connections, and the more critical token rate limit will still be enforced.
          meterRegistry.counter(VALIDATE_COUNTER_NAME, "outcome", "failOpen").increment();

          return null;
        })
        .thenRun(() -> validateTimer.record(Duration.between(start, clock.instant())));
  }

  @VisibleForTesting
  String getBucketKey(String key) {
    return "leaky_bucket::" + configuration.getName() + "::" + key;
  }
}
