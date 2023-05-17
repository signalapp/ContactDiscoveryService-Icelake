package org.signal.cdsi.metrics;

import static org.signal.cdsi.metrics.MetricsUtil.name;

import io.micrometer.core.instrument.Metrics;
import io.micrometer.core.instrument.Tags;
import io.micronaut.context.event.ApplicationEvent;
import io.micronaut.context.event.ApplicationEventListener;
import io.micronaut.inject.ExecutableMethod;
import io.micronaut.retry.event.CircuitClosedEvent;
import io.micronaut.retry.event.CircuitOpenEvent;
import io.micronaut.retry.event.RetryEvent;
import jakarta.inject.Singleton;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

@Singleton
class CircuitBreakerMetricsListener implements ApplicationEventListener<ApplicationEvent> {

  private final Map<String, AtomicBoolean> breakersOpenByMethodName = new ConcurrentHashMap<>();

  private static final String RETRY_COUNTER_NAME = name(CircuitBreakerMetricsListener.class, "retries");
  private static final String BREAKER_OPEN_GAUGE_NAME = name(CircuitBreakerMetricsListener.class, "breakerOpen");

  @Override
  public boolean supports(final ApplicationEvent event) {
    return event instanceof RetryEvent || event instanceof CircuitOpenEvent || event instanceof CircuitClosedEvent;
  }

  @Override
  public void onApplicationEvent(final ApplicationEvent event) {
    if (event instanceof final RetryEvent retryEvent) {
      Metrics.counter(RETRY_COUNTER_NAME, "method", getMethodName(retryEvent.getSource())).increment();
    } else if (event instanceof final CircuitOpenEvent breakerOpenEvent) {
      final String methodName = getMethodName(breakerOpenEvent.getSource());
      breakersOpenByMethodName.computeIfAbsent(methodName, ignored -> new AtomicBoolean(true)).set(true);

      registerBreakerGauge(methodName);
    } else if (event instanceof final CircuitClosedEvent breakerClosedEvent) {
      final String methodName = getMethodName(breakerClosedEvent.getSource());
      breakersOpenByMethodName.computeIfAbsent(methodName, ignored -> new AtomicBoolean(false)).set(false);

      registerBreakerGauge(methodName);
    }
  }

  private void registerBreakerGauge(final String methodName) {
    Metrics.gauge(BREAKER_OPEN_GAUGE_NAME,
        Tags.of("method", methodName),
        this,
        listener -> listener.breakersOpenByMethodName.get(methodName).get() ? 1 : 0);
  }

  private static String getMethodName(final ExecutableMethod<?, ?> method) {
    return method.getTargetMethod().getDeclaringClass().getSimpleName() + "#" + method.getMethodName();
  }
}
