/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

import io.micronaut.test.extensions.junit5.annotation.MicronautTest;
import jakarta.inject.Inject;
import jakarta.inject.Singleton;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import jakarta.validation.ConstraintViolationException;

@MicronautTest
public class ByteSizeValidatorTest {

  @Singleton
  public static class TestConfig {

    public void minAndMax(@ByteSize(min = 1, max = 2) String test) {
    }

    public void justMax(@ByteSize(max = 2) String test) {
    }
  }

  @Inject
  TestConfig tc;

  @Test
  public void testMinAndMax() {
    Assertions.assertThrows(ConstraintViolationException.class, () -> tc.minAndMax(""));
    tc.minAndMax("a");
    tc.minAndMax("ab");
    Assertions.assertThrows(ConstraintViolationException.class, () -> tc.minAndMax("abc"));
  }

  @Test
  public void testMax() {
    tc.justMax("");
    tc.justMax("a");
    tc.justMax("ab");
    Assertions.assertThrows(ConstraintViolationException.class, () -> tc.justMax("abc"));
  }

}
