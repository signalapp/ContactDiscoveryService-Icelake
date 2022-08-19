/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import io.micronaut.context.annotation.Requires;
import jakarta.inject.Singleton;
import java.nio.ByteBuffer;
import java.util.concurrent.CompletableFuture;

@Singleton
@Requires(missingBeans = TokenRateLimiter.class)
@Requires(env = "dev")
public class AllowAllTokenRateLimiter implements TokenRateLimiter {

  @Override
  public CompletableFuture<Void> prepare(final String key, final int amount, final ByteBuffer oldTokenHash,
      final ByteBuffer newTokenHash) {

    return CompletableFuture.completedFuture(null);
  }

  @Override
  public CompletableFuture<Integer> validate(final String key, final ByteBuffer tokenHash) {
    return CompletableFuture.completedFuture(0);
  }
}
