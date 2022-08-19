/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.auth;

import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Replaces;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.MutableHttpRequest;
import jakarta.inject.Singleton;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneId;
import org.signal.cdsi.ClockFactory;

@Factory
@Replaces(factory = ClockFactory.class)
public class AuthenticationHelper {
  @Singleton
  Clock clock() {
    return Clock.fixed(Instant.ofEpochSecond(1633738643L), ZoneId.of("Etc/UTC"));
  }

  public static final String USERNAME = "EREREREREREREREREREREQAAAABZvPKn";
  public static final String PASSWORD = "1633738643:dd04a14141680e7d43b9";
  public static final MutableHttpRequest<?> HTTP_REQUEST = HttpRequest
      .GET("/v1/test/discovery")
      .basicAuth(USERNAME, PASSWORD);
}
