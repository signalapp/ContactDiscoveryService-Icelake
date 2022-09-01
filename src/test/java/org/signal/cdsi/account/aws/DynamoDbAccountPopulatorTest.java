/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import java.time.Clock;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.stream.Collectors;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.RegisterExtension;
import org.mockito.ArgumentCaptor;
import org.signal.cdsi.enclave.DirectoryEntry;
import org.signal.cdsi.enclave.Enclave;
import org.signal.cdsi.util.UUIDUtil;
import reactor.core.publisher.Mono;
import software.amazon.awssdk.core.SdkBytes;
import software.amazon.awssdk.services.dynamodb.model.AttributeDefinition;
import software.amazon.awssdk.services.dynamodb.model.AttributeValue;
import software.amazon.awssdk.services.dynamodb.model.PutItemRequest;
import software.amazon.awssdk.services.dynamodb.model.ScalarAttributeType;
import software.amazon.awssdk.services.kinesis.KinesisAsyncClient;
import software.amazon.awssdk.services.kinesis.model.Record;
import software.amazon.awssdk.services.kinesis.model.ShardIteratorType;
import software.amazon.awssdk.services.kinesis.model.SubscribeToShardEvent;
import software.amazon.awssdk.services.kinesis.model.SubscribeToShardRequest;

class DynamoDbAccountPopulatorTest {

  private Enclave enclave;
  private KinesisAsyncClient kinesisAsyncClient;

  private DynamoDbAccountPopulator accountPopulator;

  private long nextE164 = 1_800_000_0000L;
  private final Random random = new Random();

  private static final String ACCOUNTS_TABLE_NAME = "accounts_test";
  private static final String ACCOUNTS_STREAM_NAME = "account_stream_test";
  private static final String CONSUMER_ARN = "consumer-arn";

  @RegisterExtension
  static DynamoDbExtension dynamoDbExtension = DynamoDbExtension.builder()
      .tableName(ACCOUNTS_TABLE_NAME)
      .hashKey(DynamoDbAccountPopulator.KEY_ACCOUNT_UUID)
      .attributeDefinition(AttributeDefinition.builder()
          .attributeName(DynamoDbAccountPopulator.KEY_ACCOUNT_UUID)
          .attributeType(ScalarAttributeType.B)
          .build())
      .build();

  @BeforeEach
  void setUp() {
    enclave = mock(Enclave.class);
    when(enclave.loadData(any(), anyBoolean())).thenReturn(CompletableFuture.completedFuture(null));

    kinesisAsyncClient = mock(KinesisAsyncClient.class);
    when(kinesisAsyncClient.subscribeToShard(any(SubscribeToShardRequest.class), any()))
        .thenReturn(CompletableFuture.completedFuture(null));

    final KinesisStreamConsumerSupplier kinesisStreamConsumerSupplier = mock(KinesisStreamConsumerSupplier.class);
    when(kinesisStreamConsumerSupplier.getConsumerArn()).thenReturn(Mono.just(CONSUMER_ARN));

    final AccountTableConfiguration accountTableConfiguration = new AccountTableConfiguration();
    accountTableConfiguration.setTableName(ACCOUNTS_TABLE_NAME);
    accountTableConfiguration.setStreamName(ACCOUNTS_STREAM_NAME);

    accountPopulator = new DynamoDbAccountPopulator(enclave,
        dynamoDbExtension.getDynamoDbAsyncClient(),
        kinesisAsyncClient,
        kinesisStreamConsumerSupplier,
        new SimpleMeterRegistry(),
        Clock.systemUTC(),
        accountTableConfiguration);
  }

  @Test
  void getAccountSnapshot() {
    final Set<DirectoryEntry> expectedEntries = insertRandomAccounts(1000, true).stream()
        .map(DynamoDbAccountPopulatorTest::directoryEntryFromAccount)
        .collect(Collectors.toSet());

    insertRandomAccounts(500, false);

    final List<DirectoryEntry> retrievedEntries = accountPopulator.getAccountSnapshot().collectList()
        .block();

    assertNotNull(retrievedEntries);
    assertEquals(expectedEntries, new HashSet<>(retrievedEntries));
  }

