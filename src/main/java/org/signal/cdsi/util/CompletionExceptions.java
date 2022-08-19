/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

import com.google.common.base.Preconditions;
import java.util.concurrent.CompletionException;

public class CompletionExceptions {

  /**
   * Wrap an exception in a CompletionException if it is not
   * already unchecked
   *
   * @param throwable a potentially checked exception
   * @return an unchecked exception
   */
  public static RuntimeException wrap(Throwable throwable) {
    Preconditions.checkNotNull(throwable);
    if (throwable instanceof RuntimeException ex) {
      return ex;
    }
    return new CompletionException(throwable);
  }

  /**
   * If throwable is a CompletionException, return the first non-CompletionException cause
   *
   * @param throwable
   * @return the unwrapped exception
   */
  public static Throwable unwrap(Throwable throwable) {
    Preconditions.checkNotNull(throwable);
    while(throwable instanceof CompletionException && throwable.getCause() != null) {
      throwable = throwable.getCause();
    }
    return throwable;
  }

}
