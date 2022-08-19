/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.redis;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.UUID;
import java.util.concurrent.CompletionException;
import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.RegisterExtension;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.signal.cdsi.limits.LeakyBucketRateLimitConfiguration;
import org.signal.cdsi.limits.RateLimitExceededException;
import org.signal.cdsi.redis.RedisClusterExtension;

/**
 * Test LeakyBucketRateLimiter, using the REDIS_CLUSTER_EXTENSION which uses a real redis cluster.
 */
class RedisLeakyBucketRateLimiterTest {

  private StatefulRedisClusterConnection<String, String> clusterConnection;
  private Clock clock;
  private LeakyBucketRateLimitConfiguration conf;

  // January 10, 2030 UTC. Chosen to be a real date in the future.
  private static final long START_SECONDS = 1894305600L;

  @RegisterExtension
  static final RedisClusterExtension REDIS_CLUSTER_EXTENSION = RedisClusterExtension.builder().build();

  private final int DEFAULT_BUCKET_SIZE = 100;

  @BeforeEach
  void setup() {
    clock = mock(Clock.class);
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(START_SECONDS));

    conf = new LeakyBucketRateLimitConfiguration("test");
    conf.setBucketSize(DEFAULT_BUCKET_SIZE);
    conf.setLeakRateDuration(Duration.ofSeconds(1));
    conf.setLeakRateScalar(10);

    clusterConnection = REDIS_CLUSTER_EXTENSION.getRedisClusterClient().connect();
  }

  @AfterEach
  void tearDown() {
    clusterConnection.close();
  }

  @Test
  void validate() throws Exception {
    RedisLeakyBucketRateLimiter limiter = new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(),
        clock);
    String aci = UUID.randomUUID().toString();

    when(clock.instant()).thenReturn(Instant.ofEpochSecond(START_SECONDS));
    limiter.validate(aci, 30).join(); // amount == 30
    limiter.validate(aci, 30).join(); // amount == 60
    limiter.validate(aci, 30).join(); // amount == 90
    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, 30).join());
    RateLimitExceededException e = (RateLimitExceededException) ce.getCause();
    // 90 + 30 = 120, an overage of 20; retryDuration (in ms) is 20 over divided by 10_000 (leak rate in ms) = 2_000ms.
    assertEquals(Duration.ofMillis(2_000), e.getRetryDuration());
    limiter.validate(aci, 10).join(); // amount == 100, exactly full

    when(clock.instant()).thenReturn(Instant.ofEpochSecond(START_SECONDS + 3)); // amount = 70, 3 removed.
    limiter.validate(aci, 30).join(); // amount = 100, exactly full, should not throw.

  }

  @Test
  void validateZeroSize() throws Exception {
    // Override bucket size to zero
    conf.setBucketSize(0);

    RedisLeakyBucketRateLimiter limiter = new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(), clock);
    String aci = UUID.randomUUID().toString();

    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, 1).join());
    RateLimitExceededException e = (RateLimitExceededException) ce.getCause();
    assertEquals(Duration.ofMillis(100), e.getRetryDuration());

    ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, 100).join());
    assertTrue(ce.getCause() instanceof RateLimitExceededException);
    e = (RateLimitExceededException) ce.getCause();
    assertEquals(Duration.ofMillis(10_000), e.getRetryDuration());
  }

  @ParameterizedTest
  @ValueSource(ints = {1, 30, 100, 1000})
  void validateOverflowThrows(int size) throws Exception {
    RedisLeakyBucketRateLimiter limiter = new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(), clock);
    String aci = UUID.randomUUID().toString();

    limiter.validate(aci, DEFAULT_BUCKET_SIZE).join(); // Exactly full, should not throw.
    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, size).join());
    RateLimitExceededException e = (RateLimitExceededException) ce.getCause();
    assertEquals(Duration.ofMillis((int) (size / 0.01)), e.getRetryDuration());
  }

  @Test
  void validateNoNegativeSize() throws Exception {
    RedisLeakyBucketRateLimiter limiter = new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(), clock);
    String aci = UUID.randomUUID().toString();

    limiter.validate(aci, DEFAULT_BUCKET_SIZE).join(); // Exactly full, should not throw.

    // Wait long to make sure we're not calculating a negative bucket size.
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(START_SECONDS + 20)); // amount = 0
    limiter.validate(aci, 100).join(); // amount == 100, should be full if we didn't incorrectly go negative.
    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, 1).join());
    RateLimitExceededException e = (RateLimitExceededException) ce.getCause();
    assertEquals(Duration.ofMillis(100), e.getRetryDuration());
  }
}
