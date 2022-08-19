/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.auth;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import io.micronaut.security.authentication.AuthenticationException;
import io.micronaut.security.authentication.AuthenticationRequest;
import io.micronaut.security.authentication.AuthenticationResponse;
import java.security.InvalidKeyException;
import java.security.Key;
import java.security.NoSuchAlgorithmException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoField;
import java.util.Arrays;
import java.util.Random;
import java.util.concurrent.CompletableFuture;
import java.util.stream.Stream;
import javax.crypto.KeyGenerator;
import javax.crypto.Mac;
import org.apache.commons.codec.DecoderException;
import org.apache.commons.codec.binary.Hex;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.signal.cdsi.auth.ExternalServiceTokenAuthenticationProvider.TimestampAndSignature;
import org.signal.cdsi.limits.LeakyBucketRateLimiter;
import org.signal.cdsi.limits.RateLimitExceededException;
import reactor.core.publisher.Flux;

public class ExternalServiceTokenAuthenticationProviderTest {

  private Clock clock;

  private LeakyBucketRateLimiter rateLimiter;
  private ExternalServiceTokenAuthenticationProvider authenticationProvider;

  private static final String USERNAME = "12345678901234567890123456789012";
  private static final Key SHARED_SECRET = generateSharedSecret();
  private static final Duration TOKEN_EXPIRATION = Duration.ofDays(1);

  @BeforeEach
  void setUp() throws InvalidKeyException {
    clock = mock(Clock.class);
    when(clock.instant()).thenReturn(Instant.now());

    final AuthenticationConfiguration configuration = mock(AuthenticationConfiguration.class);
    when(configuration.getSharedSecret()).thenReturn(SHARED_SECRET.getEncoded());
    when(configuration.getTokenExpiration()).thenReturn(TOKEN_EXPIRATION);

    rateLimiter = mock(LeakyBucketRateLimiter.class);
    when(rateLimiter.validate(any(), anyInt())).thenReturn(CompletableFuture.completedFuture(null));

    authenticationProvider = new ExternalServiceTokenAuthenticationProvider(configuration, new SimpleMeterRegistry(), rateLimiter, clock);
  }

  @ParameterizedTest
  @MethodSource
  void testAuthenticate(final String username, final String secret, final Instant currentTime, final boolean expectAuthenticated) {
    when(clock.instant()).thenReturn(currentTime);

    @SuppressWarnings("unchecked") final AuthenticationRequest<String, String> authenticationRequest = mock(AuthenticationRequest.class);
    when(authenticationRequest.getIdentity()).thenReturn(username);
    when(authenticationRequest.getSecret()).thenReturn(secret);

    if (expectAuthenticated) {
      final AuthenticationResponse response = Flux.from(authenticationProvider.authenticate(null, authenticationRequest)).blockLast();

      assertNotNull(response);
      assertTrue(response.isAuthenticated());
    } else {
      final AuthenticationException authenticationException = assertThrows(AuthenticationException.class,
          () -> Flux.from(authenticationProvider.authenticate(null, authenticationRequest)).blockLast());

      assertNotNull(authenticationException.getResponse());
      assertFalse(authenticationException.getResponse().isAuthenticated());
    }
  }

  private static Stream<Arguments> testAuthenticate() {
    final Instant currentTime = Instant.now();
    final Stream.Builder<Arguments> argumentsStreamBuilder = Stream.builder();

    {
      final byte[] signature = getSignature(SHARED_SECRET, USERNAME, currentTime);
      final String secret = currentTime.getEpochSecond() + ":" + Hex.encodeHexString(signature);

      argumentsStreamBuilder.add(Arguments.of(USERNAME, secret, currentTime, true));
    }

    {
      // An incorrect username will yield an incorrect signature; other signature failure modes are covered in
      // {@link #testIsSignatureValid()}.
      final byte[] signature = getSignature(SHARED_SECRET, USERNAME + "nope", currentTime);
      final String secret = currentTime.getEpochSecond() + ":" + Hex.encodeHexString(signature);

      argumentsStreamBuilder.add(Arguments.of(USERNAME, secret, currentTime, false));
    }

    {
      final Instant timestamp = currentTime.plus(TOKEN_EXPIRATION).plusSeconds(1);

      final byte[] signature = getSignature(SHARED_SECRET, USERNAME, timestamp);
      final String secret = timestamp.getEpochSecond() + ":" + Hex.encodeHexString(signature);

      argumentsStreamBuilder.add(Arguments.of(USERNAME, secret, currentTime, false));
    }

    return argumentsStreamBuilder.build();
  }

