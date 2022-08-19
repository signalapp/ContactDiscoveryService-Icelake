/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import jakarta.inject.Singleton;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;

@Singleton
public class ManualTokenRateLimiter implements TokenRateLimiter {

  private Optional<Duration> retryAfter = Optional.empty();
  private Map<TokenKey, Integer> tokenStorage = new HashMap<>();

  private record TokenKey(String key, ByteBuffer tokenHash){}

  @Override
  public CompletableFuture<Void> prepare(final String key, int amount, final ByteBuffer oldTokenHash,
      final ByteBuffer newTokenHash) {
    synchronized (this) {
      amount += tokenStorage.getOrDefault(new TokenKey(key, oldTokenHash.asReadOnlyBuffer()), 0);
      tokenStorage.put(new TokenKey(key, newTokenHash), amount);
    }
    return CompletableFuture.completedFuture(null);
  }

  @Override
  public CompletableFuture<Integer> validate(final String key, final ByteBuffer tokenHash) {
    return retryAfter
        .map(RateLimitExceededException::new)
        .map(CompletableFuture::<Integer>failedFuture)
        .orElseGet(() -> {
          synchronized (this) {
            return Optional
                .ofNullable(tokenStorage.remove(new TokenKey(key, tokenHash)))
                .map(CompletableFuture::completedFuture)
                .orElse(CompletableFuture.completedFuture(0));
          }
        });
  }

  public void setRetryAfter(Optional<Duration> retryAfter) {
    this.retryAfter = retryAfter;
  }
}
