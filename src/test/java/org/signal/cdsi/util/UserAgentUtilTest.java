package org.signal.cdsi.util;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;
import org.signal.cdsi.util.UserAgentUtil;

import javax.annotation.Nullable;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;

class UserAgentUtilTest {
  @ParameterizedTest
  @MethodSource("userAgentTests")
  void isBlank_ShouldReturnTrueForNullOrBlankStrings(@Nullable String input, String expected) {
    assertEquals(expected, UserAgentUtil.platformFromHeader(input).getValue());
  }

  private static Stream<Arguments> userAgentTests() {
    return Stream.of(
        Arguments.of(null, "empty"),
        Arguments.of("", "empty"),
        Arguments.of(" \t\n", "empty"),
        Arguments.of("bzlhb;eh.", "unrecognized"),
        Arguments.of("Signal-Android/1.0.0", "android"),
        Arguments.of("signal-ios/2.2.2", "ios"),
        Arguments.of("SiGnAl-DeSkToP/2.2.1-alphamaryfoxtrot withsomemorestuff", "desktop")
    );
  }
}
