/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.protobuf.ByteString;
import io.lettuce.core.StrAlgoArgs.By;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.client.annotation.Client;
import io.micronaut.test.extensions.junit5.annotation.MicronautTest;
import io.micronaut.websocket.WebSocketClient;
import io.micronaut.websocket.exceptions.WebSocketClientException;
import io.netty.handler.codec.http.websocketx.WebSocketClientHandshakeException;
import jakarta.inject.Inject;
import jakarta.inject.Named;
import jakarta.inject.Singleton;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import org.junit.Assert;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.signal.cdsi.account.AccountPopulator;
import org.signal.cdsi.auth.AuthenticationHelper;
import org.signal.cdsi.auth.ExternalServiceTokenAuthenticationProvider;
import org.signal.cdsi.client.CdsiWebsocket;
import org.signal.cdsi.client.CdsiWebsocket.CloseException;
import org.signal.cdsi.enclave.DirectoryEntry;
import org.signal.cdsi.enclave.TestEnclave;
import org.signal.cdsi.limits.LeakyBucketRateLimiter;
import org.signal.cdsi.limits.ManualTokenRateLimiter;
import org.signal.cdsi.limits.RateLimitExceededException;
import org.signal.cdsi.limits.RetryAfterMessage;
import org.signal.cdsi.proto.ClientRequest;
import org.signal.cdsi.proto.ClientResponse;
import org.signal.cdsi.util.UUIDUtil;
import reactor.core.publisher.Mono;

@MicronautTest
class IntegrationTest {

  private static final String TEST_NUM = "+" + 0x01020304;

  @Singleton
  @Named(ExternalServiceTokenAuthenticationProvider.CONNECTIONS_RATE_LIMITER_NAME)
  public static class ManualLeakyBucketRateLimiter implements LeakyBucketRateLimiter {

    final static long RETRY_AFTER = 23;
    private boolean allow = true;

    public void setAllow(boolean allow) {
      this.allow = allow;
    }

    @Override
    public CompletableFuture<Void> validate(final String key, final int amount) {
      if (allow) {
        return CompletableFuture.completedFuture(null);
      } else {
        return CompletableFuture.failedFuture(new RateLimitExceededException(Duration.ofSeconds(RETRY_AFTER)));
      }
    }
  }

  @Inject
  ManualTokenRateLimiter tokenRateLimiter;

  @Inject
  ManualLeakyBucketRateLimiter connectionLimiter;


  @Singleton
  public static class TestAccountPopulator implements AccountPopulator {

    @Override
    public boolean hasFinishedInitialAccountPopulation() {
      return true;
    }

    @Override
    public boolean isHealthy() {
      return true;
    }
  }

  private static final long E164 = 0x01020304;
  private static final UUID ACI = UUID.randomUUID();
  private static final UUID PNI = UUID.randomUUID();
  private static final UUID UAK = UUID.randomUUID();

  private static final long E164_ADDED_REMOVED = 0x01020305;

  private static final UUID UUID_ZERO = new UUID(0, 0);
  private static final int RECORD_SIZE = 8+16+16;

  @BeforeEach
  void reset() throws InterruptedException {
    connectionLimiter.setAllow(true);
    tokenRateLimiter.reset();
    enclave.setOverloaded(false);
    enclave.waitForInitialAttestation();

    enclave.loadData(List.of(new DirectoryEntry(E164,
                UUIDUtil.toByteArray(ACI),
                UUIDUtil.toByteArray(PNI),
                UUIDUtil.toByteArray(UAK)),
            new DirectoryEntry(E164_ADDED_REMOVED,
                UUIDUtil.toByteArray(UUID.randomUUID()),
                UUIDUtil.toByteArray(UUID.randomUUID()),
                UUIDUtil.toByteArray(UUID.randomUUID())),
            DirectoryEntry.deletionEntry(E164_ADDED_REMOVED)),
        true);
  }

