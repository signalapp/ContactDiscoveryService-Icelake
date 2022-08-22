/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.metrics;

import io.micrometer.core.instrument.Metrics;
import io.micrometer.core.instrument.composite.CompositeMeterRegistry;
import io.micronaut.configuration.metrics.aggregator.MeterRegistryConfigurer;
import jakarta.inject.Singleton;

@Singleton
class GlobalMeterRegistryConfigurer implements MeterRegistryConfigurer<CompositeMeterRegistry> {

  /**
   * Adds the primary meter registry to the global meter registry, enabling convenience methods, like {@link
   * Metrics#counter(String, String...)}, to work.
   */
  @Override
  public void configure(final CompositeMeterRegistry meterRegistry) {
    Metrics.addRegistry(meterRegistry);
  }

  @Override
  public Class<CompositeMeterRegistry> getType() {
    return CompositeMeterRegistry.class;
  }
}
