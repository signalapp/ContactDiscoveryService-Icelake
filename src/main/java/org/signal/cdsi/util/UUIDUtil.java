/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

import com.google.common.base.Preconditions;
import com.google.protobuf.ByteString;
import java.nio.ByteBuffer;
import java.util.UUID;

public class UUIDUtil {

  public static ByteString toByteString(final UUID uuid) {
    return ByteString.copyFrom(toByteBuffer(uuid));
  }

  public static byte[] toByteArray(final UUID uuid) {
    return toByteBuffer(uuid).array();
  }

  public static ByteBuffer toByteBuffer(final UUID uuid) {
    ByteBuffer out = ByteBuffer.allocate(16);
    out.putLong(uuid.getMostSignificantBits());
    out.putLong(uuid.getLeastSignificantBits());
    return out.flip();
  }
}
