/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

/**
 * A CDSI enclave exception represents an error reported by the business logic layer of the CDSI enclave. Please see
 * {@code error.h} for an enumeration of error codes.
 */
public class CdsiEnclaveException extends EnclaveException {

  private final int code;

  protected CdsiEnclaveException(final int code) {
    this.code = code;
  }

  /**
   * Returns the CDSI enclave error code that triggered this exception.
   *
   * @return the error code that triggered this exception
   */
  public int getCode() {
    return code;
  }

  @Override
  public String getMessage() {
    return String.format("CDSI enclave code %d", code);
  }
}
