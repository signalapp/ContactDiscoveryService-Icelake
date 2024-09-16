/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import io.micronaut.context.annotation.ConfigurationProperties;
import io.micronaut.context.annotation.Context;
import jakarta.validation.constraints.NotBlank;

@ConfigurationProperties("cosmos")
@Context
public class CosmosClientConfiguration {

  private @NotBlank String database;
  private @NotBlank String container;
  private @NotBlank String endpoint;
  private @NotBlank String key;

  public String getDatabase() {
    return database;
  }

  public void setDatabase(final String database) {
    this.database = database;
  }

  public String getContainer() {
    return container;
  }

  public void setContainer(final String container) {
    this.container = container;
  }

  public String getEndpoint() {
    return endpoint;
  }

  public void setEndpoint(final String endpoint) {
    this.endpoint = endpoint;
  }

  public String getKey() {
    return key;
  }

  public void setKey(final String key) {
    this.key = key;
  }
}
