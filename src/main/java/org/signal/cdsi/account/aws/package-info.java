/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

@Configuration
@Requires(property = "account-table.region")
@Requires(property = "account-table.table-name")
@Requires(property = "account-table.stream-name")
package org.signal.cdsi.account.aws;

import io.micronaut.context.annotation.Configuration;
import io.micronaut.context.annotation.Requires;