  @Test
  void testAuthenticateRateLimited() {
    when(rateLimiter.validate(any(), anyInt()))
        .thenReturn(CompletableFuture.failedFuture(new RateLimitExceededException(Duration.ofSeconds(29))));

    final byte[] signature = getSignature(SHARED_SECRET, USERNAME, clock.instant());
    final String secret = clock.instant().getEpochSecond() + ":" + Hex.encodeHexString(signature);

    @SuppressWarnings("unchecked") final AuthenticationRequest<String, String> authenticationRequest = mock(
        AuthenticationRequest.class);
    when(authenticationRequest.getIdentity()).thenReturn(USERNAME);
    when(authenticationRequest.getSecret()).thenReturn(secret);

    final RateLimitExceededException exception = assertThrows(RateLimitExceededException.class,
        () -> {
          try {
            Flux.from(authenticationProvider.authenticate(null, authenticationRequest)).blockLast();
          } catch (Exception e) {
            throw e.getCause();
          }
        });
    assertEquals(Duration.ofSeconds(29), exception.getRetryDuration());
  }

  @Test
  void testParseToken() throws DecoderException {
    final Instant timestamp = clock.instant().with(ChronoField.MILLI_OF_SECOND, 0);
    final String encodedSignature = Hex.encodeHexString(getSignature(generateSharedSecret(), USERNAME, timestamp));

    final TimestampAndSignature timestampAndSignature =
        ExternalServiceTokenAuthenticationProvider.parseToken(timestamp.getEpochSecond() + ":" + encodedSignature);

    assertEquals(timestamp, timestampAndSignature.timestamp());
    assertArrayEquals(Hex.decodeHex(encodedSignature), timestampAndSignature.signature());
  }

  @ParameterizedTest
  @ValueSource(strings = {"", ":", ":::", ":notimestamp", "notanumber:abc123", "123", "1234:", "1234:nothex"})
  void testParseTokenMalformed(final String token) {
    assertThrows(IllegalArgumentException.class, () -> ExternalServiceTokenAuthenticationProvider.parseToken(token));
  }

  @Test
  void testIsValidSignature() {
    final Instant timestamp = clock.instant().with(ChronoField.MILLI_OF_SECOND, 0);
    final byte[] signature = getSignature(SHARED_SECRET, USERNAME, timestamp);

    assertTrue(authenticationProvider.isValidSignature(USERNAME, timestamp, signature));

    final byte[] bogusSignature = new byte[signature.length];
    new Random().nextBytes(bogusSignature);

    assertFalse(authenticationProvider.isValidSignature(USERNAME + "nope", timestamp, signature));
    assertFalse(authenticationProvider.isValidSignature(USERNAME, timestamp.plusSeconds(1), signature));
    assertFalse(authenticationProvider.isValidSignature(USERNAME, timestamp, bogusSignature));
  }

  @ParameterizedTest
  @MethodSource
  void testIsValidTime(final Instant currentTime, final Instant credentialTime, final boolean expectValid) {
    when(clock.instant()).thenReturn(currentTime);
    assertEquals(expectValid, authenticationProvider.isValidTime(credentialTime));
  }

  private static Stream<Arguments> testIsValidTime() {
    final Instant currentTime = Instant.now();

    return Stream.of(
        Arguments.of(currentTime, currentTime, true),
        Arguments.of(currentTime, currentTime.plus(TOKEN_EXPIRATION), true),
        Arguments.of(currentTime, currentTime.minus(TOKEN_EXPIRATION), true),
        Arguments.of(currentTime, currentTime.plus(TOKEN_EXPIRATION).plus(Duration.ofSeconds(1)), false),
        Arguments.of(currentTime, currentTime.minus(TOKEN_EXPIRATION).minus(Duration.ofSeconds(1)), false)
    );
  }

  private static Key generateSharedSecret() {
    try {
      final KeyGenerator keyGenerator = KeyGenerator.getInstance(ExternalServiceTokenAuthenticationProvider.ALGORITHM);
      return keyGenerator.generateKey();
    } catch (final NoSuchAlgorithmException e) {
      // All Java implementations are required to support HmacSHA256, so this can never happen
      throw new AssertionError(e);
    }
  }

  private static byte[] getSignature(final Key sharedSecret, final String username, final Instant timestamp) {
    try {
      final Mac hmac = Mac.getInstance(ExternalServiceTokenAuthenticationProvider.ALGORITHM);
      hmac.init(sharedSecret);

      return Arrays.copyOf(hmac.doFinal((username + ":" + timestamp.getEpochSecond()).getBytes()), 10);
    } catch (final NoSuchAlgorithmException | InvalidKeyException e) {
      // Neither of these cases can happen with HmacSHA256 (must be supported) or a locally-generated key
      throw new AssertionError(e);
    }
  }
}
