package org.signal.cdsi.metrics;

import io.micrometer.core.instrument.Meter.Id;
import io.micrometer.core.instrument.config.MeterFilter;
import jakarta.inject.Singleton;

@Singleton
public class ExecutorMetricsPrefixFilter implements MeterFilter {

  @Override
  public Id map(final Id id) {
    if (id.getName().startsWith("executor.")) {
      return id.withName(MetricsUtil.METRIC_NAME_PREFIX + id.getName());
    }

    return MeterFilter.super.map(id);
  }
}
