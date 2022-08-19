/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account;

import io.micronaut.context.annotation.Requires;
import io.micronaut.context.annotation.Value;
import io.micronaut.scheduling.TaskExecutors;
import jakarta.annotation.PostConstruct;
import jakarta.inject.Named;
import jakarta.inject.Singleton;
import java.util.ArrayList;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.atomic.AtomicBoolean;
import org.signal.cdsi.enclave.DirectoryEntry;
import org.signal.cdsi.enclave.Enclave;
import org.signal.cdsi.util.UUIDUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * An {@link AccountPopulator} implementation that generates random accounts and adds them to an enclave. This is
 * intended solely for testing purposes.
 */
@Singleton
@Requires(missingBeans = AccountPopulator.class)
@Requires(env = "dev")
public class RandomAccountPopulator implements AccountPopulator {

  private final Enclave enclave;
  private final int accounts;
  private final ExecutorService executorService;

  private final AtomicBoolean accountsPopulated = new AtomicBoolean(false);

  private static final int BATCH_SIZE = 4096;

  private static final Logger logger = LoggerFactory.getLogger(RandomAccountPopulator.class);

  public RandomAccountPopulator(final Enclave enclave,
      @Value("${random-account-populator.accounts:0}") final int accounts,
      @Named(TaskExecutors.IO) ExecutorService executorService) {
    this.enclave = enclave;
    this.accounts = accounts;
    this.executorService = executorService;
  }

  @PostConstruct
  void populateAccounts() {
    CompletableFuture.runAsync(() -> {
      final long start = System.currentTimeMillis();

      final ArrayList<DirectoryEntry> entries = new ArrayList<>(BATCH_SIZE);
      long e164 = 18000000000L;

      for (int i = 0; i < accounts; i++) {
        entries.add(new DirectoryEntry(e164++,
            UUIDUtil.toByteArray(UUID.randomUUID()),
            UUIDUtil.toByteArray(UUID.randomUUID()),
            UUIDUtil.toByteArray(UUID.randomUUID())));

        if (entries.size() % BATCH_SIZE == 0) {
          enclave.loadData(entries, false).join();
          entries.clear();
        }
      }

      if (!entries.isEmpty()) {
        enclave.loadData(entries, false).join();
      }

      logger.info("Populated enclave with {} random accounts in {} milliseconds.", accounts, System.currentTimeMillis() - start);
      accountsPopulated.set(true);
    }, executorService);
  }

  @Override
  public boolean hasFinishedInitialAccountPopulation() {
    return accountsPopulated.get();
  }

  @Override
  public boolean isHealthy() {
    return true;
  }
}
