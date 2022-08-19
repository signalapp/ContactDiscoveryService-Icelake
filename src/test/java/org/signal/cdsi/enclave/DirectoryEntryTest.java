/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.UUID;
import org.junit.jupiter.api.Test;
import org.signal.cdsi.util.UUIDUtil;

class DirectoryEntryTest {

  @Test
  void isDeletion() {
    assertFalse(new DirectoryEntry(18005551234L,
        UUIDUtil.toByteArray(UUID.randomUUID()),
        UUIDUtil.toByteArray(UUID.randomUUID()),
        UUIDUtil.toByteArray(UUID.randomUUID()))
        .isDeletion());

    assertTrue(DirectoryEntry.deletionEntry(18005551234L).isDeletion());
  }
}
