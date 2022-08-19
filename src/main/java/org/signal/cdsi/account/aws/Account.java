/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import javax.annotation.Nullable;
import java.util.UUID;

record Account(long e164, UUID uuid, UUID pni, @Nullable byte[] uak, boolean canonicallyDiscoverable) {
}
