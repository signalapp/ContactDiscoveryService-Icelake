/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import io.micronaut.context.annotation.Factory;
import jakarta.inject.Singleton;
import software.amazon.awssdk.core.client.config.ClientOverrideConfiguration;
import software.amazon.awssdk.core.retry.RetryMode;
import software.amazon.awssdk.core.retry.RetryPolicy;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.dynamodb.DynamoDbAsyncClient;

@Factory
class DynamoDbClientFactory {

  private final AccountTableConfiguration accountTableConfiguration;

  public DynamoDbClientFactory(final AccountTableConfiguration accountTableConfiguration) {
    this.accountTableConfiguration = accountTableConfiguration;
  }

  @Singleton
  DynamoDbAsyncClient dynamoDbAsyncClient() {
    return DynamoDbAsyncClient.builder()
        .region(Region.of(accountTableConfiguration.getRegion()))
        .overrideConfiguration(ClientOverrideConfiguration.builder()
            .retryPolicy(RetryPolicy.forRetryMode(RetryMode.ADAPTIVE)
                .copy(builder -> builder.numRetries(accountTableConfiguration.getMaxRetries())
                    .fastFailRateLimiting(false)))
            .build())
        .build();
  }
}
