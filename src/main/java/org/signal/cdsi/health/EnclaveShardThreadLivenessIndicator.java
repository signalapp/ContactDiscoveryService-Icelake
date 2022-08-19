/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.health;

import io.micronaut.core.async.publisher.Publishers;
import io.micronaut.health.HealthStatus;
import io.micronaut.management.health.indicator.HealthIndicator;
import io.micronaut.management.health.indicator.HealthResult;
import io.micronaut.management.health.indicator.annotation.Liveness;
import jakarta.inject.Singleton;
import org.reactivestreams.Publisher;
import org.signal.cdsi.enclave.Enclave;
import org.signal.cdsi.enclave.EnclaveConfiguration;
import reactor.core.publisher.Mono;

@Singleton
@Liveness
public class EnclaveShardThreadLivenessIndicator implements HealthIndicator {

  private final Enclave enclave;
  private final int expectedShardThreadCount;

  public EnclaveShardThreadLivenessIndicator(final Enclave enclave, final EnclaveConfiguration enclaveConfiguration) {
    this.enclave = enclave;
    this.expectedShardThreadCount = enclaveConfiguration.getShards();
  }

  @Override
  public Publisher<HealthResult> getResult() {
    return Mono.just(HealthResult.builder("allEnclaveShardsRunning")
        .status(enclave.getRunningShardThreadCount() == expectedShardThreadCount ? HealthStatus.UP : HealthStatus.DOWN)
        .build());
  }
}
