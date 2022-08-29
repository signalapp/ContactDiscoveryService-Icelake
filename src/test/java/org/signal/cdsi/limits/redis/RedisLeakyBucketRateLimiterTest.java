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

import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.UUID;
import java.util.concurrent.CompletionException;
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

  private static final Instant CURRENT_TIME = Instant.now();

  @RegisterExtension
  static final RedisClusterExtension REDIS_CLUSTER_EXTENSION = RedisClusterExtension.builder().build();

  private static final int DEFAULT_BUCKET_SIZE = 100;

  private static final Duration MAXIMUM_CLOCK_DRIFT = Duration.ofSeconds(2);

  @BeforeEach
  void setup() {
    clock = mock(Clock.class);
    when(clock.instant()).thenReturn(CURRENT_TIME);

    conf = new LeakyBucketRateLimitConfiguration("test");
    conf.setBucketSize(DEFAULT_BUCKET_SIZE);
    conf.setLeakRateDuration(Duration.ofMinutes(1));
    conf.setLeakRateScalar(10);

    clusterConnection = REDIS_CLUSTER_EXTENSION.getRedisClusterClient().connect();
  }

  @AfterEach
  void tearDown() {
    clusterConnection.close();
  }

  @Test
  void validate() throws Exception {
    final RedisLeakyBucketRateLimiter limiter =
        new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(), clock);

    final String aci = UUID.randomUUID().toString();
    final String bucketKey = limiter.getBucketKey(aci);

    limiter.validate(aci, 30).join(); // amount == 30
    assertApproximateTtl(bucketKey, conf.getLeakRateDuration().multipliedBy(30 / conf.getLeakRateScalar()));

    limiter.validate(aci, 30).join(); // amount == 60
    assertApproximateTtl(bucketKey, conf.getLeakRateDuration().multipliedBy(60 / conf.getLeakRateScalar()));

    limiter.validate(aci, 30).join(); // amount == 90
    assertApproximateTtl(bucketKey, conf.getLeakRateDuration().multipliedBy(90 / conf.getLeakRateScalar()));

    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, 30).join());
    RateLimitExceededException rateLimitException = (RateLimitExceededException) ce.getCause();

    // Expected overage is 20 permits
    assertEquals(conf.getLeakRateDuration().multipliedBy(20 / conf.getLeakRateScalar()), rateLimitException.getRetryDuration());

    limiter.validate(aci, 10).join(); // amount == 100, exactly full
    assertApproximateTtl(bucketKey, conf.getLeakRateDuration().multipliedBy(100 / conf.getLeakRateScalar()));

    when(clock.instant()).thenReturn(CURRENT_TIME.plus(conf.getLeakRateDuration().multipliedBy(30 / conf.getLeakRateScalar()))); // amount = 70, 3 removed.
    limiter.validate(aci, 30).join(); // amount = 100, exactly full, should not throw.
    assertApproximateTtl(bucketKey, conf.getLeakRateDuration().multipliedBy(100 / conf.getLeakRateScalar()));
  }

  void assertApproximateTtl(final String key, final Duration expectedTtl) {
    final Duration ttl = Duration.ofMillis(clusterConnection.sync().pttl(key));

    // Even though we can control our own clock for tests, we can't control Redis' clock. That means that some time will
    // elapse between setting the expiration for a key and reading its TTL; we compensate by allowing some amount of
    // clock drift. Under Redis 7 or newer, we could use a combination of `PEXPIREAT` and `PEXPIRETIME`
    // (https://redis.io/commands/pexpiretime/) to avoid clock drift concerns.
    assertTrue(ttl.minus(expectedTtl).abs().compareTo(MAXIMUM_CLOCK_DRIFT) <= 0);
  }

  @Test
  void validateZeroSize() throws Exception {
    // Override bucket size to zero
    conf.setBucketSize(0);

    RedisLeakyBucketRateLimiter limiter = new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(), clock);
    String aci = UUID.randomUUID().toString();

    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, 1).join());
    RateLimitExceededException rateLimitExceededException = (RateLimitExceededException) ce.getCause();

    assertEquals(conf.getLeakRateDuration().dividedBy(conf.getLeakRateScalar()), rateLimitExceededException.getRetryDuration());
  }

  @ParameterizedTest
  @ValueSource(ints = {30, 100, 1000})
  void validateOverflowThrows(int size) throws Exception {
    RedisLeakyBucketRateLimiter limiter = new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(), clock);
    String aci = UUID.randomUUID().toString();

    limiter.validate(aci, DEFAULT_BUCKET_SIZE).join(); // Exactly full, should not throw.
    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, size).join());
    RateLimitExceededException rateLimitException = (RateLimitExceededException) ce.getCause();

    assertEquals(conf.getLeakRateDuration().multipliedBy(size / conf.getLeakRateScalar()), rateLimitException.getRetryDuration());
  }

  @Test
  void validateNoNegativeSize() throws Exception {
    RedisLeakyBucketRateLimiter limiter = new RedisLeakyBucketRateLimiter(clusterConnection, conf, new SimpleMeterRegistry(), clock);
    String aci = UUID.randomUUID().toString();

    limiter.validate(aci, DEFAULT_BUCKET_SIZE).join(); // Exactly full, should not throw.

    // Wait long to make sure we're not calculating a negative bucket size.
    when(clock.instant()).thenReturn(CURRENT_TIME.plus(conf.getLeakRateDuration().multipliedBy(DEFAULT_BUCKET_SIZE * 2 / conf.getLeakRateScalar()))); // amount = 0
    limiter.validate(aci, DEFAULT_BUCKET_SIZE).join(); // amount == 100, should be full if we didn't incorrectly go negative.
    CompletionException ce = assertThrows(CompletionException.class, () -> limiter.validate(aci, 1).join());
    RateLimitExceededException rateLimitExceededException = (RateLimitExceededException) ce.getCause();

    assertEquals(conf.getLeakRateDuration().dividedBy(conf.getLeakRateScalar()), rateLimitExceededException.getRetryDuration());
  }
}
