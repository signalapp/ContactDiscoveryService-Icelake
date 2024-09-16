package org.signal.cdsi.limits;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.annotations.VisibleForTesting;
import jakarta.validation.constraints.Positive;

public class RetryAfterMessage {
  @Positive
  @JsonProperty
  long retryAfter;

  public RetryAfterMessage() {}
  public RetryAfterMessage(long retryAfter) {
    this.retryAfter = retryAfter;
  }

  @VisibleForTesting
  public long getRetryAfter() {
    return retryAfter;
  }
}
