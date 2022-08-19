package org.signal.cdsi.account.aws;

import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import software.amazon.awssdk.core.retry.RetryPolicyContext;
import software.amazon.awssdk.services.dynamodb.model.ProvisionedThroughputExceededException;

class DynamoDbClientFactoryTest {

  @Test
  void retryPolicyAllowsProvisionedThroughputCapacityExceededExceptions() {
    final RetryPolicyContext retryPolicyContext = RetryPolicyContext.builder()
        .exception(ProvisionedThroughputExceededException.builder().build())
        .build();

    assertTrue(DynamoDbClientFactory.getRetryPolicy().aggregateRetryCondition().shouldRetry(retryPolicyContext));
  }
}
