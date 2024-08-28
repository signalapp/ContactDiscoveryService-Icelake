/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import java.io.IOException;
import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.signal.cdsi.limits.TokenRateLimiter;
import org.signal.cdsi.util.UUIDUtil;

@EnabledOnOs(OS.LINUX)
class EnclaveTest {

  private ExecutorService enclaveExecutor;
  private Clock clock;

  private Enclave enclave;

  private static final int SHARD_COUNT = 8;

  @BeforeEach
  void setUp() throws EnclaveException, IOException {
    final EnclaveConfiguration configuration = new EnclaveConfiguration();
    configuration.setEnclaveId("test");
    configuration.setTokenSecret("test");
    configuration.setSimulated(true);
    configuration.setShards(SHARD_COUNT);
    configuration.setAvailableEpcMemory(3200000);
    configuration.setLoadFactor(1.6);

    final TokenRateLimiter tokenRateLimiter = mock(TokenRateLimiter.class);

    enclaveExecutor = Executors.newSingleThreadExecutor();
    clock = mock(Clock.class);

    enclave = new Enclave(configuration, tokenRateLimiter, new SimpleMeterRegistry(), enclaveExecutor, clock);
  }

  @AfterEach
  @SuppressWarnings("ResultOfMethodCallIgnored")
  void tearDown() throws InterruptedException, EnclaveException {
    enclave.close();

    enclaveExecutor.shutdown();
    enclaveExecutor.awaitTermination(1, TimeUnit.SECONDS);
  }

  @Test
  void close() {
    final Instant firstAttestation = Instant.now();
    when(clock.instant()).thenReturn(firstAttestation);

    enclave.renewAttestation();
    assertEquals(Optional.of(firstAttestation), enclave.getLastAttestationTimestamp());

    assertFalse(enclave.isClosed());
    assertDoesNotThrow(enclave::close);
    assertTrue(enclave.isClosed());

    final CompletionException createClientException =
        assertThrows(CompletionException.class, () -> enclave.newClient("test").join(),
            "Client creation should not be allowed after enclave closure");

    assertTrue(createClientException.getCause() instanceof RejectedExecutionException);

    when(clock.instant()).thenReturn(firstAttestation.plusSeconds(60));

    assertDoesNotThrow(enclave::renewAttestation, "Attestation renewal should not throw after enclave closure");
    assertEquals(Optional.of(firstAttestation), enclave.getLastAttestationTimestamp(),
        "Calls to renew attestation should have no effect after enclave closure");
  }

  @Test
  void tableStatistics() {
    // Even an empty table starts with a single "dummy" entry in each shard
    assertEquals(SHARD_COUNT, Enclave.getEntryCount(enclave.getTableStatistics().join()));
    assertTrue(Enclave.getCapacity(enclave.getTableStatistics().join()) > 0);

    final long expectedEntries = Math.min(97, Enclave.getCapacity(enclave.getTableStatistics().join()));
    final List<DirectoryEntry> entries = new ArrayList<>((int) expectedEntries);

    for (int i = 0; i < expectedEntries; i++) {
      final long e164 = 18005551234L + i;

      entries.add(new DirectoryEntry(e164,
          UUIDUtil.toByteArray(UUID.randomUUID()),
          UUIDUtil.toByteArray(UUID.randomUUID()),
          UUIDUtil.toByteArray(UUID.randomUUID())));
    }

    enclave.loadData(entries, true).join();

    assertEquals(expectedEntries + SHARD_COUNT, Enclave.getEntryCount(enclave.getTableStatistics().join()));
    assertEquals(expectedEntries, enclave.activeEntries.get());

    final List<DirectoryEntry> deletedEntires = entries.stream()
        .filter(e -> e.e164() % 2 == 0)
        .map(e -> DirectoryEntry.deletionEntry(e.e164()))
        .toList();

    enclave.loadData(deletedEntires, false).join();

    assertEquals(expectedEntries + SHARD_COUNT, Enclave.getEntryCount(enclave.getTableStatistics().join()));
    assertEquals(expectedEntries - deletedEntires.size(), enclave.activeEntries.get());
  }

  @Test
  void getRunningShardThreadCount() throws EnclaveException, InterruptedException {
    assertEquals(SHARD_COUNT, enclave.getRunningShardThreadCount());

    enclave.close();
    assertEquals(0, enclave.getRunningShardThreadCount());
  }
}
