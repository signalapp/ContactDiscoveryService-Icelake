/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.redis;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.atLeast;
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
import io.micronaut.context.annotation.Replaces;
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

// This tests the @Retry and @CircuitBreaker, since those are only enabled through Micronaut
@MicronautTest
@Property(name = "resilience4j.retry.instances.redis.max-attempts", value = "2")
@Property(name = "resilience4j.retry.instances.redis.wait-duration", value = "PT0.001S")
public class RedisLeakyBucketRateLimiterIntegrationTest {

  private RedisCommand<String, String, Object> noOverflow;
  private RedisCommand<String, String, Object> overflow;

  @MockBean
  @Named(LeakyBucketRedisClientFactory.CONNECTION_NAME)
  StatefulRedisClusterConnection<String, String> mockConnection = mock(StatefulRedisClusterConnection.class);

  @Inject
  private RedisLeakyBucketRateLimiter limiter;

  private RedisAdvancedClusterAsyncCommands<String, String> asyncCommands;

  @SuppressWarnings("unchecked")
  @BeforeEach
  void setup() {
    asyncCommands = mock(RedisAdvancedClusterAsyncCommands.class);
    when(mockConnection.async()).thenReturn(asyncCommands);

    noOverflow = new Command<>(CommandType.EVALSHA, new CommandOutput<>(StringCodec.UTF8, 0L) {});
    overflow = new Command<>(CommandType.EVALSHA, new CommandOutput<>(StringCodec.UTF8, 1L) {});
  }

  @Test
  void testSuccess() {
    final AsyncCommand<String, String, Object> asyncCommand = new AsyncCommand<>(noOverflow);
    asyncCommand.complete();
    when(asyncCommands.evalsha(any(), eq(ScriptOutputType.INTEGER), any(), any()))
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
    when(asyncCommands.evalsha(any(), eq(ScriptOutputType.INTEGER), any(), any()))
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
    when(this.asyncCommands.evalsha(any(), eq(ScriptOutputType.INTEGER), any(), any()))
        .thenReturn(asyncCommand);

    String aci = UUID.randomUUID().toString();

    assertTimeoutPreemptively(Duration.ofSeconds(1), () -> {
      limiter.validate(aci, 1).join();
    });

    verify(mockConnection, atLeast(2)).async();
  }

}
