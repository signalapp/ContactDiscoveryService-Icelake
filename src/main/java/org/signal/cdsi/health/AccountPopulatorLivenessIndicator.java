package org.signal.cdsi.health;

import io.micronaut.context.annotation.Context;
import io.micronaut.core.async.publisher.Publishers;
import io.micronaut.health.HealthStatus;
import io.micronaut.management.health.indicator.HealthIndicator;
import io.micronaut.management.health.indicator.HealthResult;
import io.micronaut.management.health.indicator.annotation.Liveness;
import org.reactivestreams.Publisher;
import org.signal.cdsi.account.AccountPopulator;

@Context
@Liveness
public class AccountPopulatorLivenessIndicator implements HealthIndicator {

  private final AccountPopulator accountPopulator;

  public AccountPopulatorLivenessIndicator(final AccountPopulator accountPopulator) {
    this.accountPopulator = accountPopulator;
  }

  @Override
  public Publisher<HealthResult> getResult() {
    return Publishers.just(
        HealthResult.builder(
                "accountPopulatorHealthy",
                accountPopulator.isHealthy() ? HealthStatus.UP : HealthStatus.DOWN)
            .build());
  }
}
