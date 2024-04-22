/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */
package org.signal.lambda;

import com.amazonaws.services.lambda.runtime.events.models.dynamodb.AttributeValue;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

class Account {

  @VisibleForTesting
  static final String KEY_ACCOUNT_UUID = "U";
  @VisibleForTesting
  static final String ATTR_ACCOUNT_E164 = "P";

  @VisibleForTesting
  static final String ATTR_PNI_UUID = "PNI";

  @VisibleForTesting
  static final String ATTR_CANONICALLY_DISCOVERABLE = "C";

  @VisibleForTesting
  static final String ATTR_UAK = "UAK";

  @JsonProperty
  String e164;

  @JsonProperty
  byte[] uuid;

  @JsonProperty
  boolean canonicallyDiscoverable;

  @JsonProperty
  byte[] pni;

  @JsonProperty
  byte[] uak;

  Account() {
  }  // empty constructor for JSON

  Account(String e164, byte[] uuid, boolean canonicallyDiscoverable, byte[] pni, byte[] uak) {
    this.e164 = e164;
    this.uuid = uuid;
    this.canonicallyDiscoverable = canonicallyDiscoverable;
    this.pni = pni;
    this.uak = uak;
  }

  static Account fromItem(Map<String, AttributeValue> item) {
    Preconditions.checkNotNull(item.get(KEY_ACCOUNT_UUID));
    Preconditions.checkNotNull(item.get(ATTR_ACCOUNT_E164));
    byte[] uuid = new byte[16];
    item.get(KEY_ACCOUNT_UUID).getB().get(uuid);
    byte[] pni = new byte[16];
    item.get(ATTR_PNI_UUID).getB().get(pni);

    byte[] uak;
    final AttributeValue uakAttributeValue = item.get(ATTR_UAK);
    if (uakAttributeValue != null) {
    ByteBuffer uakBB = uakAttributeValue.getB();
      uak = new byte[uakBB.remaining()];
      uakBB.get(uak);
    } else {
      uak = null;
    }

    return new Account(
        item.get(ATTR_ACCOUNT_E164).getS(),
        uuid,
        item.get(ATTR_CANONICALLY_DISCOVERABLE).getBOOL(),
        pni,
        uak);
  }

  Account forceNotInCds() {
    return new Account(e164, uuid, false, pni, uak);
  }

  // Partition such that the primary key (UUID) is maintained across streams.
  String partitionKey() {
    ByteBuffer s = ByteBuffer.wrap(uuid);
    return new UUID(s.getLong(), s.getLong()).toString();
  }

  @Override
  public boolean equals(final Object o) {
    if (this == o)
      return true;
    if (o == null || getClass() != o.getClass())
      return false;
    Account account = (Account) o;
    return canonicallyDiscoverable == account.canonicallyDiscoverable &&
        e164.equals(account.e164) &&
        Arrays.equals(uuid, account.uuid) &&
        Arrays.equals(pni, account.pni) &&
        Arrays.equals(uak, account.uak);
  }

  @Override
  public String toString() {
    return "Account{" +
        "e164='" + e164 +
        "', uuid=" + Arrays.toString(uuid) +
        ", canonicallyDiscoverable=" + canonicallyDiscoverable +
        ", uak=" + Arrays.toString(uak) +
        ", pni=" + Arrays.toString(pni) +
        "}";
  }
}
