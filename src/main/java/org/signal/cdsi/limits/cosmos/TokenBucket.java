/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import java.util.Objects;

/**
 * The model for rate limiting bucket rows in a cosmos container
 */

class TokenBucket {

  public static final String ID = "0";

  // the user identifier, partition key
  private String key;

  // There should be one bucket per user, should be a constant
  private String id;

  // the amount in the bucket
  private double amount;

  // last update timestamp
  private String ts;

  // un-utilized permits from gcts to now can be used to
  // clean up old tokens
  private String gcts;

  // cosmos time to live. Cosmosdb will interpret the ttl field specially
  // and will automatically expire items ttl seconds after the last update time
  // (which is stored in the internal cosmos field _ts)
  private Integer ttl;

  public TokenBucket() {
  }

  public String getKey() {
    return this.key;
  }

  public void setKey(String key) {
    this.key = key;
  }

  public String getId() {
    return id;
  }

  public void setId(String id) {
    this.id = id;
  }

  public double getAmount() {
    return amount;
  }

  public void setAmount(final double amount) {
    this.amount = amount;
  }

  public String getTs() {
    return ts;
  }

  public void setTs(final String ts) {
    this.ts = ts;
  }

  public String getGcts() {
    return gcts;
  }

  public void setGcts(final String gcts) {
    this.gcts = gcts;
  }

  // getter required by cosmosdb sdk
  @SuppressWarnings("unused")
  public Integer getTtl() {
    return this.ttl;
  }

  public void setTtl(final Integer ttl) {
    this.ttl = ttl;
  }

  public static TokenBucket create(final String aci, final double amount, final String ts, int ttl) {
    final TokenBucket bucket = new TokenBucket();
    bucket.setKey(aci);
    bucket.setId(ID);
    bucket.setTs(ts);
    bucket.setAmount(amount);
    bucket.setGcts(ts);
    bucket.setTtl(ttl);
    return bucket;
  }

  @Override
  public boolean equals(final Object o) {
    if (this == o)
      return true;
    if (o == null || getClass() != o.getClass())
      return false;
    TokenBucket that = (TokenBucket) o;
    return Double.compare(that.amount, amount) == 0 && Objects.equals(key, that.key) && Objects.equals(id, that.id)
        && Objects.equals(ts, that.ts) && Objects.equals(gcts, that.gcts) && Objects.equals(ttl, that.ttl);
  }

  @Override
  public int hashCode() {
    return Objects.hash(key, id, amount, ts, gcts, ttl);
  }

  @Override
  public String toString() {
    return "TokenBucket{" +
        "key='" + key + '\'' +
        ", id='" + id + '\'' +
        ", amount=" + amount +
        ", ts='" + ts + '\'' +
        ", gcts='" + gcts + '\'' +
        ", ttl=" + ttl +
        '}';
  }
}
