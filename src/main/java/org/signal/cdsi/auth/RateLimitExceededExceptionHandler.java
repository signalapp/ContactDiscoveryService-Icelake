/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.auth;

import io.micronaut.context.annotation.Primary;
import io.micronaut.context.event.ApplicationEventPublisher;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.HttpStatus;
import io.micronaut.http.MutableHttpResponse;
import io.micronaut.http.annotation.Produces;
import io.micronaut.http.hateoas.JsonError;
import io.micronaut.http.hateoas.Link;
import io.micronaut.http.server.exceptions.ExceptionHandler;
import jakarta.inject.Singleton;
import java.util.Map;
import org.signal.cdsi.limits.RateLimitExceededException;

@Singleton
@Primary
@Produces
public class RateLimitExceededExceptionHandler implements ExceptionHandler<RateLimitExceededException, MutableHttpResponse<?>> {
  protected final ApplicationEventPublisher<?> eventPublisher;

  public RateLimitExceededExceptionHandler(ApplicationEventPublisher<?> eventPublisher) {
    this.eventPublisher = eventPublisher;
  }

  @Override
  public MutableHttpResponse<?> handle(HttpRequest request, RateLimitExceededException exception) {
    JsonError error = new JsonError(exception.getMessage());
    error.link(Link.SELF, Link.of(request.getUri()));
    return HttpResponse.status(HttpStatus.TOO_MANY_REQUESTS)
        .headers(Map.of("Retry-After", Long.toString(exception.getRetryDuration().toSeconds())))
        .body(error);
  }
}
