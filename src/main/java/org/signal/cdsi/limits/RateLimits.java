/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import java.time.Duration;
import java.time.Instant;

public class RateLimits {

  /**
   * Given a leaky bucket and the amount of time that has passed,
   * compute the new bucket value after leaking and adding delta permits
   *
   * @return the new amount in the bucket
   * @throws RateLimitExceededException if the bucket reaches its capacity
   */
  public static double calculateBucketUtilization(
      RateLimitConfiguration configuration,
      Instant lastUpdated,
      Instant now,
      double priorSize,
      double delta)
      throws RateLimitExceededException {
    double since = Duration.between(lastUpdated, now).toSeconds();
    double currentAmount = Math.max(0, priorSize - since / configuration.getLeakRateDuration().toSeconds() * configuration.getLeakRateScalar());
    double newAmount = currentAmount + delta;
    checkOverage(configuration, newAmount);
    return newAmount;
  }

  public static void checkOverage(RateLimitConfiguration configuration, double proposedAmount) throws RateLimitExceededException {
    double overage = proposedAmount - configuration.getBucketSize();
    if (overage > 0) {
      Duration retryDuration = Duration.ofSeconds(
          1 + (long) (overage / configuration.getLeakRateScalar() * configuration.getLeakRateDuration()
              .toSeconds()));
      throw new RateLimitExceededException(retryDuration);
    }
  }

}
