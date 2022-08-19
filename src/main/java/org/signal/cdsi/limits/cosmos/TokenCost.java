/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import java.util.Objects;

/**
 * The model for the cosmos items representing the cost of an unused token
 */
class TokenCost {
  // the owner of the token (partition key)
  private String key;
  // the token's hash (base64 encoded)
  private String id;
  // the cost the request associated with this token
  private long cost;
  // timestamp
  private String ts;

  // cosmos time to live. Cosmosdb will interpret the ttl field specially
  // and will automatically expire items ttl seconds after the last update time
  // (which is stored in the internal cosmos field _ts)
  private Integer ttl;

  public TokenCost() {}

  public String getKey() {
    return key;
  }

  public void setKey(final String key) {
    this.key = key;
  }

  public String getId() {
    return id;
  }

  public void setId(final String id) {
    this.id = id;
  }

  public long getCost() {
    return cost;
  }

  public void setCost(final long cost) {
    this.cost = cost;
  }

  public String getTs() {
    return ts;
  }

  public void setTs(final String ts) {
    this.ts = ts;
  }

  // getter required by cosmosdb sdk
  @SuppressWarnings("unused")
  public Integer getTtl() {
    return this.ttl;
  }

  public void setTtl(final Integer ttl) {
    this.ttl = ttl;
  }

  public static TokenCost create(final String aci, final String id, final long cost, final String ts, final int ttl) {
    final TokenCost tc = new TokenCost();
    tc.setKey(aci);
    tc.setId(id);
    tc.setCost(cost);
    tc.setTs(ts);
    tc.setTtl(ttl);
    return tc;
  }

  @Override
  public boolean equals(final Object o) {
    if (this == o)
      return true;
    if (o == null || getClass() != o.getClass())
      return false;
    TokenCost tokenCost = (TokenCost) o;
    return cost == tokenCost.cost && Objects.equals(key, tokenCost.key) && Objects.equals(id, tokenCost.id)
        && Objects.equals(ts, tokenCost.ts) && Objects.equals(ttl, tokenCost.ttl);
  }

  @Override
  public int hashCode() {
    return Objects.hash(key, id, cost, ts, ttl);
  }

  @Override
  public String toString() {
    return "TokenCost{" +
        "key='" + key + '\'' +
        ", id='" + id + '\'' +
        ", cost=" + cost +
        ", ts='" + ts + '\'' +
        ", ttl=" + ttl +
        '}';
  }
}
