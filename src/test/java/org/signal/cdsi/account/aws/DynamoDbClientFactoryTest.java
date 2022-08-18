package org.signal.cdsi.account.aws;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import software.amazon.awssdk.core.retry.RetryPolicy;
import software.amazon.awssdk.core.retry.RetryPolicyContext;
import software.amazon.awssdk.services.dynamodb.model.ProvisionedThroughputExceededException;

import static org.junit.jupiter.api.Assertions.*;

class DynamoDbClientFactoryTest {

  private DynamoDbClientFactory dynamoDbClientFactory;

  @BeforeEach
  void setUp() {
    final AccountTableConfiguration configuration = new AccountTableConfiguration();
    configuration.setMaxRetries(8);

    dynamoDbClientFactory = new DynamoDbClientFactory(configuration);
  }

  @Test
  void retryPolicyAllowsProvisionedThroughputCapacityExceededExceptions() {
    final RetryPolicyContext retryPolicyContext = RetryPolicyContext.builder()
        .exception(ProvisionedThroughputExceededException.builder().build())
        .build();

    assertTrue(dynamoDbClientFactory.getRetryPolicy().aggregateRetryCondition().shouldRetry(retryPolicyContext));
  }
}
