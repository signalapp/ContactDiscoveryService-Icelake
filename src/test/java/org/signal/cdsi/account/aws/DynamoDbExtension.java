/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import com.amazonaws.services.dynamodbv2.local.embedded.DynamoDBEmbedded;
import com.amazonaws.services.dynamodbv2.local.shared.access.AmazonDynamoDBLocal;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.extension.AfterEachCallback;
import org.junit.jupiter.api.extension.BeforeEachCallback;
import org.junit.jupiter.api.extension.ExtensionContext;
import software.amazon.awssdk.services.dynamodb.DynamoDbAsyncClient;
import software.amazon.awssdk.services.dynamodb.DynamoDbClient;
import software.amazon.awssdk.services.dynamodb.model.AttributeDefinition;
import software.amazon.awssdk.services.dynamodb.model.CreateTableRequest;
import software.amazon.awssdk.services.dynamodb.model.GlobalSecondaryIndex;
import software.amazon.awssdk.services.dynamodb.model.KeySchemaElement;
import software.amazon.awssdk.services.dynamodb.model.KeyType;
import software.amazon.awssdk.services.dynamodb.model.ProvisionedThroughput;
import software.amazon.awssdk.services.dynamodb.model.StreamSpecification;
import software.amazon.awssdk.services.dynamodb.streams.DynamoDbStreamsAsyncClient;

public class DynamoDbExtension implements BeforeEachCallback, AfterEachCallback {

  static final  String DEFAULT_TABLE_NAME = "test_table";

  static final ProvisionedThroughput DEFAULT_PROVISIONED_THROUGHPUT = ProvisionedThroughput.builder()
      .readCapacityUnits(20L)
      .writeCapacityUnits(20L)
      .build();

  private AmazonDynamoDBLocal embedded;

  private final String tableName;
  private final String hashKeyName;
  private final String rangeKeyName;

  private final List<AttributeDefinition> attributeDefinitions;
  private final List<GlobalSecondaryIndex> globalSecondaryIndexes;
  private final StreamSpecification streamSpecification;

  private final long readCapacityUnits;
  private final long writeCapacityUnits;

  private DynamoDbClient dynamoDbClient;
  private DynamoDbAsyncClient dynamoDbAsyncClient;
  private DynamoDbStreamsAsyncClient dynamoDbStreamsAsyncClient;

  private DynamoDbExtension(String tableName, String hashKey, String rangeKey, List<AttributeDefinition> attributeDefinitions, List<GlobalSecondaryIndex> globalSecondaryIndexes, long readCapacityUnits,
      long writeCapacityUnits, StreamSpecification streamSpecification) {

    this.tableName = tableName;
    this.hashKeyName = hashKey;
    this.rangeKeyName = rangeKey;

    this.readCapacityUnits = readCapacityUnits;
    this.writeCapacityUnits = writeCapacityUnits;

    this.attributeDefinitions = attributeDefinitions;
    this.globalSecondaryIndexes = globalSecondaryIndexes;
    this.streamSpecification = streamSpecification;
  }

  public static DynamoDbExtensionBuilder builder() {
    return new DynamoDbExtensionBuilder();
  }

  @Override
  public void afterEach(ExtensionContext context) {
    try {
      embedded.shutdown();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Override
  public void beforeEach(ExtensionContext context) throws Exception {

    embedded = DynamoDBEmbedded.create(true);

    initializeClient();

    createTable();
  }

  private void createTable() {
    KeySchemaElement[] keySchemaElements;
    if (rangeKeyName == null) {
      keySchemaElements = new KeySchemaElement[] {
          KeySchemaElement.builder().attributeName(hashKeyName).keyType(KeyType.HASH).build(),
      };
    } else {
      keySchemaElements = new KeySchemaElement[] {
          KeySchemaElement.builder().attributeName(hashKeyName).keyType(KeyType.HASH).build(),
          KeySchemaElement.builder().attributeName(rangeKeyName).keyType(KeyType.RANGE).build(),
      };
    }

    final CreateTableRequest createTableRequest = CreateTableRequest.builder()
        .tableName(tableName)
        .keySchema(keySchemaElements)
        .attributeDefinitions(attributeDefinitions.isEmpty() ? null : attributeDefinitions)
        .globalSecondaryIndexes(globalSecondaryIndexes.isEmpty() ? null : globalSecondaryIndexes)
        .streamSpecification(streamSpecification)
        .provisionedThroughput(ProvisionedThroughput.builder()
            .readCapacityUnits(readCapacityUnits)
            .writeCapacityUnits(writeCapacityUnits)
            .build())
        .build();

    getDynamoDbClient().createTable(createTableRequest);
  }

  private void initializeClient() {
    dynamoDbClient = embedded.dynamoDbClient();
    dynamoDbAsyncClient = embedded.dynamoDbAsyncClient();
    dynamoDbStreamsAsyncClient = embedded.dynamoDbStreamsAsyncClient();
  }

  public static class DynamoDbExtensionBuilder {
    private String tableName = DEFAULT_TABLE_NAME;

    private String hashKey;
    private String rangeKey;

    private List<AttributeDefinition> attributeDefinitions = new ArrayList<>();
    private List<GlobalSecondaryIndex> globalSecondaryIndexes = new ArrayList<>();
    private StreamSpecification streamSpecification = null;

    private long readCapacityUnits = DEFAULT_PROVISIONED_THROUGHPUT.readCapacityUnits();
    private long writeCapacityUnits = DEFAULT_PROVISIONED_THROUGHPUT.writeCapacityUnits();

    private DynamoDbExtensionBuilder() {

    }

    public DynamoDbExtensionBuilder tableName(String databaseName) {
      this.tableName = databaseName;
      return this;
    }

    public DynamoDbExtensionBuilder hashKey(String hashKey) {
      this.hashKey = hashKey;
      return this;
    }

    public DynamoDbExtensionBuilder rangeKey(String rangeKey) {
      this.rangeKey = rangeKey;
      return this;
    }

    public DynamoDbExtensionBuilder attributeDefinition(AttributeDefinition attributeDefinition) {
      attributeDefinitions.add(attributeDefinition);
      return this;
    }

    public DynamoDbExtensionBuilder globalSecondaryIndex(GlobalSecondaryIndex index) {
      globalSecondaryIndexes.add(index);
      return this;
    }

    public DynamoDbExtensionBuilder stream(StreamSpecification stream) {
      streamSpecification = stream;
      return this;
    }

    public DynamoDbExtension build() {
      return new DynamoDbExtension(tableName, hashKey, rangeKey,
          attributeDefinitions, globalSecondaryIndexes, readCapacityUnits, writeCapacityUnits, streamSpecification);
    }
  }

  public DynamoDbClient getDynamoDbClient() {
    return dynamoDbClient;
  }

  public DynamoDbAsyncClient getDynamoDbAsyncClient() {
    return dynamoDbAsyncClient;
  }

  public DynamoDbStreamsAsyncClient getDynamoDbStreamsAsyncClient() { return dynamoDbStreamsAsyncClient; }

  public String getTableName() {
    return tableName;
  }
}
