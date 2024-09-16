/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.auth;

import static org.signal.cdsi.metrics.MetricsUtil.name;

import com.google.common.annotations.VisibleForTesting;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Tag;
import io.micronaut.http.HttpRequest;
import io.micronaut.security.authentication.AuthenticationFailureReason;
import io.micronaut.security.authentication.AuthenticationProvider;
import io.micronaut.security.authentication.AuthenticationRequest;
import io.micronaut.security.authentication.AuthenticationResponse;
import jakarta.inject.Named;
import jakarta.inject.Singleton;
import java.security.InvalidKeyException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.Arrays;
import java.util.List;
import javax.annotation.Nullable;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import org.apache.commons.codec.DecoderException;
import org.apache.commons.codec.binary.Hex;
import org.reactivestreams.Publisher;
import org.signal.cdsi.limits.LeakyBucketRateLimiter;
import org.signal.cdsi.util.UserAgentUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;

/**
 * An authentication provider that verifies credentials produced by an external service credential generator in the main
 * Signal server.
 */
@Singleton
public class ExternalServiceTokenAuthenticationProvider implements AuthenticationProvider<HttpRequest<?>> {

  private final byte[] sharedSecret;
  private final Duration tokenExpiration;
  private final MeterRegistry meterRegistry;

  private final LeakyBucketRateLimiter rateLimiter;
  private final Clock clock;

  public static final String CONNECTIONS_RATE_LIMITER_NAME = "connections";

  @VisibleForTesting
  static final String ALGORITHM = "HmacSHA256";

  private static final String AUTHENTICATION_COUNTER_NAME = name(ExternalServiceTokenAuthenticationProvider.class, "authentications");
  private static final String OUTCOME_TAG_NAME = "outcome";
  private static final String FAILURE_REASON_TAG_NAME = "failureReason";

  private static final Logger log = LoggerFactory.getLogger(ExternalServiceTokenAuthenticationProvider.class);

  @VisibleForTesting
  record TimestampAndSignature(Instant timestamp, byte[] signature) {}

  public ExternalServiceTokenAuthenticationProvider(AuthenticationConfiguration configuration,
      final MeterRegistry meterRegistry,
      @Named(CONNECTIONS_RATE_LIMITER_NAME) LeakyBucketRateLimiter rateLimiter,
      Clock clock) throws InvalidKeyException {

    this.sharedSecret = configuration.getSharedSecret();
    this.tokenExpiration = configuration.getTokenExpiration();
    this.meterRegistry = meterRegistry;
    this.rateLimiter = rateLimiter;
    this.clock = clock;

    try {
      // Fail fast and propagate an InvalidKeyException if the key isn't valid
      final Mac hmac = Mac.getInstance(ALGORITHM);
      hmac.init(new SecretKeySpec(sharedSecret, ALGORITHM));
    } catch (final NoSuchAlgorithmException e) {
      // All Java implementations are required to support HmacSHA256, so this really can't ever happen
      throw new AssertionError(e);
    }
  }

  @Override
  public Publisher<AuthenticationResponse> authenticate(@Nullable HttpRequest<?> httpRequest,
      AuthenticationRequest<?, ?> authenticationRequest) {
    final Tag platformTag = UserAgentUtil.platformFromHeader(
        httpRequest == null ? null : httpRequest.getHeaders().get("user-agent"));
    return Mono.create(mono -> {
      try {
        final String username = (String) authenticationRequest.getIdentity();
        final TimestampAndSignature timestampAndSignature = parseToken((String) authenticationRequest.getSecret());

        if (isValid(username, timestampAndSignature.timestamp(), timestampAndSignature.signature())) {
          rateLimiter.validate(username, 1).whenComplete((unused, err) -> {
            if (err != null) {
              incrementAuthenticationFailureCounter("rate_limit_exceeded", platformTag);
              mono.error(err);
            } else {
              incrementAuthenticationSuccessCounter(platformTag);
              mono.success(AuthenticationResponse.success(username));
            }
          });
        } else {
          incrementAuthenticationFailureCounter(AuthenticationFailureReason.CREDENTIALS_DO_NOT_MATCH, platformTag);
          mono.error(AuthenticationResponse.exception(AuthenticationFailureReason.CREDENTIALS_DO_NOT_MATCH));
        }
      } catch (final Exception e) {
        log.warn("Unexpected authentication exception", e);

        incrementAuthenticationFailureCounter(AuthenticationFailureReason.UNKNOWN, platformTag);
        mono.error(AuthenticationResponse.exception(AuthenticationFailureReason.UNKNOWN));
      }
    });
  }

  private void incrementAuthenticationSuccessCounter(Tag platformTag) {
    meterRegistry.counter(AUTHENTICATION_COUNTER_NAME, List.of(
        Tag.of(OUTCOME_TAG_NAME, "success"), platformTag)).increment();
  }

  private void incrementAuthenticationFailureCounter(
      final AuthenticationFailureReason authenticationFailureReason, Tag platformTag) {
    incrementAuthenticationFailureCounter(authenticationFailureReason.name().toLowerCase(), platformTag);
  }

  private void incrementAuthenticationFailureCounter(final String reason, Tag platformTag) {
    meterRegistry.counter(AUTHENTICATION_COUNTER_NAME,
        List.of(
            Tag.of(OUTCOME_TAG_NAME, "failure"),
            Tag.of(FAILURE_REASON_TAG_NAME, reason),
            platformTag)).increment();
  }

  private boolean isValid(final String username, final Instant timestamp, final byte[] signature) {
    return isValidTime(timestamp) && isValidSignature(username, timestamp, signature);
  }

  @VisibleForTesting
  boolean isValidTime(final Instant timestamp) {
    return Duration.between(clock.instant(), timestamp).abs().compareTo(tokenExpiration) <= 0;
  }

  @VisibleForTesting
  boolean isValidSignature(final String username, final Instant timestamp, final byte[] signature) {
    try {
      final Mac hmac = Mac.getInstance(ALGORITHM);
      hmac.init(new SecretKeySpec(sharedSecret, ALGORITHM));

      final String prefix = username + ":" + timestamp.getEpochSecond();

      final byte[] ourSuffix = Arrays.copyOf(hmac.doFinal(prefix.getBytes()), 10);  // truncate to 10 bytes
      return MessageDigest.isEqual(ourSuffix, signature);
    } catch (final NoSuchAlgorithmException | InvalidKeyException e) {
      // We checked the key at construction time and all Java implementations are required to support HmacSHA256, so
      // neither of these cases can ever actually happen.
      throw new AssertionError(e);
    }
  }

  @VisibleForTesting
  static TimestampAndSignature parseToken(final String token) {
    final int separatorIndex = token.indexOf(':');

    if (separatorIndex < 1 || separatorIndex == token.length() - 1) {
      throw new IllegalArgumentException("Invalid token format; expected two parts separated by a ':'");
    }

    final Instant timestamp;

    try {
      timestamp = Instant.ofEpochSecond(Long.parseLong(token, 0, separatorIndex, 10));
    } catch (final NumberFormatException e) {
      throw new IllegalArgumentException("Invalid token format; could not parse timestamp", e);
    }

    final byte[] signature;

    try {
      signature = Hex.decodeHex(token.substring(separatorIndex + 1).toCharArray());
    } catch (final DecoderException e) {
      throw new IllegalArgumentException("Failed to parse token signature", e);
    }

    return new TimestampAndSignature(timestamp, signature);
  }
}
