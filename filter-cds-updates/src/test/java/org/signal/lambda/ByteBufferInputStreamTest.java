/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */
package org.signal.lambda;

import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;

import static org.junit.jupiter.api.Assertions.*;

class ByteBufferInputStreamTest {
  @Test
  void testReadByte() throws Exception {
    ByteBuffer b = ByteBuffer.allocate(4);
    b.put(new byte[]{1,2,3,4});
    ByteBufferInputStream bbis = new ByteBufferInputStream(b.flip().asReadOnlyBuffer());
    assertEquals(1, bbis.read());
    assertEquals(2, bbis.read());
    assertEquals(3, bbis.read());
    assertEquals(4, bbis.read());
    assertEquals(-1, bbis.read());
  }
  @Test
  void testReadByteArray() throws Exception {
    ByteBuffer b = ByteBuffer.allocate(4);
    b.put(new byte[]{1,2,3,4});
    ByteBufferInputStream bbis = new ByteBufferInputStream(b.flip().asReadOnlyBuffer());
    byte output[] = new byte[10];
    assertEquals(4, bbis.read(output));
    assertArrayEquals(new byte[]{1,2,3,4,0,0,0,0,0,0}, output);
    assertEquals(0, bbis.read(output));
  }
  @Test
  void testReadByteArrayOffsetLength() throws Exception {
    ByteBuffer b = ByteBuffer.allocate(4);
    b.put(new byte[]{1,2,3,4});
    ByteBufferInputStream bbis = new ByteBufferInputStream(b.flip().asReadOnlyBuffer());
    byte output[] = new byte[10];
    assertEquals(2, bbis.read(output, 2, 2));
    assertEquals(2, bbis.read(output, 4, 6));
    assertArrayEquals(new byte[]{0,0,1,2,3,4,0,0,0,0}, output);
    assertEquals(0, bbis.read(output));
  }
}
