/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.redis;

import io.lettuce.core.ClientOptions.DisconnectedBehavior;
import io.lettuce.core.RedisURI;
import io.lettuce.core.cluster.ClusterClientOptions;
import io.lettuce.core.cluster.ClusterTopologyRefreshOptions;
import io.lettuce.core.cluster.RedisClusterClient;
import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.lettuce.core.metrics.MicrometerCommandLatencyRecorder;
import io.lettuce.core.metrics.MicrometerOptions;
import io.lettuce.core.resource.ClientResources;
import io.lettuce.core.resource.DefaultClientResources;
import io.micrometer.core.instrument.Metrics;
import io.micronaut.context.annotation.Bean;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Property;
import io.micronaut.context.annotation.Requires;
import jakarta.inject.Named;
import jakarta.inject.Singleton;
import java.net.URI;
import java.util.List;

@Requires(property = "redisLeakyBucket.urls")
@Factory
class LeakyBucketRedisClientFactory {

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
  RedisClusterClient redisClusterClient(@Property(name = "redisLeakyBucket.urls") final List<URI> uris) {
    if (uris.isEmpty()) {
      throw new IllegalArgumentException("Redis URLs most not be empty");
    }
    final List<RedisURI> redisURIs = uris.stream().map(RedisURI::create).toList();

    final MicrometerOptions options = MicrometerOptions.builder()
        .histogram(true).build();
    final ClientResources clientResources = DefaultClientResources.builder()
        .commandLatencyRecorder(new MicrometerCommandLatencyRecorder(Metrics.globalRegistry, options))
        .build();

    final RedisClusterClient redisClusterClient = RedisClusterClient.create(clientResources, redisURIs);

    redisClusterClient.setOptions(ClusterClientOptions.builder()
        .disconnectedBehavior(DisconnectedBehavior.REJECT_COMMANDS)
        .validateClusterNodeMembership(false)
        .topologyRefreshOptions(ClusterTopologyRefreshOptions.builder()
            .enableAllAdaptiveRefreshTriggers()
            .build())
        .build());

    return redisClusterClient;
  }
}
