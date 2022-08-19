/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

public class InvalidEntryException extends Exception {
  InvalidEntryException(String msg) {
    super(msg);
  }
}
