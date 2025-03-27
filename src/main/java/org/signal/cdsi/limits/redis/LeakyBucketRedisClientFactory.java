/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.redis;

import io.lettuce.core.ClientOptions.DisconnectedBehavior;
import io.lettuce.core.RedisURI;
import io.lettuce.core.RedisURI.Builder;
import io.lettuce.core.TimeoutOptions;
import io.lettuce.core.cluster.ClusterClientOptions;
import io.lettuce.core.cluster.ClusterTopologyRefreshOptions;
import io.lettuce.core.cluster.RedisClusterClient;
import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.lettuce.core.metrics.MicrometerOptions;
import io.lettuce.core.resource.ClientResources;
import io.lettuce.core.resource.DefaultClientResources;
import io.micronaut.context.annotation.Bean;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Property;
import io.micronaut.context.annotation.Requires;
import jakarta.inject.Named;
import jakarta.inject.Singleton;
import java.net.URI;
import java.time.Duration;
import java.util.List;

@Requires(property = LeakyBucketRedisClientFactory.HOST)
@Factory
class LeakyBucketRedisClientFactory {

  static final String HOST = "redis-leaky-bucket.host";
  static final String PASSWORD = "redis-leaky-bucket.password";
  private static final String CLIENT_NAME = "redisLeakyBucketClient";
  public static final String CONNECTION_NAME = "redisLeakyBucketConnection";

  @Named(CONNECTION_NAME)
  @Bean(preDestroy = "close")
  @Singleton
  StatefulRedisClusterConnection<String, String> redisConnection(@Named(CLIENT_NAME) RedisClusterClient redisClient) {
    return redisClient.connect();
  }

  @Named(CLIENT_NAME)
  @Bean(preDestroy = "shutdown")
  @Singleton
  RedisClusterClient redisClusterClient(
      @Property(name = HOST) final String host,
      @Property(name = PASSWORD) final String password) {
    if (host.isBlank()) {
      throw new IllegalArgumentException("redis host must be specified");
    }

    final Builder redisUriBuilder = RedisURI.builder().withHost(host);
    if (password != null && !password.isBlank()) {
      redisUriBuilder.withPassword(password);
    }
    final RedisURI redisUri = redisUriBuilder.build();

    final MicrometerOptions options = MicrometerOptions.builder()
        .histogram(true).build();
    final ClientResources clientResources = DefaultClientResources.builder().build();

    final RedisClusterClient redisClusterClient = RedisClusterClient.create(clientResources, redisUri);

    redisClusterClient.setOptions(ClusterClientOptions.builder()
        .timeoutOptions(TimeoutOptions.builder()
            .timeoutCommands(true)
            .fixedTimeout(Duration.ofMillis(500))
            .build())
        .disconnectedBehavior(DisconnectedBehavior.REJECT_COMMANDS)
        .validateClusterNodeMembership(false)
        .topologyRefreshOptions(ClusterTopologyRefreshOptions.builder()
            .enableAllAdaptiveRefreshTriggers()
            .build())
        .build());

    return redisClusterClient;
  }
}
