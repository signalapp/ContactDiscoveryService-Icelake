/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi;

import static org.signal.cdsi.metrics.MetricsUtil.name;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import io.micrometer.core.annotation.Counted;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Tag;
import io.micrometer.core.instrument.Tags;
import io.micrometer.core.instrument.Timer;
import io.micrometer.core.instrument.Timer.Sample;
import io.micronaut.http.annotation.Header;
import io.micronaut.runtime.http.scope.RequestScope;
import io.micronaut.security.annotation.Secured;
import io.micronaut.security.rules.SecurityRule;
import io.micronaut.websocket.CloseReason;
import io.micronaut.websocket.WebSocketSession;
import io.micronaut.websocket.annotation.OnClose;
import io.micronaut.websocket.annotation.OnMessage;
import io.micronaut.websocket.annotation.OnOpen;
import io.micronaut.websocket.annotation.ServerWebSocket;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import javax.annotation.Nullable;
import org.signal.cdsi.enclave.CdsiEnclaveException;
import org.signal.cdsi.enclave.Enclave;
import org.signal.cdsi.enclave.EnclaveClient;
import org.signal.cdsi.enclave.EnclaveClient.State;
import org.signal.cdsi.enclave.EnclaveException;
import org.signal.cdsi.enclave.OpenEnclaveException;
import org.signal.cdsi.limits.RateLimitExceededException;
import org.signal.cdsi.limits.RetryAfterMessage;
import org.signal.cdsi.util.CompletionExceptions;
import org.signal.cdsi.util.UserAgentUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Secured(SecurityRule.IS_AUTHENTICATED)
@ServerWebSocket("/v1/{enclaveId}/discovery")
@RequestScope
public class WebSocketHandler {

  private static final Logger logger = LoggerFactory.getLogger(WebSocketHandler.class);

  static final ObjectMapper OBJECT_MAPPER = new ObjectMapper()
      .setPropertyNamingStrategy(PropertyNamingStrategies.SNAKE_CASE)
      .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
      .configure(DeserializationFeature.FAIL_ON_NULL_CREATOR_PROPERTIES, false)
      .configure(DeserializationFeature.FAIL_ON_MISSING_CREATOR_PROPERTIES, false);

  private static final String CLOSE_COUNTER_NAME = name(WebSocketHandler.class, "close");
  private static final String SESSION_TIMER_NAME = name(WebSocketHandler.class, "session");
  private static final String ENCLAVE_OP_TIMER_NAME = name(WebSocketHandler.class, "enclaveOperation");
  private static final String OPEN_WEBSOCKET_GAUGE_NAME = name(WebSocketHandler.class, "openWebsockets");
  private static final String ENCLAVE_ERROR_CODES_COUNTER_NAME = name(WebSocketHandler.class, "enclaveErrorCodes");
  private static final String ENCLAVE_ERROR_CODES_TAG_CODE = "code";

  private static final AtomicInteger OPEN_WEBSOCKET_COUNT = new AtomicInteger(0);

  private final Enclave enclave;
  private EnclaveClient client;
  private final MeterRegistry meterRegistry;
  private String userId;
  private Tag platformTag;

  // `chain` is an in-order chain of events done by this websocket.  Each websocket
  // action (open, message, close) resets the chain by adding steps to it.  This
  // guarantees in-order execution of events.
  private CompletableFuture<Void> chain = CompletableFuture.completedFuture(null);
  private boolean closed = false;

  // records the duration of this websocket session
  private Sample sessionSample;

  public WebSocketHandler(Enclave enclave, MeterRegistry meterRegistry) {
    this.enclave = enclave;
    this.meterRegistry = meterRegistry;

    this.meterRegistry.gauge(OPEN_WEBSOCKET_GAUGE_NAME, OPEN_WEBSOCKET_COUNT);
  }

  /** If the websocket has not already been closed, close it erroneously. */
  private Void closeWithError(final WebSocketSession session, Throwable cause) {
    if (closed || client == null) return null;
    closed = true;
    cause = CompletionExceptions.unwrap(cause);

    if (cause instanceof RateLimitExceededException) {
      logger.debug("Websocket for session {} closed due to rate limit exceeded", session.getId());
    } else if (cause instanceof ClosedEarlyException) {
      logger.debug("Websocket for session {} closed early", session.getId());
    } else if (cause instanceof EnclaveException) {
      logger.debug("Websocket for session {} closed due to enclave exception", session.getId(), cause);
    } else {
      logger.warn("Closing websocket session {} erroneously", session.getId(), cause);
    }

    final CloseReason closeReason;

    // See https://grpc.github.io/grpc/core/md_doc_statuscodes.html
    if (cause instanceof RateLimitExceededException rle) {
      closeReason = new CloseReason(4008, retryAfterCloseReason(rle.getRetryDuration()));
    } else if (cause instanceof IOException) {
      closeReason = new CloseReason(4014, cause.getMessage());
    } else if (cause instanceof IllegalArgumentException) {
      closeReason = new CloseReason(4003, cause.getMessage());
    } else if (cause instanceof CdsiEnclaveException enclaveException) {
      meterRegistry.counter(ENCLAVE_ERROR_CODES_COUNTER_NAME, List.of(
          Tag.of(ENCLAVE_ERROR_CODES_TAG_CODE, "cdsi_" + enclaveException.getCode()), platformTag)).increment();
      closeReason = new CloseReason(statusCodeFromCdsiEnclaveException(enclaveException), cause.getMessage());
    } else if (cause instanceof OpenEnclaveException enclaveException) {
      meterRegistry.counter(ENCLAVE_ERROR_CODES_COUNTER_NAME, List.of(
          Tag.of(ENCLAVE_ERROR_CODES_TAG_CODE, "oe_" + enclaveException.getCode()), platformTag)).increment();
      closeReason = new CloseReason(4013, cause.getMessage());
    } else {
      closeReason = new CloseReason(4013, cause.getMessage());
    }
    this.close(session, closeReason);
    return null;
  }


