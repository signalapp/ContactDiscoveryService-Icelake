/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import java.nio.ByteBuffer;
import java.util.concurrent.CompletableFuture;

public interface LeakyBucketRateLimiter {
  CompletableFuture<Void> validate(String key, int amount);
}
