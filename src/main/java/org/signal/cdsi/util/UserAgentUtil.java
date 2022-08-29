/*
 * Copyright 2013-2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

import io.micrometer.core.instrument.Tag;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class UserAgentUtil {
  private static final Pattern STANDARD_UA_PATTERN = Pattern.compile("^Signal-(Android|Desktop|iOS)/([^ ]+)( (.+))?$", Pattern.CASE_INSENSITIVE);

  public static Tag platformFromHeader(String s) {
    if (s == null || s.isBlank()) return Tag.of("platform", "empty");
    Matcher matcher = STANDARD_UA_PATTERN.matcher(s);
    if (!matcher.matches()) return Tag.of("platform", "unrecognized");
    return Tag.of("platform", matcher.group(1).toLowerCase());
  }
}
