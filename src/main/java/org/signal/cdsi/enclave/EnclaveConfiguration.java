/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import io.micronaut.context.annotation.ConfigurationProperties;
import io.micronaut.context.annotation.Context;

import jakarta.validation.constraints.DecimalMax;
import jakarta.validation.constraints.DecimalMin;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Positive;
import org.signal.cdsi.util.ByteSize;

@Context
@ConfigurationProperties("enclave")
public class EnclaveConfiguration {

  @NotBlank
  private String enclaveId;

  @Positive
  private long availableEpcMemory;

  @DecimalMin("1.0")
  @DecimalMax("3.0")
  private double loadFactor;

  @Positive
  private int shards;

  @NotBlank @ByteSize(max = 64)
  private String tokenSecret;

  @Positive
  private int maxOutstandingRequests = 100;

  private boolean omitPermitsUsed = false;

  private boolean simulated;

  public String getEnclaveId() {
    return enclaveId;
  }

  public void setEnclaveId(final String enclaveId) {
    this.enclaveId = enclaveId;
  }

  public long getAvailableEpcMemory() {
    return availableEpcMemory;
  }

  public void setAvailableEpcMemory(final long availableEpcMemory) {
    this.availableEpcMemory = availableEpcMemory;
  }

  public double getLoadFactor() {
    return loadFactor;
  }

  public void setLoadFactor(final double loadFactor) {
    this.loadFactor = loadFactor;
  }

  public int getShards() {
    return shards;
  }

  public void setShards(final int shards) {
    this.shards = shards;
  }

  public String getTokenSecret() {
    return tokenSecret;
  }

  public void setTokenSecret(final String tokenSecret) {
    this.tokenSecret = tokenSecret;
  }

  public boolean isSimulated() {
    return simulated;
  }

  public void setSimulated(final boolean simulated) {
    this.simulated = simulated;
  }

  public boolean isOmitPermitsUsed() {
    return omitPermitsUsed;
  }

  public void setOmitPermitsUsed(final boolean omitPermitsUsed) {
    this.omitPermitsUsed = omitPermitsUsed;
  }

  public int getMaxOutstandingRequests() {
    return maxOutstandingRequests;
  }

  public void setMaxOutstandingRequests(final int maxOutstandingRequests) {
    this.maxOutstandingRequests = maxOutstandingRequests;
  }
}
