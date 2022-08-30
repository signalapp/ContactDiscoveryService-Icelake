/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

/**
 * An Open Enclave exception represents an error reported by the <a href="https://openenclave.io/sdk/">Open Enclave</a>
 * layer of the CDSI application.
 */
public class OpenEnclaveException extends EnclaveException {
  private final String function;

  private final String codeName;
  private final int code;

  public OpenEnclaveException(String function, String codeName, int code) {
    super();
    this.function = function;
    this.codeName = codeName;
    this.code = code;
  }

  /**
   * The Open Enclave error code that triggered this exception.
   *
   * @return the error code that triggered this exception
   */
  public int getCode() {
    return this.code;
  }

  /**
   * Returns the name of the function in which the error occurred.
   *
   * @return the name of the function in which the error occurred
   */
  public String getFunction() {
    return function;
  }

  /**
   * Returns the name of the error that occurred.
   *
   * @return the name of the error that occurred
   */
  public String getCodeName() {
    return codeName;
  }

  @Override
  public String getMessage() {
    return String.format("OpenEnclave in %s: %d=%s", function, code, codeName);
  }
}