  private static byte[] concat(byte[]... a) {
    int size = 0;
    for (final byte[] bytes : a) {
      size += bytes.length;
    }
    byte[] out = new byte[size];
    int idx = 0;
    for (final byte[] bytes : a) {
      System.arraycopy(bytes, 0, out, idx, bytes.length);
      idx += a.length;
    }
    return out;
  }

  private static byte[] e164PniAciTriple(long e164, UUID pni, UUID aci) {
    ByteBuffer out = ByteBuffer.allocate(40);
    out.putLong(e164);
    out.put(UUIDUtil.toByteBuffer(pni));
    out.put(UUIDUtil.toByteBuffer(aci));
    return out.array();
  }

  @Inject
  @Client("/")
  WebSocketClient webSocketClient;

  @Inject
  TestEnclave enclave;

  @Test
  void authenticationMissing() {
    @SuppressWarnings("resource") final WebSocketClientException e = assertThrows(WebSocketClientException.class,
        () -> Mono.from(webSocketClient.connect(CdsiWebsocket.class, "/v1/test/discovery")).block());

    assertEquals(401, ((WebSocketClientHandshakeException) e.getCause()).response().status().code());
  }

  @Test
  void authenticationInvalidUsernamePassword() {
    @SuppressWarnings("resource") final WebSocketClientException e = assertThrows(WebSocketClientException.class,
        () -> Mono.from(webSocketClient.connect(
            CdsiWebsocket.class,
            HttpRequest.GET("/v1/test/discovery").basicAuth("a", "b"))).block());

    assertEquals(401, ((WebSocketClientHandshakeException) e.getCause()).response().status().code());
  }

  @Test
  void authenticationUnverifiedUsernamePassword() {
    @SuppressWarnings("resource") final WebSocketClientException e = assertThrows(WebSocketClientException.class,
        () -> Mono.from(webSocketClient.connect(
            CdsiWebsocket.class,
            HttpRequest.GET("/v1/test/discovery").basicAuth(
                "12345678901234567890123456789012",
                "1234567890:23456789012345678901"))).block());

    assertEquals(401, ((WebSocketClientHandshakeException) e.getCause()).response().status().code());
  }

  @Test
  void connectionLimitExceeded() {
    connectionLimiter.setAllow(false);
    @SuppressWarnings("resource") final WebSocketClientException e = assertThrows(WebSocketClientException.class,
        () -> Mono.from(webSocketClient.connect(
            CdsiWebsocket.class,
            AuthenticationHelper.HTTP_REQUEST)).block());

    assertEquals(429, ((WebSocketClientHandshakeException) e.getCause()).response().status().code());

    assertEquals(
        ManualLeakyBucketRateLimiter.RETRY_AFTER,
        Long.parseLong(((WebSocketClientHandshakeException) e.getCause()).response().headers().get("Retry-After")));
  }

  @Test
  void enclaveOverloaded() {
    {
      enclave.setOverloaded(true);

      @SuppressWarnings("resource") final WebSocketClientException e = assertThrows(WebSocketClientException.class,
          () -> Mono.from(webSocketClient.connect(
              CdsiWebsocket.class,
              AuthenticationHelper.HTTP_REQUEST)).block());

      assertEquals(508, ((WebSocketClientHandshakeException) e.getCause()).response().status().code());
    }

    {
      @SuppressWarnings("resource") final WebSocketClientException e = assertThrows(WebSocketClientException.class,
          () -> Mono.from(webSocketClient.connect(CdsiWebsocket.class, "/v1/test/discovery")).block());

      // Normally, trying to connect without credentials would result in a 401. Here, we want to make sure we don't even
      // bother with authentication (and, importantly, with rate-limiting) if we know the enclave is already overloaded.
      assertEquals(508, ((WebSocketClientHandshakeException) e.getCause()).response().status().code());
    }
  }

