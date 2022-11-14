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

  private Optional<Duration> validateRetryAfter = Optional.empty();
  private Optional<Duration> prepareRetryAfter = Optional.empty();
  private Map<TokenKey, Integer> tokenStorage = new HashMap<>();

  private record TokenKey(String key, ByteBuffer tokenHash){}

  @Override
  public CompletableFuture<Void> prepare(final String key, final int amount, final ByteBuffer oldTokenHash,
      final ByteBuffer newTokenHash) {
    return prepareRetryAfter
        .map(RateLimitExceededException::new)
        .map(CompletableFuture::<Void>failedFuture)
        .orElseGet(() -> {
          synchronized (this) {
            int newAmount = amount + tokenStorage.getOrDefault(new TokenKey(key, oldTokenHash.asReadOnlyBuffer()), 0);
            tokenStorage.put(new TokenKey(key, newTokenHash), newAmount);
          }
          return CompletableFuture.completedFuture(null);
        });
  }

  @Override
  public CompletableFuture<Integer> validate(final String key, final ByteBuffer tokenHash) {
    return validateRetryAfter
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

  public void setPrepareRetryAfter(Optional<Duration> retryAfter) {
    this.prepareRetryAfter = retryAfter;
  }

  public void setValidateRetryAfter(Optional<Duration> retryAfter) {
    this.validateRetryAfter = retryAfter;
  }

  public void reset() {
    this.prepareRetryAfter = Optional.empty();
    this.validateRetryAfter = Optional.empty();
  }
}
