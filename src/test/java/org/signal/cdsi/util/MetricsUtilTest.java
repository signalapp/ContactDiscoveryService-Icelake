/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

import org.junit.jupiter.api.Test;
import org.signal.cdsi.metrics.MetricsUtil;

import static org.junit.jupiter.api.Assertions.*;

class MetricsUtilTest {

  @Test
  void testNameFromClass() {
    assertEquals("cdsi.MetricsUtilTest.test", MetricsUtil.name(getClass(), "test"));
  }

  @Test
  void testNameFromString() {
    assertEquals("cdsi.MetricsUtilTest.test", MetricsUtil.name("MetricsUtilTest", "test"));
    assertEquals("cdsi.MetricsUtilTest.test", MetricsUtil.name("cdsi.MetricsUtilTest", "test"));
  }
}