  @Test
  void testSingleE164() throws Exception {
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientResponse clientResponse = cdsiWebsocket.run(ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(Long.parseLong(TEST_NUM.substring(1)))
              .array()))
          .build());

      assertArrayEquals(
          concat(
              e164PniAciTriple(E164, PNI, UUID_ZERO)),
          clientResponse.getE164PniAciTriples().toByteArray());
      assertEquals(1, clientResponse.getDebugPermitsUsed());
    }
  }

  @Test
  void testResendMultipleE164s() throws Exception {
    ByteString token = ByteString.EMPTY;
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientRequest request =ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(1)
              .array()))
          .build();
      final ClientResponse clientResponse = cdsiWebsocket.run(request);

      Set<ByteString> expected = new HashSet<>();
      expected.add(ByteString.copyFrom(e164PniAciTriple(0, UUID_ZERO, UUID_ZERO)));
      Set<ByteString> got = new HashSet<>();
      for (int i = 0; i < clientResponse.getE164PniAciTriples().size(); i += RECORD_SIZE) {
        ByteString next = clientResponse.getE164PniAciTriples().substring(i, i+RECORD_SIZE);
        got.add(next);
      }
      assertEquals(expected, got);
      assertEquals(1, clientResponse.getDebugPermitsUsed());
      token = clientResponse.getToken();
    }
    // Send the same request again, but this time with the token we got and with the value in prev_e164s.
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientRequest request = ClientRequest
          .newBuilder()
          .setPrevE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(1)
              .array()))
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(64)
              .putLong(2)
              .putLong(3)
              .putLong(4)
              .putLong(5)
              .putLong(6)
              .putLong(7)
              .putLong(8)
              .putLong(E164)  // should be found
              .array()))
          .setToken(token)
          .build();
      final ClientResponse clientResponse = cdsiWebsocket.run(request);

      Set<ByteString> expected = new HashSet<>();
      expected.add(ByteString.copyFrom(e164PniAciTriple(E164, PNI, UUID_ZERO)));
      expected.add(ByteString.copyFrom(e164PniAciTriple(0, UUID_ZERO, UUID_ZERO)));
      Set<ByteString> got = new HashSet<>();
      for (int i = 0; i < clientResponse.getE164PniAciTriples().size(); i += RECORD_SIZE) {
        ByteString next = clientResponse.getE164PniAciTriples().substring(i, i+RECORD_SIZE);
        got.add(next);
      }
      assertEquals(expected, got);
      assertEquals(8, clientResponse.getDebugPermitsUsed());
    }
  }

  @Test
  void testSingleE164ValidUAK() throws Exception {
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientResponse clientResponse = cdsiWebsocket.run(ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(Long.parseLong(TEST_NUM.substring(1)))
              .array()))
          .setAciUakPairs(ByteString.copyFrom(List.of(UUIDUtil.toByteString(ACI), UUIDUtil.toByteString(UAK))))
          .build());

      assertArrayEquals(
          concat(
              e164PniAciTriple(E164, PNI, ACI)),
          clientResponse.getE164PniAciTriples().toByteArray());

      assertEquals(1, clientResponse.getDebugPermitsUsed());
    }
  }

  @Test
  void testSingleE164NotFound() throws Exception {
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientResponse clientResponse = cdsiWebsocket.run(ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(1)
              .array()))
          .setAciUakPairs(ByteString.copyFrom(List.of(UUIDUtil.toByteString(ACI), UUIDUtil.toByteString(UAK))))
          .build());

      assertArrayEquals(
          new byte[40],
          clientResponse.getE164PniAciTriples().toByteArray());

      assertEquals(1, clientResponse.getDebugPermitsUsed());
    }
  }

  @Test
  void testSingleE164NotFoundDeleted() throws Exception {
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientResponse clientResponse = cdsiWebsocket.run(ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(E164_ADDED_REMOVED)
              .array()))
          .setAciUakPairs(ByteString.copyFrom(List.of(UUIDUtil.toByteString(ACI), UUIDUtil.toByteString(UAK))))
          .build());

      assertArrayEquals(
          new byte[40],
          clientResponse.getE164PniAciTriples().toByteArray());

      assertEquals(1, clientResponse.getDebugPermitsUsed());
    }
  }

  @Test
  void testSingleE164InvalidUAK() throws Exception {
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientResponse clientResponse = cdsiWebsocket.run(ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(E164)
              .array()))
          .setAciUakPairs(
              ByteString.copyFrom(List.of(UUIDUtil.toByteString(ACI), UUIDUtil.toByteString(UUID.randomUUID()))))
          .build());

      assertArrayEquals(
          concat(
              e164PniAciTriple(E164, PNI, UUID_ZERO)),
          clientResponse.getE164PniAciTriples().toByteArray());

      assertEquals(1, clientResponse.getDebugPermitsUsed());
    }
  }

  @Test
  void testManyE164s() throws Exception {
    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final int numE164s = 10000;
      final ByteBuffer e164sBuf = ByteBuffer.allocate(numE164s * 8);

      for (long i = 0; i < numE164s; i++) {
        e164sBuf.putLong(i + 0xff000000L);
      }

      final ClientResponse clientResponse = cdsiWebsocket.run(ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(e164sBuf.flip()))
          .build());

      assertArrayEquals(
          new byte[40 * numE164s],
          clientResponse.getE164PniAciTriples().toByteArray());

      assertEquals(numE164s, clientResponse.getDebugPermitsUsed());
    }
  }


  @ParameterizedTest
  @ValueSource(booleans = {false, true})
  void testRateLimitExceeded(boolean limitAtAck) throws Exception {
    if (limitAtAck) {
      tokenRateLimiter.setValidateRetryAfter(Optional.of(Duration.ofSeconds(13)));
    } else {
      tokenRateLimiter.setPrepareRetryAfter(Optional.of(Duration.ofSeconds(13)));
    }

    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final ClientResponse clientResponse = cdsiWebsocket.run(ClientRequest
          .newBuilder()
          .setNewE164S(ByteString.copyFrom(ByteBuffer
              .allocate(8)
              .putLong(E164)
              .array()))
          .setAciUakPairs(
              ByteString.copyFrom(List.of(UUIDUtil.toByteString(ACI), UUIDUtil.toByteString(UUID.randomUUID()))))
          .build());

      Assert.fail("should throw a close exception after rate limit is exceeded");
    } catch (CloseException e) {
      assertEquals(4008, e.getReason().getCode());
      ObjectMapper objectMapper = new ObjectMapper();
      System.out.println(e.getReason().getReason());
      final RetryAfterMessage retryAfter = WebSocketHandler.OBJECT_MAPPER.readValue(e.getReason().getReason(),
          RetryAfterMessage.class);
      assertEquals(retryAfter.getRetryAfter(), 13);
    }
  }

  @Test
  void testInvalidToken() throws Exception {
    tokenRateLimiter.setValidateRetryAfter(Optional.of(Duration.ofSeconds(13)));

    try (final CdsiWebsocket cdsiWebsocket = Mono.from(webSocketClient.connect(
        CdsiWebsocket.class,
        AuthenticationHelper.HTTP_REQUEST)).block()) {

      assertNotNull(cdsiWebsocket);

      final byte[] badToken = new byte[65];
      badToken[0] = 1;  // version
      CdsiWebsocket.CloseException e = assertThrows(CdsiWebsocket.CloseException.class,
          () -> cdsiWebsocket.run(ClientRequest
              .newBuilder()
              .setNewE164S(ByteString.copyFrom(ByteBuffer
                  .allocate(8)
                  .putLong(E164)
                  .array()))
              .setAciUakPairs(
                  ByteString.copyFrom(List.of(UUIDUtil.toByteString(ACI), UUIDUtil.toByteString(UUID.randomUUID()))))
              .setToken(ByteString.copyFrom(badToken))
              .build()));
      assertEquals(4101, e.getReason().getCode());
    }
  }
}
