/*
 * Copyright 2025 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.util;

public class TestcontainersImages {

  private static final String DYNAMO_DB = "${dynamodb.image}";
  private static final String REDIS_CLUSTER = "${redis-cluster.image}";

  public static String getDynamoDb() {
    return DYNAMO_DB;
  }

  public static String getRedisCluster() {
    return REDIS_CLUSTER;
  }
}
