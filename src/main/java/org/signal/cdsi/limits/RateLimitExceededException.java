/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */
package org.signal.cdsi.limits;

import java.time.Duration;

public class RateLimitExceededException extends Exception {

  private final Duration retryDuration;

  public RateLimitExceededException(final Duration retryDuration) {
    super(null, null, true, false);
    this.retryDuration = retryDuration;
  }

  public Duration getRetryDuration() { return retryDuration; }

}