  private List<Account> insertRandomAccounts(final int accounts, final boolean canonicallyDiscoverable) {
    final List<Account> insertedAccounts = new ArrayList<>(accounts);

    for (int i = 0; i < accounts; i++) {
      final Account account = generateRandomAccount(canonicallyDiscoverable);
      assert account.uak() != null;

      insertedAccounts.add(account);

      dynamoDbExtension.getDynamoDbClient().putItem(PutItemRequest.builder()
          .tableName(ACCOUNTS_TABLE_NAME)
          .item(Map.of(
              DynamoDbAccountPopulator.KEY_ACCOUNT_UUID,
              AttributeValue.builder().b(SdkBytes.fromByteBuffer(UUIDUtil.toByteBuffer(account.uuid()))).build(),
              DynamoDbAccountPopulator.ATTR_ACCOUNT_E164, AttributeValue.builder().s("+" + account.e164()).build(),
              DynamoDbAccountPopulator.ATTR_CANONICALLY_DISCOVERABLE,
              AttributeValue.builder().bool(account.canonicallyDiscoverable()).build(),
              DynamoDbAccountPopulator.ATTR_PNI,
              AttributeValue.builder().b(SdkBytes.fromByteBuffer(UUIDUtil.toByteBuffer(account.pni()))).build(),
              DynamoDbAccountPopulator.ATTR_UAK,
              AttributeValue.builder().b(SdkBytes.fromByteArray(account.uak())).build()
          ))
          .build());
    }

    return insertedAccounts;
  }

  @Test
  void handleSubscribeToShardEvent() throws JsonProcessingException {
    final ObjectMapper objectMapper = new ObjectMapper();

    final List<Account> accounts = new ArrayList<>();
    final List<Record> records = new ArrayList<>();

    for (int i = 0; i < 10; i++) {
      final Account account = generateRandomAccount(true);
      accounts.add(account);
      records.add(Record.builder()
          .sequenceNumber(String.valueOf(i))
          .data(SdkBytes.fromUtf8String(objectMapper.writeValueAsString(account)))
          .build());
    }

    final SubscribeToShardEvent event = SubscribeToShardEvent.builder()
        .continuationSequenceNumber("continuation")
        .millisBehindLatest(0L)
        .records(records)
        .build();

    assertFalse(accountPopulator.hasFinishedInitialAccountPopulation(),
        "Account population should not complete until at least one stream event has been processed");

    accountPopulator.handleSubscribeToShardEvent(event);

    assertTrue(accountPopulator.hasFinishedInitialAccountPopulation());

    final List<DirectoryEntry> expectedEntries = accounts.stream()
        .map(DynamoDbAccountPopulatorTest::directoryEntryFromAccount)
        .toList();

    verify(enclave).loadData(expectedEntries, false);
  }

  @Test
  void resubscribe() {
    final String continuationSequenceNumber = "continuation";
    final String shardId = "shard-id";

    final SubscribeToShardEvent event = SubscribeToShardEvent.builder()
        .continuationSequenceNumber(continuationSequenceNumber)
        .millisBehindLatest(0L)
        .records(Collections.emptyList())
        .build();

    accountPopulator.setShardId(shardId);
    accountPopulator.setShouldRenewSubscription(true);

    // The main thing we're doing here is setting the populator's last continuation sequence number
    accountPopulator.handleSubscribeToShardEvent(event);
    accountPopulator.renewSubscription();

    verify(enclave, never()).loadData(any(), anyBoolean());

    final ArgumentCaptor<SubscribeToShardRequest> requestArgumentCaptor =
        ArgumentCaptor.forClass(SubscribeToShardRequest.class);

    verify(kinesisAsyncClient).subscribeToShard(requestArgumentCaptor.capture(), any());
    final SubscribeToShardRequest request = requestArgumentCaptor.getValue();

    assertEquals(CONSUMER_ARN, request.consumerARN());
    assertEquals(shardId, request.shardId());
    assertEquals(continuationSequenceNumber, request.startingPosition().sequenceNumber());
    assertEquals(ShardIteratorType.AFTER_SEQUENCE_NUMBER, request.startingPosition().type());
  }

  private static DirectoryEntry directoryEntryFromAccount(final Account account) {
    return new DirectoryEntry(account.e164(),
        UUIDUtil.toByteArray(account.uuid()),
        UUIDUtil.toByteArray(account.pni()),
        account.uak());
  }

  private Account generateRandomAccount(final boolean canonicallyDiscoverable) {
    final byte[] uak = new byte[16];
    random.nextBytes(uak);

    return new Account(++nextE164, UUID.randomUUID(), UUID.randomUUID(), uak, canonicallyDiscoverable);
  }

  @Test
  void e164FromString() {
    assertEquals(18005551234L, DynamoDbAccountPopulator.e164FromString("+18005551234"));
    assertThrows(IllegalArgumentException.class, () -> DynamoDbAccountPopulator.e164FromString("18005551234"));
  }
}
