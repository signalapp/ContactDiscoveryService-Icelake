/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import io.micronaut.context.annotation.ConfigurationProperties;

import javax.validation.constraints.NotBlank;
import javax.validation.constraints.Positive;

@ConfigurationProperties("accountTable")
class AccountTableConfiguration {

  @NotBlank
  private String region;

  @NotBlank
  private String tableName;

  @NotBlank
  private String streamName;

  @Positive
  private int tableReadSegments = 16;

  @Positive
  private int maxRetries = 128;

  public String getRegion() {
    return region;
  }

  public void setRegion(final String region) {
    this.region = region;
  }

  public String getTableName() {
    return tableName;
  }

  public void setTableName(final String tableName) {
    this.tableName = tableName;
  }

  public String getStreamName() {
    return streamName;
  }

  public void setStreamName(final String streamName) {
    this.streamName = streamName;
  }

  public int getTableReadSegments() {
    return tableReadSegments;
  }

  public void setTableReadSegments(final int tableReadSegments) {
    this.tableReadSegments = tableReadSegments;
  }

  public int getMaxRetries() {
    return maxRetries;
  }

  public void setMaxRetries(final int maxRetries) {
    this.maxRetries = maxRetries;
  }
}
