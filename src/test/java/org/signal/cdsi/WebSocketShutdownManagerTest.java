package org.signal.cdsi;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;

import io.micronaut.context.event.ApplicationEvent;
import io.micronaut.runtime.EmbeddedApplication;
import io.micronaut.runtime.event.ApplicationShutdownEvent;
import io.micronaut.websocket.CloseReason;
import io.micronaut.websocket.WebSocketSession;
import io.micronaut.websocket.event.WebSocketSessionClosedEvent;
import io.micronaut.websocket.event.WebSocketSessionOpenEvent;
import java.time.Duration;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

class WebSocketShutdownManagerTest {

  private WebSocketShutdownManager webSocketShutdownManager;

  @BeforeEach
  void setUp() {
    webSocketShutdownManager = new WebSocketShutdownManager(Duration.ZERO);
  }

  @Test
  void onApplicationEvent() {
    final WebSocketSession openSession = mock(WebSocketSession.class);
    webSocketShutdownManager.onApplicationEvent(new WebSocketSessionOpenEvent(openSession));

    final WebSocketSession closedSession = mock(WebSocketSession.class);
    webSocketShutdownManager.onApplicationEvent(new WebSocketSessionOpenEvent(closedSession));
    webSocketShutdownManager.onApplicationEvent(new WebSocketSessionClosedEvent(closedSession));

    webSocketShutdownManager.onApplicationEvent(new ApplicationShutdownEvent(mock(EmbeddedApplication.class)));

    verify(openSession).close(CloseReason.GOING_AWAY);
    verifyNoInteractions(closedSession);
  }

  @Test
  void supports() {
    assertTrue(webSocketShutdownManager.supports(new WebSocketSessionOpenEvent(mock(WebSocketSession.class))));
    assertTrue(webSocketShutdownManager.supports(new WebSocketSessionClosedEvent(mock(WebSocketSession.class))));
    assertTrue(webSocketShutdownManager.supports(new ApplicationShutdownEvent(mock(EmbeddedApplication.class))));
    assertFalse(webSocketShutdownManager.supports(new ApplicationEvent(this)));
  }

  @Test
  void onApplicationEventSpuriousEvent() {
    assertDoesNotThrow(() -> webSocketShutdownManager.onApplicationEvent(new ApplicationEvent(this)));
  }
}
