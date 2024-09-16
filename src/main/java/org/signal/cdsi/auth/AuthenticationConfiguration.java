/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.auth;

import io.micronaut.context.annotation.ConfigurationProperties;
import io.micronaut.context.annotation.Context;
import java.time.Duration;
import java.util.Base64;
import jakarta.validation.constraints.NotNull;
import jakarta.validation.constraints.Size;

@Context
@ConfigurationProperties("authentication")
public class AuthenticationConfiguration {

  @Size(min = 32, max = 32)
  @NotNull
  private byte[] sharedSecret;

  private Duration tokenExpiration = Duration.ofDays(1);

  public byte[] getSharedSecret() {
    return sharedSecret;
  }

  public void setSharedSecret(final String sharedSecret) {
    this.sharedSecret = Base64.getDecoder().decode(sharedSecret);
  }

  public Duration getTokenExpiration() {
    return tokenExpiration;
  }

  public void setTokenExpiration(final Duration tokenExpiration) {
    this.tokenExpiration = tokenExpiration;
  }
}
