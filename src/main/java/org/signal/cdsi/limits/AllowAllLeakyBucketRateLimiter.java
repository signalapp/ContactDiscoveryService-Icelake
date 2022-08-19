/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.micronaut.context.annotation.EachBean;
import io.micronaut.context.annotation.Requires;
import jakarta.inject.Singleton;
import java.util.concurrent.CompletableFuture;

@Singleton
@Requires(env = "dev")
@Requires(missingBeans = StatefulRedisClusterConnection.class)
@EachBean(LeakyBucketRateLimitConfiguration.class)
public class AllowAllLeakyBucketRateLimiter implements LeakyBucketRateLimiter {

  private final LeakyBucketRateLimitConfiguration configuration;

  public AllowAllLeakyBucketRateLimiter(final LeakyBucketRateLimitConfiguration configuration) {
    this.configuration = configuration;
  }

  @Override
  public CompletableFuture<Void> validate(final String key, final int amount) {
    return CompletableFuture.completedFuture(null);
  }
}
