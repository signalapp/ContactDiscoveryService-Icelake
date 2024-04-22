/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */
package org.signal.lambda;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;

public class ByteBufferInputStream extends InputStream {

  public ByteBufferInputStream(ByteBuffer buf) {
    this.buf = buf.slice();
  }

  private final ByteBuffer buf;

  @Override
  public int read() throws IOException {
    if (!buf.hasRemaining())
      return -1;
    return buf.get();
  }

  @Override
  public int read(byte[] b, int offset, int length) throws IOException {
    if (length > buf.remaining()) length = buf.remaining();
    buf.get(b, offset, length);
    return length;
  }
}
