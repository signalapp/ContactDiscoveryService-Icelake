/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.redis;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import io.lettuce.core.RedisException;
import io.lettuce.core.ScriptOutputType;
import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.lettuce.core.cluster.api.async.RedisAdvancedClusterAsyncCommands;
import io.lettuce.core.codec.StringCodec;
import io.lettuce.core.output.CommandOutput;
import io.lettuce.core.protocol.AsyncCommand;
import io.lettuce.core.protocol.Command;
import io.lettuce.core.protocol.CommandType;
import io.lettuce.core.protocol.RedisCommand;
import io.micronaut.context.annotation.Property;
import io.micronaut.test.annotation.MockBean;
import io.micronaut.test.extensions.junit5.annotation.MicronautTest;
import jakarta.inject.Inject;
import jakarta.inject.Named;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CompletionException;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.signal.cdsi.limits.RateLimitExceededException;

// This tests the @CircuitBreaker, since it is only enabled through Micronaut
@MicronautTest
@Property(name = "redis-leaky-bucket.circuit-breaker.attempts", value = RedisLeakyBucketRateLimiterIntegrationTest.RETRIES)
@Property(name = "redis-leaky-bucket.circuit-breaker.delay", value = "2ms")
@Property(name = "redis-leaky-bucket.circuit-breaker.reset", value = "1s")
public class RedisLeakyBucketRateLimiterIntegrationTest {
  static final String RETRIES = "2";
  private RedisCommand<String, String, Object> noOverflow;
  private RedisCommand<String, String, Object> overflow;

  @SuppressWarnings("unchecked")
  @MockBean
  @Named(LeakyBucketRedisClientFactory.CONNECTION_NAME)
  StatefulRedisClusterConnection<String, String> mockConnection = mock(StatefulRedisClusterConnection.class);

  @Inject
  private RedisLeakyBucketRateLimiter limiter;

  private RedisAdvancedClusterAsyncCommands<String, String> asyncCommands;

  @SuppressWarnings("unchecked")
  @BeforeEach
  void setup() throws InterruptedException {
    asyncCommands = mock(RedisAdvancedClusterAsyncCommands.class);
    when(mockConnection.async()).thenReturn(asyncCommands);

    noOverflow = new Command<>(CommandType.EVALSHA, new CommandOutput<>(StringCodec.UTF8, 0L) {});
    overflow = new Command<>(CommandType.EVALSHA, new CommandOutput<>(StringCodec.UTF8, 1L) {});

    // Terrible hack: wait for the breaker to reset between test runs
    Thread.sleep(2_000);
  }

  @Test
  void testSuccess() {
    final AsyncCommand<String, String, Object> asyncCommand = new AsyncCommand<>(noOverflow);
    asyncCommand.complete();
    when(asyncCommands.evalsha(anyString(), eq(ScriptOutputType.INTEGER), any(String[].class), any(String[].class)))
        .thenReturn(asyncCommand);

    String aci = UUID.randomUUID().toString();

    assertTimeoutPreemptively(Duration.ofSeconds(1), () -> {
      limiter.validate(aci, 1).join();
    });

    verify(mockConnection, times(1)).async();
  }

  @Test
  void testRateLimitExceeded() {
    final AsyncCommand<String, String, Object> asyncCommand = new AsyncCommand<>(overflow);
    asyncCommand.complete();
    when(asyncCommands.evalsha(anyString(), eq(ScriptOutputType.INTEGER), any(String[].class), any(String[].class)))
        .thenReturn(asyncCommand);

    String aci = UUID.randomUUID().toString();

    assertTimeoutPreemptively(Duration.ofSeconds(1), () -> {
      final CompletionException e = assertThrows(CompletionException.class, () -> limiter.validate(aci, 1).join());
      assertEquals(RateLimitExceededException.class, e.getCause().getClass());
    });

    verify(mockConnection, times(1)).async();
  }

  @Test
  void testRedisClusterUnavailable() {
    final AsyncCommand<String, String, Object> asyncCommand = new AsyncCommand<>(noOverflow);
    asyncCommand.completeExceptionally(new RedisException("Unavailable"));
    when(this.asyncCommands.evalsha(anyString(), eq(ScriptOutputType.INTEGER), any(String[].class), any(String[].class)))
        .thenReturn(asyncCommand);

    String aci = UUID.randomUUID().toString();

    assertTimeoutPreemptively(Duration.ofSeconds(1), () -> {
      limiter.validate(aci, 1).join();
    });

    // We expect one "original" call, then RETRIES follow-up calls
    verify(mockConnection, times(Integer.parseInt(RETRIES) + 1)).async();
  }

}
