package org.signal.cdsi;

import io.micronaut.context.annotation.Property;
import io.micronaut.context.event.ApplicationEvent;
import io.micronaut.context.event.ApplicationEventListener;
import io.micronaut.runtime.event.ApplicationShutdownEvent;
import io.micronaut.websocket.CloseReason;
import io.micronaut.websocket.WebSocketSession;
import io.micronaut.websocket.event.WebSocketSessionClosedEvent;
import io.micronaut.websocket.event.WebSocketSessionOpenEvent;
import jakarta.inject.Singleton;
import java.time.Duration;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/// A WebSocket shutdown manager maintains a set of open WebSockets and attempts to close them after a configurable
/// duration after receiving an application shutdown event.
@Singleton
public class WebSocketShutdownManager implements ApplicationEventListener<ApplicationEvent> {

  private final Duration gracefulShutdownDuration;

  private final Set<WebSocketSession> openSessions = Collections.synchronizedSet(new HashSet<>());

  private static final Logger logger = LoggerFactory.getLogger(WebSocketShutdownManager.class);

  public WebSocketShutdownManager(@Property(name = "websocket.graceful-shutdown-duration", defaultValue = "PT1M") final Duration gracefulShutdownDuration) {
    this.gracefulShutdownDuration = gracefulShutdownDuration;
  }

  @Override
  public void onApplicationEvent(final ApplicationEvent event) {
    switch (event) {
      case WebSocketSessionOpenEvent sessionOpenEvent -> openSessions.add(sessionOpenEvent.getSource());
      case WebSocketSessionClosedEvent sessionClosedEvent -> openSessions.remove(sessionClosedEvent.getSource());

      case ApplicationShutdownEvent _ -> {
        logger.info("Received application shutdown event; waiting {} for WebSockets to close normally",
            gracefulShutdownDuration);
        try {
          // We can do this much more gracefully with the graceful shutdown system in Micronaut 4.9 (see
          // https://github.com/micronaut-projects/micronaut-core/pull/10701), but we can't upgrade to Micronaut 4.9
          // until Cosmos supports Netty 4.2. Until then, we use the strategy described in
          // https://github.com/micronaut-projects/micronaut-core/issues/2664.
          Thread.sleep(gracefulShutdownDuration);

          logger.info("Attempting to close {} remaining WebSockets", openSessions.size());
          openSessions.forEach(session -> session.close(CloseReason.GOING_AWAY));
        } catch (final InterruptedException e) {
          logger.error("Interrupted before gracefully shutting down open connections", e);
        }
      }

      default -> {}
    }
  }

  @Override
  public boolean supports(final ApplicationEvent event) {
    return switch (event) {
      case WebSocketSessionOpenEvent _, WebSocketSessionClosedEvent _, ApplicationShutdownEvent _ -> true;
      default -> false;
    };
  }
}
