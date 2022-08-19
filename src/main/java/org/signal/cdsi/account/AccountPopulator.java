/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account;

public interface AccountPopulator {

  boolean hasFinishedInitialAccountPopulation();

  boolean isHealthy();
}
