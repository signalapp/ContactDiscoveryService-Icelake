/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */
package org.signal.cdsi.limits;

import java.nio.ByteBuffer;
import java.util.concurrent.CompletableFuture;

/**
 * Provides rate limiting for requests based on opaque tokens returned by the enclave
 * <p>
 * Broadly speaking,
 * <ol>
 * <li> A client requests a new token </li>
 * <li> The server computes the cost of the new token and persists it for future use with
 *      {@link #prepare(String, int, ByteBuffer, ByteBuffer)} </li>
 * <li> The token is returned to the client. This ensures that the client has the token before any rates
 *      have been depleted. </li>
 * <li> The client uses the token, the server transactionally depletes the amount used and removes the stored token
 *      with {@link #validate(String, ByteBuffer)}
 * <li> All subsequent uses of the token are free </li>
 * </ol>
 */
public interface TokenRateLimiter {

  CompletableFuture<Void> prepare(String key, int amount, final ByteBuffer oldTokenHash, final ByteBuffer newTokenHash);

  /**
   * Use the permits for the provided token, or fail if not enough permits are available
   *
   * @param key       identifier for the request
   * @param tokenHash the previously prepared token to use
   * @return A future that on success completes with number of permits used, otherwise throws {@link
   * RateLimitExceededException}
   */
  CompletableFuture<Integer> validate(String key, final ByteBuffer tokenHash);
}
