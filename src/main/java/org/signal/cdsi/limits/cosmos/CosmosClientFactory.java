/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import com.azure.cosmos.ConsistencyLevel;
import com.azure.cosmos.CosmosAsyncClient;
import com.azure.cosmos.CosmosAsyncContainer;
import com.azure.cosmos.CosmosClientBuilder;
import io.micronaut.context.annotation.Factory;
import jakarta.annotation.PreDestroy;
import jakarta.inject.Singleton;

@Factory
public class CosmosClientFactory {

  private CosmosAsyncClient cosmosAsyncClient;

  @Singleton
  public CosmosAsyncContainer cosmosAsyncContainer(final CosmosClientConfiguration configuration) {
    cosmosAsyncClient = new CosmosClientBuilder()
        .endpoint(configuration.getEndpoint())
        .key(configuration.getKey())
        .consistencyLevel(ConsistencyLevel.SESSION)
        .buildAsyncClient();

    return cosmosAsyncClient.getDatabase(configuration.getDatabase()).getContainer(configuration.getContainer());
  }

  @PreDestroy
  public void preDestroy() {
    if (cosmosAsyncClient != null) {
      cosmosAsyncClient.close();
    }
  }
}
