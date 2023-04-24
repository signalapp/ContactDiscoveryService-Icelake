/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import io.micronaut.core.async.publisher.Publishers;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.HttpStatus;
import io.micronaut.http.MutableHttpResponse;
import io.micronaut.http.annotation.Filter;
import io.micronaut.http.filter.HttpServerFilter;
import io.micronaut.http.filter.ServerFilterChain;
import org.reactivestreams.Publisher;
import org.signal.cdsi.enclave.Enclave;

/**
 * Rejects new requests when the enclave is overloaded.
 */
@Filter("/v1/*/discovery")
public class LoadSheddingFilter implements HttpServerFilter {

  private final Enclave enclave;

  public LoadSheddingFilter(final Enclave enclave) {
    this.enclave = enclave;
  }

  @Override
  public Publisher<MutableHttpResponse<?>> doFilter(final HttpRequest<?> request, final ServerFilterChain chain) {
    return enclave.isOverloaded() ?
        Publishers.just(HttpResponse.status(HttpStatus.valueOf(508))) :
        chain.proceed(request);
  }
}
