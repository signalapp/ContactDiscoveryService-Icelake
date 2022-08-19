/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import com.google.common.annotations.VisibleForTesting;
import io.micronaut.context.annotation.Factory;
import jakarta.inject.Singleton;
import software.amazon.awssdk.core.client.config.ClientOverrideConfiguration;
import software.amazon.awssdk.core.retry.RetryMode;
import software.amazon.awssdk.core.retry.RetryPolicy;
import software.amazon.awssdk.core.retry.conditions.OrRetryCondition;
import software.amazon.awssdk.core.retry.conditions.RetryCondition;
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
            .retryPolicy(getRetryPolicy())
            .build())
        .build();
  }

  @VisibleForTesting
  static RetryPolicy getRetryPolicy() {
    // Our DynamoDB client will only ever do one thing: load a huge pile of accounts at startup. That process involves
    // a very large number of behind-the-scenes requests. For "normal" DynamoDB usage (i.e. using DynamoDB as part of a
    // process that fulfills requests from users), we'd want a retry policy that gives up after some reasonable amount
    // of time or retries. Clients can retry their requests if they fail and don't wind up waiting on requests that can
    // get bogged down for a long time.
    //
    // This is different. Loading accounts is a relatively long process, and we must complete it before the application
    // can start serving requests. Giving up partway through is really not an option. We want to doggedly persist at
    // loading accounts until the job is done no matter how long it takes and how many times we need to retry.
    return RetryPolicy.forRetryMode(RetryMode.ADAPTIVE)
        .copy(builder -> builder.numRetries(Integer.MAX_VALUE)
            .additionalRetryConditionsAllowed(false)
            .retryCapacityCondition(null)
            .retryCondition(OrRetryCondition.create(RetryCondition.defaultRetryCondition(),
                new RetryOnProvisionedCapacityExceededCondition()))
            .fastFailRateLimiting(false));
  }
}
