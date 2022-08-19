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
import io.micronaut.management.health.indicator.annotation.Readiness;
import org.reactivestreams.Publisher;
import org.signal.cdsi.account.AccountPopulator;

@Context
@Readiness
public class AccountsPopulatedReadinessIndicator implements HealthIndicator {

  private final AccountPopulator accountPopulator;

  public AccountsPopulatedReadinessIndicator(final AccountPopulator accountPopulator) {
    this.accountPopulator = accountPopulator;
  }

  @Override
  public Publisher<HealthResult> getResult() {
    return Publishers.just(
        HealthResult.builder(
                "accountsPopulated",
                accountPopulator.hasFinishedInitialAccountPopulation() ? HealthStatus.UP : HealthStatus.DOWN)
            .build());
  }
}
