/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import com.google.common.base.Preconditions;
import java.nio.ByteBuffer;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicBoolean;
import org.signal.cdsi.limits.TokenRateLimiter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class EnclaveClient {
  public enum State {
    UNINITIALIZED,
    ATTESTED,
    RATELIMIT,
    COMPLETE,
  }

  private final Enclave enclave;
  private final long id;
  private final ByteBuffer ereport;
  private final String rateLimitKey;
  private final TokenRateLimiter tokenRateLimiter;
  private int requestSize;
  private State state = State.UNINITIALIZED;
  private ByteBuffer newTokenHash = null;
  private final AtomicBoolean closed;
  private CompletableFuture<Void> closedFuture;

  private static final Logger logger = LoggerFactory.getLogger(EnclaveClient.class);

  EnclaveClient(
      final Enclave enclave,
      final long id,
      String rateLimitKey,
      TokenRateLimiter tokenRateLimiter,
      ByteBuffer ereport) {
    this.enclave = enclave;
    this.id = id;
    this.rateLimitKey = rateLimitKey;
    this.ereport = ereport;
    this.tokenRateLimiter = tokenRateLimiter;
    this.closed = new AtomicBoolean();
  }

  long getId() {
    return id;
  }

  public State getState() {
    return this.state;
  }

  public ByteBuffer getEreport() {
    return ereport;
  }

  String getRateLimitKey() {
    return rateLimitKey;
  }

  public CompletableFuture<ByteBuffer> handshake(ByteBuffer in) {
    Preconditions.checkState(!closed.get());
    Preconditions.checkState(state == State.UNINITIALIZED);
    state = State.ATTESTED;

    return enclave.clientHandshake(this, in);
  }

  public CompletableFuture<ByteBuffer> rateLimit(final ByteBuffer request) {
    Preconditions.checkState(!closed.get());
    Preconditions.checkState(state == State.ATTESTED);
    Preconditions.checkState(newTokenHash == null);

    state = State.RATELIMIT;
    requestSize = request.remaining();
    newTokenHash = ByteBuffer.allocateDirect(32);

    return enclave.clientRateLimit(this, request, newTokenHash);
  }

  public CompletableFuture<ByteBuffer> complete(ByteBuffer ack) {
    Preconditions.checkState(!closed.get());
    Preconditions.checkState(state == State.RATELIMIT);
    Preconditions.checkState(newTokenHash != null);

    state = State.COMPLETE;
    // Given a request of size X, we're unsure what's in that request (it's encrypted), so
    // we assume it's the request that gives us the largest response possible.  The request
    // that gives us the largest possible response is one that's entirely filled with e164s,
    // with no ACI/UAK pairs.  For such a request, each 8 bytes of input (a single e164)
    // returns 40 bytes of output (an 8-byte e164, a 16-byte ACI, and a 16-byte PNI).  This
    // is a 5x multiplier (output=input*5).  There's also the potential that a few other singular
    // fields may be added to the proto, so add in a bit of slop (128 bytes).
    final ByteBuffer out = ByteBuffer.allocateDirect(requestSize * 5 + 128);

    return tokenRateLimiter.validate(rateLimitKey, newTokenHash)
        .thenCompose(permitsUsed -> enclave.clientRun(this, permitsUsed, ack, out));
  }

  /** Closes (asynchronously) the underlying resources utilized by this client.
   *
   * The closeAsync method _must_ be called by the user of an EnclaveClient, and it must be called
   * only after all other async (CompletableFuture) methods have finished.  It is an error to, for example:
   *
   *   var a = client.handshake(...);
   *   client.closeAsync();
   *
   * Instead, the correct use would be:
   *
   *   client.handshake(...).whenComplete((...) -> client.closeAsync());
   *
   * It is also erroneous to call any other async methods of this client after closeAsync().  closeAsync() may
   * be called any number of times, safely.  Only the first such call will actually close the client, and
   * all such calls will return the same future.  Other async calls made subsequent to closeAsync() will
   * throw an IllegalStateException due to Precondition checks.
   *
   * @return CompletableFuture that finishes when this client has been closed.
   */
  public synchronized CompletableFuture<Void> closeAsync() {
    if (!closed.getAndSet(true)) {
      Preconditions.checkState(closedFuture == null);
      closedFuture = enclave.closeClient(id);
    }
    return closedFuture;
  }
}
