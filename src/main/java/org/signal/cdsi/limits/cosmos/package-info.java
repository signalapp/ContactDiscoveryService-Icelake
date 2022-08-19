/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

@Configuration
@Requires(property = "cosmos.database")
@Requires(property = "cosmos.container")
@Requires(property = "cosmos.endpoint")
@Requires(property = "cosmos.key")
package org.signal.cdsi.limits.cosmos;

import io.micronaut.context.annotation.Configuration;
import io.micronaut.context.annotation.Requires;
