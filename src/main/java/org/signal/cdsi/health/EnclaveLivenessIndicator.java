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
import org.signal.cdsi.enclave.EnclaveClient;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Liveness
class EnclaveLivenessIndicator implements HealthIndicator {
  private static final Logger logger = LoggerFactory.getLogger(EnclaveLivenessIndicator.class);
  private static final String NAME = "EnclaveClientCreation";

  private final Enclave enclave;

  EnclaveLivenessIndicator(Enclave enclave) {
    this.enclave = enclave;
  }

  @Override
  public Publisher<HealthResult> getResult() {
    logger.trace("creating client for liveness");

    return Publishers.fromCompletableFuture(
        enclave.newClient("UNUSED_LIVENESS_RATELIMIT_KEY")
            .thenCompose(EnclaveClient::closeAsync)
            .thenApply(v -> {
              logger.trace("successfully created client for liveness");
              return HealthResult.builder(NAME, HealthStatus.UP).build();
            })
            .exceptionally(err -> {
              logger.error("failure to create enclave client for liveness", err);
              return HealthResult.builder(NAME, HealthStatus.DOWN).build();
            }));
  }
}
