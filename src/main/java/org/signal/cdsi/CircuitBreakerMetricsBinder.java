/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi;

import io.github.resilience4j.circuitbreaker.CircuitBreakerRegistry;
import io.github.resilience4j.micrometer.tagged.TaggedCircuitBreakerMetrics;
import io.micrometer.core.instrument.MeterRegistry;
import io.micronaut.context.annotation.Context;

@Context
public class CircuitBreakerMetricsBinder {

  public CircuitBreakerMetricsBinder(final MeterRegistry meterRegistry, final CircuitBreakerRegistry circuitBreakerRegistry) {
    TaggedCircuitBreakerMetrics
        .ofCircuitBreakerRegistry(circuitBreakerRegistry)
        .bindTo(meterRegistry);
  }
}
