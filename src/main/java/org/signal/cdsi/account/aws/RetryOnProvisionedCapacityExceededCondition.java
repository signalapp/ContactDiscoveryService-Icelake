package org.signal.cdsi.account.aws;

import software.amazon.awssdk.core.retry.RetryPolicyContext;
import software.amazon.awssdk.core.retry.conditions.RetryCondition;
import software.amazon.awssdk.services.dynamodb.model.ProvisionedThroughputExceededException;

public class RetryOnProvisionedCapacityExceededCondition implements RetryCondition {

  @Override
  public boolean shouldRetry(final RetryPolicyContext context) {
    return context.exception() instanceof ProvisionedThroughputExceededException;
  }
}