  private String retryAfterCloseReason(Duration retryAfter) {
      try {
        return OBJECT_MAPPER.writeValueAsString(new RetryAfterMessage(retryAfter.toSeconds()));
      } catch (JsonProcessingException e) {
        logger.error("Failed to serialize retry after", e);
        return "";
      }
  }

  private static int statusCodeFromCdsiEnclaveException(CdsiEnclaveException e) {
    // 4000 - 4099: maps to GRPC error codes
    // 4100 - 4199: CDSI-specific error codes
    return switch (e.getCode()) {
      case 202, // err_ENCLAVE__LOADPB__SECRET_TOO_LARGE,
          203, // err_ENCLAVE__LOADPB__TUPLES_INVALID,
          204, // err_ENCLAVE__LOADPB__REQUEST_PB_DECODE,
          301, // err_ENCLAVE__NEWCLIENT__EREPORT_TOO_LARGE,
          404, // err_ENCLAVE__RATELIMIT__INVALID_E164S,
          408, // err_ENCLAVE__RATELIMIT__NO_TOKEN,
          409, // err_ENCLAVE__RATELIMIT__UNSUPPORTED_VERSION,
          410, // err_ENCLAVE__RATELIMIT__REQUEST_PB_DECODE,
          412, // err_ENCLAVE__RATELIMIT__REQUEST_PB_INVALID,
          413, // err_ENCLAVE__RATELIMIT__INVALID_TOKEN_FORMAT,
          503, // err_ENCLAVE__RUN__NO_TOKEN_ACK,
          506 // err_ENCLAVE__RUN__REQUEST_PB_DECODE,
          -> 4003;  // invalid argument

      case 403 // err_ENCLAVE__RATELIMIT__INVALID_TOKEN,
          -> 4101;  // invalid rate limit token

      default -> 4013;  // internal error
    };
  }

  @OnOpen
  @Counted("cdsi.WebSocketHandler.onOpen.count")
  public void onOpen(final WebSocketSession session, String enclaveId, @Nullable @Header("User-Agent") String userAgentString) {
    logger.trace("Opening websocket session for {} for enclave={}", session.getId(), enclaveId);
    OPEN_WEBSOCKET_COUNT.incrementAndGet();

    this.userId = session.getUserPrincipal().get().getName();
    this.platformTag = UserAgentUtil.platformFromHeader(userAgentString);
    this.sessionSample = Timer.start();
    chain = chain
        .thenCompose(v -> enclave.newClient(userId))
        .thenCompose(client -> {
          WebSocketHandler.this.client = client;
          return session.sendAsync(client.getEreport());
        })
        .thenAccept(ignore -> {})
        .exceptionally(err -> closeWithError(session, err));
  }


  @OnMessage(maxPayloadLength = 10<<20)
  public void onMessage(final WebSocketSession session, final byte[] message) {
    logger.trace("Received websocket message for userId {} on session {}", userId, session.getId());
    final ByteBuffer msg = ByteBuffer.allocateDirect(message.length).put(message).flip();
    chain = chain
        .thenCompose(v -> {
          logger.trace("Processing websocket message for userId {} on session {}", userId, session.getId());
          return switch (client.getState()) {
            case UNINITIALIZED -> time("handshake", client.handshake(msg));
            case ATTESTED -> time("rateLimit", client.rateLimit(msg));
            case RATELIMIT -> time("complete", client.complete(msg));
            default -> throw new IllegalStateException("Enclave client received message in invalid state");
          };
        })
        .thenCompose(response -> session.sendAsync(response))
        .thenAccept(v -> {
          logger.trace("Client state: {}", client.getState());
          if (client.getState() == State.COMPLETE) {
            logger.trace("Closing websocket session {} normally", session.getId());
            this.close(session, CloseReason.NORMAL);
            closed = true;
          }
        })
        .exceptionally(err -> closeWithError(session, err));
  }

  private static class ClosedEarlyException extends Exception {}

  @OnClose
  public void onClose(final WebSocketSession session) {
    logger.trace("Closing websocket session {} for userId {}", session.getId(), userId);
    OPEN_WEBSOCKET_COUNT.decrementAndGet();

    chain = chain
        .thenApply(v -> closeWithError(session, new ClosedEarlyException()))
        .thenAccept(ignored -> sessionSample.stop(meterRegistry.timer(SESSION_TIMER_NAME, Tags.of(platformTag))));
    // We use whenComplete to make sure that even if issues arise with other parts of processing,
    // the closeAsync method will be called.  Note also that we don't check for or wait for it to
    // complete, we just start it.
    chain.whenComplete((v, e) -> {
      if (client != null) {
        client.closeAsync();
      }
    });
  }

  private <T> CompletableFuture<T> time(String operationName, CompletableFuture<T> future) {
    Sample start = Timer.start();
    return future.whenComplete((ignored1, ignored2) ->
        start.stop(meterRegistry.timer(ENCLAVE_OP_TIMER_NAME, "type", operationName)));
  }

  private void close(WebSocketSession session, CloseReason closeReason) {
    this.meterRegistry.counter(CLOSE_COUNTER_NAME, "closeCode", Integer.toString(closeReason.getCode()))
        .increment();
    session.close(closeReason);
  }
}
