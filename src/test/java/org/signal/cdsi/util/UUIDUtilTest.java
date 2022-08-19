/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

import com.google.protobuf.ByteString;
import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.*;

class UUIDUtilTest {

  @Test
  void toByteString() {
    final UUID uuid = UUID.randomUUID();
    final ByteString uuidByteString = UUIDUtil.toByteString(uuid);

    assertEquals(uuid, uuidFromByteBuffer(ByteBuffer.wrap(uuidByteString.toByteArray())));
  }

  @Test
  void toByteArray() {
    final UUID uuid = UUID.randomUUID();
    final byte[] uuidByteArray = UUIDUtil.toByteArray(uuid);

    assertEquals(uuid, uuidFromByteBuffer(ByteBuffer.wrap(uuidByteArray)));
  }

  @Test
  void toByteBuffer() {
    final UUID uuid = UUID.randomUUID();
    final ByteBuffer uuidByteBuffer = UUIDUtil.toByteBuffer(uuid);

    assertEquals(uuid, uuidFromByteBuffer(uuidByteBuffer));
  }

  private static UUID uuidFromByteBuffer(final ByteBuffer byteBuffer) {
    assertEquals(16, byteBuffer.remaining());
    return new UUID(byteBuffer.getLong(), byteBuffer.getLong());
  }
}
