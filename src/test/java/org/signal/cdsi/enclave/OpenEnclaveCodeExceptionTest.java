/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;

@EnabledOnOs(OS.LINUX)
public class OpenEnclaveCodeExceptionTest {
  @Test
  void enclaveError() throws Exception {
    Enclave.loadSharedLibrary("test");
    OpenEnclaveException err = assertThrows(OpenEnclaveException.class,
        () -> Enclave.nativeEnclaveInit(1, 2.0, 1, "/path/does/not/exist", true));
    assertEquals(err.getCodeName(), "OE_FAILURE");
    assertEquals(err.getFunction(), "oe_create_cds_enclave");
  }
}
