/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;


import io.micronaut.context.annotation.Factory;
import io.micronaut.core.annotation.AnnotationValue;
import io.micronaut.core.annotation.Introspected;
import io.micronaut.validation.validator.constraints.ConstraintValidator;
import io.micronaut.validation.validator.constraints.ConstraintValidatorContext;
import jakarta.inject.Singleton;
import java.nio.charset.StandardCharsets;

@Factory
@Introspected
public class Validators {

  @Singleton
  ConstraintValidator<ByteSize, String> byteSizeValidator() {
    return (String value, AnnotationValue<ByteSize> annotationMetadata, ConstraintValidatorContext context) -> {
        if (value == null) {
          return true;
        }
        int lengthInBytes = value.toString().getBytes(StandardCharsets.UTF_8).length;
        final int min = annotationMetadata.intValue("min").orElse(0);
        final int max = annotationMetadata.intValue("max").orElse(Integer.MAX_VALUE);
        return lengthInBytes >= min && lengthInBytes <= max;
      };
  }
}
