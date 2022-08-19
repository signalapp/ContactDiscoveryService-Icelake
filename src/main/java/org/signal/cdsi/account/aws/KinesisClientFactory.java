/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import io.micronaut.context.annotation.Factory;
import jakarta.inject.Singleton;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.kinesis.KinesisAsyncClient;

@Factory
class KinesisClientFactory {

  private final AccountTableConfiguration accountTableConfiguration;

  public KinesisClientFactory(final AccountTableConfiguration accountTableConfiguration) {
    this.accountTableConfiguration = accountTableConfiguration;
  }

  @Singleton
  KinesisAsyncClient kinesisAsyncClient() {
    return KinesisAsyncClient.builder()
        .region(Region.of(accountTableConfiguration.getRegion()))
        .build();
  }
}
