/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

import jakarta.validation.Constraint;
import jakarta.validation.Payload;
import java.lang.annotation.*;

import static java.lang.annotation.ElementType.*;
import static java.lang.annotation.RetentionPolicy.RUNTIME;

@Target({METHOD, FIELD, ANNOTATION_TYPE, CONSTRUCTOR, PARAMETER, TYPE_USE})
@Retention(RUNTIME)
@Documented
@Constraint(validatedBy = {})
public @interface ByteSize {

  String message() default "Invalid string length in bytes: {validatedValue}";

  Class<?>[] groups() default {};

  Class<? extends Payload>[] payload() default {};

  /**
   * @return size the element must be higher or equal to
   */
  int min() default 0;

  /**
   * @return size the element must be lower or equal to
   */
  int max() default Integer.MAX_VALUE;
}
