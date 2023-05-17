/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.metrics;

public class MetricsUtil {

  static final String METRIC_NAME_PREFIX = "cdsi.";

  /**
   * Returns a dot-separated ('.') name for the given class and name parts
   */
  public static String name(Class<?> clazz, String... parts) {
    return name(clazz.getSimpleName(), parts);
  }

  /**
   * Returns a dot-separated ('.') name for the given name parts
   */
  public static String name(String name, String... parts) {
    final StringBuilder nameBuilder = new StringBuilder();

    if (!name.startsWith(METRIC_NAME_PREFIX)) {
      nameBuilder.append(METRIC_NAME_PREFIX);
    }

    nameBuilder.append(name);

    for (final String part : parts) {
      nameBuilder.append(".").append(part);
    }

    return nameBuilder.toString();
  }

}
