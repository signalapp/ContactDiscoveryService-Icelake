/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import jakarta.validation.constraints.PositiveOrZero;
import java.time.Duration;

public abstract class RateLimitConfiguration {

  @PositiveOrZero
  private int bucketSize;

  @PositiveOrZero
  private int leakRateScalar;

  private Duration leakRateDuration;

  public int getBucketSize() {
    return bucketSize;
  }

  public void setBucketSize(int bucketSize) {
    this.bucketSize = bucketSize;
  }

  public int getLeakRateScalar() {
    return leakRateScalar;
  }

  public void setLeakRateScalar(final int leakRateScalar) {
    this.leakRateScalar = leakRateScalar;
  }

  public Duration getLeakRateDuration() {
    return leakRateDuration;
  }

  public void setLeakRateDuration(final Duration leakRateDuration) {
    this.leakRateDuration = leakRateDuration;
  }
}
