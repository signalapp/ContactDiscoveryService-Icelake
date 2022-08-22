/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.metrics;

import io.micrometer.core.instrument.Meter.Id;
import io.micrometer.core.instrument.config.MeterFilter;
import io.micrometer.core.instrument.distribution.DistributionStatisticConfig;
import jakarta.inject.Singleton;


@Singleton
class DistributionStatisticsConfigMeterFilter implements MeterFilter {

  private static final DistributionStatisticConfig defaultDistributionStatisticConfig = DistributionStatisticConfig.builder()
      .percentiles(.5, .75, .95, .99, .999)
      .build();

  @Override
  public DistributionStatisticConfig configure(final Id id, final DistributionStatisticConfig config) {
    return defaultDistributionStatisticConfig.merge(config);
  }
}
