/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits;

import io.micronaut.context.annotation.ConfigurationProperties;
import io.micronaut.context.annotation.Context;

@Context
@ConfigurationProperties("tokenRateLimit")
public class TokenRateLimitConfiguration extends RateLimitConfiguration {
}
