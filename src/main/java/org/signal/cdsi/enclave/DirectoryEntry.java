/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import com.google.protobuf.ByteString;
import javax.annotation.Nullable;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.Objects;

public record DirectoryEntry(long e164, byte[] aci, byte[] pni, @Nullable byte[] uak) {

  private static final byte[] EMPTY_BYTE_ARRAY = new byte[0];
  private static final byte[] ALL_EMPTY = new byte[48];
  private static final byte[] EMPTY_UUID = new byte[16];

  public static DirectoryEntry deletionEntry(final long e164) {
    return new DirectoryEntry(e164, EMPTY_BYTE_ARRAY, EMPTY_BYTE_ARRAY, EMPTY_BYTE_ARRAY);
  }

  void writeTo(final ByteString.Output byteString) throws InvalidEntryException {
    final ByteBuffer bb = ByteBuffer.allocate(8 + 16 + 16 + 16).order(ByteOrder.BIG_ENDIAN);

    bb.putLong(e164);
    if (isDeletion()) {
      bb.put(ALL_EMPTY);
    } else if (pni.length != 16 || aci.length != 16) {
      throw new InvalidEntryException(
          String.format("Invalid sizes : ACI=%d PNI=%d UAK=%d for ACI %s",
              aci.length, pni.length, uak != null ? uak.length : 0, HexFormat.of().formatHex(aci)));
    } else {
      bb.put(uak != null && uak.length == 16 ? aci : EMPTY_UUID);
      bb.put(pni);
      bb.put(uak != null && uak.length == 16 ? uak : EMPTY_UUID);
    }

    try {
      byteString.write(bb.array());
    } catch (IOException e) {
      throw new AssertionError("failed to write to bytestring output", e);
    }
  }

  public boolean isDeletion() {
    return aci().length == 0;
  }

  @Override
  public boolean equals(final Object o) {
    if (this == o)
      return true;
    if (o == null || getClass() != o.getClass())
      return false;
    final DirectoryEntry that = (DirectoryEntry) o;
    return e164 == that.e164 && Arrays.equals(aci, that.aci) && Arrays.equals(pni, that.pni) && Arrays.equals(uak,
        that.uak);
  }

  @Override
  public int hashCode() {
    int result = Objects.hash(e164);
    result = 31 * result + Arrays.hashCode(aci);
    result = 31 * result + Arrays.hashCode(pni);
    result = 31 * result + Arrays.hashCode(uak);
    return result;
  }
}
