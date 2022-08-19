/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;


import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.EachProperty;
import io.micronaut.context.annotation.Parameter;
import javax.validation.constraints.NotBlank;

@EachProperty("leakyBucketLimit")
@Context
public class LeakyBucketRateLimitConfiguration extends RateLimitConfiguration {

  @NotBlank
  private final String name;

  public LeakyBucketRateLimitConfiguration(@Parameter String name) {
    this.name = name;
  }

  public String getName() {
    return name;
  }
}
