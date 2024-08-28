/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import io.micrometer.core.instrument.MeterRegistry;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Replaces;
import io.micronaut.context.annotation.Requires;
import jakarta.inject.Named;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.signal.cdsi.limits.TokenRateLimiter;
import java.io.IOException;
import java.time.Clock;
import java.util.concurrent.ExecutorService;

@Context
@Requires(env = "test")
@Replaces(Enclave.class)
@EnabledOnOs(OS.LINUX)
public class TestEnclave extends Enclave {

  private boolean overloaded = false;

  public TestEnclave(final EnclaveConfiguration enclaveConfiguration,
      final TokenRateLimiter tokenRateLimiter,
      final MeterRegistry meterRegistry,
      @Named(Enclave.JNI_EXECUTOR_NAME) final ExecutorService jniExecutor,
      final Clock clock) throws IOException, EnclaveException {

    super(enclaveConfiguration, tokenRateLimiter, meterRegistry, jniExecutor, clock);
  }

  public void setOverloaded(final boolean overloaded) {
    this.overloaded = overloaded;
  }

  @Override
  public boolean isOverloaded() {
    return overloaded;
  }
}
