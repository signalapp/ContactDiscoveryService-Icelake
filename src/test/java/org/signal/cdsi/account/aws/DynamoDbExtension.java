/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import com.almworks.sqlite4java.SQLite;
import com.amazonaws.services.dynamodbv2.local.main.ServerRunner;
import com.amazonaws.services.dynamodbv2.local.server.DynamoDBProxyServer;
import org.junit.jupiter.api.extension.AfterEachCallback;
import org.junit.jupiter.api.extension.BeforeEachCallback;
import org.junit.jupiter.api.extension.ExtensionContext;
import software.amazon.awssdk.auth.credentials.AwsBasicCredentials;
import software.amazon.awssdk.auth.credentials.StaticCredentialsProvider;
import software.amazon.awssdk.regions.Region;
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
import java.net.ServerSocket;
import java.net.URI;
import java.util.ArrayList;
import java.util.List;

public class DynamoDbExtension implements BeforeEachCallback, AfterEachCallback {

  static final  String DEFAULT_TABLE_NAME = "test_table";

  static final ProvisionedThroughput DEFAULT_PROVISIONED_THROUGHPUT = ProvisionedThroughput.builder()
      .readCapacityUnits(20L)
      .writeCapacityUnits(20L)
      .build();

  private DynamoDBProxyServer server;
  private int port;

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
      server.stop();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Override
  public void beforeEach(ExtensionContext context) throws Exception {

    startServer();

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

  private void startServer() throws Exception {
    // Even though we're using AWS SDK v2, Dynamo's local implementation's canonical location
    // is within v1 (https://github.com/aws/aws-sdk-java-v2/issues/982).  This does support
    // v2 clients, though.
    SQLite.setLibraryPath("target/lib");  // if you see a library failed to load error, you need to run mvn test-compile at least once first
    ServerSocket serverSocket = new ServerSocket(0);
    serverSocket.setReuseAddress(false);
    port = serverSocket.getLocalPort();
    serverSocket.close();
    server = ServerRunner.createServerFromCommandLineArgs(new String[]{"-inMemory", "-port", String.valueOf(port), "-disableTelemetry", "true"});
    server.start();
  }

  private void initializeClient() {
    dynamoDbClient = DynamoDbClient.builder()
        .endpointOverride(URI.create("http://localhost:" + port))
        .region(Region.of("local-test-region"))
        .credentialsProvider(StaticCredentialsProvider.create(
            AwsBasicCredentials.create("accessKey", "secretKey")))
        .build();
    dynamoDbAsyncClient = DynamoDbAsyncClient.builder()
        .endpointOverride(URI.create("http://localhost:" + port))
        .region(Region.of("local-test-region"))
        .credentialsProvider(StaticCredentialsProvider.create(
            AwsBasicCredentials.create("accessKey", "secretKey")))
        .build();
    dynamoDbStreamsAsyncClient = DynamoDbStreamsAsyncClient.builder()
        .endpointOverride(URI.create("http://localhost:" + port))
        .region(Region.of("local-test-region"))
        .credentialsProvider(StaticCredentialsProvider.create(
            AwsBasicCredentials.create("accessKey", "secretKey")))
        .build();
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
