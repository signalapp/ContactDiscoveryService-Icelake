/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.health;

import io.micronaut.context.annotation.Context;
import io.micronaut.core.async.publisher.Publishers;
import io.micronaut.health.HealthStatus;
import io.micronaut.management.health.indicator.HealthIndicator;
import io.micronaut.management.health.indicator.HealthResult;
import io.micronaut.management.health.indicator.annotation.Liveness;
import org.reactivestreams.Publisher;
import org.signal.cdsi.enclave.Enclave;

@Context
@Liveness
public class InitialAttestationLivenessIndicator implements HealthIndicator {

  private final Enclave enclave;

  public InitialAttestationLivenessIndicator(final Enclave enclave) {
    this.enclave = enclave;
  }

  @Override
  public Publisher<HealthResult> getResult() {
    return Publishers.just(
        HealthResult.builder(
                "initialAttestationComplete",
                enclave.getLastAttestationTimestamp().isPresent() ? HealthStatus.UP : HealthStatus.DOWN)
            .build());
  }
}
