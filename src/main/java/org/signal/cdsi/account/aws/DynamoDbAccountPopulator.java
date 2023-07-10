/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import static org.signal.cdsi.metrics.MetricsUtil.name;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import io.micrometer.core.instrument.Counter;
import io.micrometer.core.instrument.MeterRegistry;
import io.micronaut.scheduling.annotation.Scheduled;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.inject.Singleton;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import javax.annotation.Nullable;
import org.signal.cdsi.account.AccountPopulator;
import org.signal.cdsi.enclave.DirectoryEntry;
import org.signal.cdsi.enclave.Enclave;
import org.signal.cdsi.util.UUIDUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Flux;
import software.amazon.awssdk.core.async.SdkPublisher;
import software.amazon.awssdk.services.dynamodb.DynamoDbAsyncClient;
import software.amazon.awssdk.services.dynamodb.model.AttributeValue;
import software.amazon.awssdk.services.dynamodb.model.ScanRequest;
import software.amazon.awssdk.services.kinesis.KinesisAsyncClient;
import software.amazon.awssdk.services.kinesis.model.DescribeStreamRequest;
import software.amazon.awssdk.services.kinesis.model.Record;
import software.amazon.awssdk.services.kinesis.model.ShardIteratorType;
import software.amazon.awssdk.services.kinesis.model.StartingPosition;
import software.amazon.awssdk.services.kinesis.model.SubscribeToShardEvent;
import software.amazon.awssdk.services.kinesis.model.SubscribeToShardEventStream;
import software.amazon.awssdk.services.kinesis.model.SubscribeToShardRequest;
import software.amazon.awssdk.services.kinesis.model.SubscribeToShardResponse;
import software.amazon.awssdk.services.kinesis.model.SubscribeToShardResponseHandler;

/**
 * The DynamoDB account populator populates an {@link Enclave} with account data from a DynamoDB table and a Kinesis
 * stream that contains filtered updates from the table.
 *
 * @see <a href="https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Scan.html#Scan.ParallelScan">Amazon
 * DynamoDB Developer Guide - Working with scans in DynamoDB - Parallel scan</a>
 * @see <a href="https://docs.aws.amazon.com/kinesis/latest/APIReference/API_SubscribeToShard.html">Amazon Kinesis Data
 * Streams Service API Reference - SubscribeToShard</a>
 * @see <a href="https://docs.aws.amazon.com/sdk-for-java/latest/developer-guide/examples-kinesis-stream.html">AWS SDK
 * for Java Developer Guide - Subscribing to Amazon Kinesis Data Streams</a>
 */
@Singleton
class DynamoDbAccountPopulator implements AccountPopulator, SubscribeToShardResponseHandler {

  private final Enclave enclave;
  private final DynamoDbAsyncClient dynamoDbAsyncClient;
  private final KinesisAsyncClient kinesisAsyncClient;
  private final KinesisStreamConsumerSupplier streamConsumerSource;
  private final Clock clock;

  private final String accountTableName;
  private final String updateStreamName;
  private final int accountTableReadSegments;

  private final Counter entriesFromTableCounter;
  private final Counter entriesFromStreamCounter;

  @Nullable
  private String shardId;

  @Nullable
  private Instant populateStartTime;

  @Nullable
  private volatile String continuationSequenceNumber;

  private volatile boolean healthy = true;
  private volatile boolean finishedInitialAccountPopulation;
  private volatile boolean shouldRenewSubscription;

  @VisibleForTesting
  static final String KEY_ACCOUNT_UUID = "U";

  @VisibleForTesting
  static final String ATTR_ACCOUNT_E164 = "P";

  @VisibleForTesting
  static final String ATTR_CANONICALLY_DISCOVERABLE = "C";

  @VisibleForTesting
  static final String ATTR_PNI = "PNI";

  @VisibleForTesting
  static final String ATTR_UAK = "UAK";

  private static final ObjectMapper OBJECT_MAPPER = new ObjectMapper()
      .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
      .configure(DeserializationFeature.FAIL_ON_NULL_CREATOR_PROPERTIES, false)
      .configure(DeserializationFeature.FAIL_ON_MISSING_CREATOR_PROPERTIES, false);

  private static final int BATCH_SIZE = 4096;
  private static final Duration BATCH_TIMEOUT = Duration.ofSeconds(1);

  private static final Duration MAXIMUM_CLOCK_DRIFT = Duration.ofMinutes(1);

  private static final Logger logger = LoggerFactory.getLogger(DynamoDbAccountPopulator.class);

  public DynamoDbAccountPopulator(final Enclave enclave,
      final DynamoDbAsyncClient dynamoDbAsyncClient,
      final KinesisAsyncClient kinesisAsyncClient,
      final KinesisStreamConsumerSupplier streamConsumerSource,
      final MeterRegistry meterRegistry,
      final Clock clock,
      final AccountTableConfiguration accountTableConfiguration) {

    this.enclave = enclave;
    this.dynamoDbAsyncClient = dynamoDbAsyncClient;
    this.kinesisAsyncClient = kinesisAsyncClient;
    this.streamConsumerSource = streamConsumerSource;
    this.clock = clock;

    this.accountTableName = accountTableConfiguration.getTableName();
    this.updateStreamName = accountTableConfiguration.getStreamName();
    this.accountTableReadSegments = accountTableConfiguration.getTableReadSegments();

    this.entriesFromTableCounter =
        meterRegistry.counter(name(DynamoDbAccountPopulator.class, "entriesProcessed"), "dataSource", "table");

    this.entriesFromStreamCounter =
        meterRegistry.counter(name(DynamoDbAccountPopulator.class, "entriesProcessed"), "dataSource", "stream");
  }

  @PostConstruct
  void populateAccounts() {
    setShardId(kinesisAsyncClient.describeStream(DescribeStreamRequest.builder()
            .streamName(updateStreamName)
            .build())
        .thenApply(describeStreamResponse -> {
          if (describeStreamResponse.streamDescription().shards().size() != 1) {
            throw new IllegalStateException("Steam must have exactly one shard");
          }

          return describeStreamResponse.streamDescription().shards().get(0).shardId();
        })
        .join());

    // We want to get all the updates since we started reading the snapshot, but it's possible that our clock and
    // Kinesis' clock disagree. To compensate, we give ourselves a little padding on the start time. This may lead to
    // some duplicated events, but that's not a problem in practice.
    populateStartTime = clock.instant().minus(MAXIMUM_CLOCK_DRIFT);

    getAccountSnapshot()
        .bufferTimeout(BATCH_SIZE, BATCH_TIMEOUT)
        .doOnComplete(() -> {
          logger.info("Finished loading {} entries from account table",
              Double.valueOf(entriesFromTableCounter.count()).longValue());

          shouldRenewSubscription = true;
          this.subscribeToShard();
        })
        .doOnError(throwable -> {
          logger.error("Failed to load account snapshot", throwable);
          healthy = false;
        })
        .subscribe(entries -> {
          enclave.loadData(entries, false).join();
          entriesFromTableCounter.increment(entries.size());
        });
  }

  @PreDestroy
  void shutDown() {
    shouldRenewSubscription = false;
  }

  // Subscriptions lapse every five minutes. According to
  // https://docs.aws.amazon.com/kinesis/latest/APIReference/API_SubscribeToShard.html:
  //
  // > If you call SubscribeToShard again with the same ConsumerARN and ShardId within 5 seconds of a successful call,
  // > you'll get a ResourceInUseException. If you call SubscribeToShard 5 seconds or more after a successful call,
  // > the second call takes over the subscription and the previous connection expires or fails with a
  // > ResourceInUseException.
  //
  // That means we want to choose a refresh interval that's somewhere between 5 seconds and 5 minutes. Going with every
  // minute means that we'll get several chances to renew before the subscription lapses on its own, but won't run afoul
  // of the rate limit.
  @Scheduled(fixedDelay = "1m")
  void renewSubscription() {
    if (shouldRenewSubscription) {
      logger.debug("Renewing subscription to shard {}", shardId);
      subscribeToShard();
    }
  }

  @VisibleForTesting
  void setShardId(final String shardId) {
    this.shardId = shardId;
  }

  @VisibleForTesting
  void setShouldRenewSubscription(final boolean shouldRenewSubscription) {
    this.shouldRenewSubscription = shouldRenewSubscription;
  }

  @Override
  public boolean hasFinishedInitialAccountPopulation() {
    return finishedInitialAccountPopulation;
  }

  @Override
  public boolean isHealthy() {
    return healthy;
  }

  @VisibleForTesting
  Flux<DirectoryEntry> getAccountSnapshot() {
    final List<Flux<DirectoryEntry>> segmentPublishers = new ArrayList<>(accountTableReadSegments);

    for (int segment = 0; segment < accountTableReadSegments; segment++) {
      final ScanRequest scanRequest = ScanRequest.builder()
          .segment(segment)
          .totalSegments(accountTableReadSegments)
          .tableName(accountTableName)
          .attributesToGet(
              KEY_ACCOUNT_UUID,
              ATTR_ACCOUNT_E164,
              ATTR_CANONICALLY_DISCOVERABLE,
              ATTR_PNI,
              ATTR_UAK)
          .build();

      segmentPublishers.add(Flux.from(dynamoDbAsyncClient.scanPaginator(scanRequest).items())
          .mapNotNull(DynamoDbAccountPopulator::directoryInsertEntryFromItem));
    }

    // Shuffle the list to make it less likely that we'll have two consumers trying to read the same segments at the
    // same time
    Collections.shuffle(segmentPublishers);

    return Flux.merge(segmentPublishers);
  }

  /**
   * If the item attributes indicate the account is canonically discoverable, returns a {@code DirectoryEntry},
   * else {@code null}.
   * <br>
   * Note: should only be used for the initial directory load, when there is no point in sending a deletion entry,
   * as the entry does not exist
   */
  @Nullable
  private static DirectoryEntry directoryInsertEntryFromItem(final Map<String, AttributeValue> item) {
    final long e164 = e164FromString(item.get(ATTR_ACCOUNT_E164).s());
    final boolean canonicallyDiscoverable = item.containsKey(ATTR_CANONICALLY_DISCOVERABLE) &&
        item.get(ATTR_CANONICALLY_DISCOVERABLE).bool();

    if (canonicallyDiscoverable) {
      final byte[] aci = item.get(KEY_ACCOUNT_UUID).b().asByteArray();
      final byte[] pni = item.get(ATTR_PNI).b().asByteArray();
      final byte[] uak = item.containsKey(ATTR_UAK) && item.get(ATTR_UAK).b() != null ?
          item.get(ATTR_UAK).b().asByteArray() : null;

      return new DirectoryEntry(e164, aci, pni, uak);
    } else {
      return null;
    }
  }

  @VisibleForTesting
  static long e164FromString(final String s) {
    if (!s.startsWith("+")) {
      throw new IllegalArgumentException("e164 not prefixed with '+'");
    }

    return Long.parseLong(s, 1, s.length(), 10);
  }

  private void subscribeToShard() {
    final StartingPosition startingPosition;

    final String localContinuationSequenceNumber = continuationSequenceNumber;

    if (localContinuationSequenceNumber != null) {
      startingPosition = StartingPosition.builder()
          .type(ShardIteratorType.AFTER_SEQUENCE_NUMBER)
          .sequenceNumber(localContinuationSequenceNumber)
          .build();
    } else if (populateStartTime != null) {
      startingPosition = StartingPosition.builder()
          .type(ShardIteratorType.AT_TIMESTAMP)
          .timestamp(populateStartTime)
          .build();
    } else {
      throw new IllegalStateException(
          "Cannot subscribe to a shard without a start time or continuation sequence number");
    }

    // The "shape" of this API affordance is different from what readers might expect, particularly in contrast to
    // reading the DynamoDB table. This call doesn't produce any meaningful response on its own, but will trigger calls
    // to the various methods in `SubscribeToShardResponseHandler` (`responseReceived`, `onEventStream`,
    // `exceptionOccurred`, and `complete`). `responseReceived` and `exceptionOccurred` tell us whether this call
    // succeeded or failed, while readers might reasonably expect that information to be part of the value returned by
    // a call to `subscribeToShard`.
    logger.debug("Subscribing to shard {}", shardId);

    kinesisAsyncClient.subscribeToShard(SubscribeToShardRequest.builder()
            .shardId(shardId)
            .consumerARN(streamConsumerSource.getConsumerArn().block())
            .startingPosition(startingPosition)
            .build(),
        this)
        .whenComplete((ignored, cause) -> {
          if (cause != null) {
            logger.warn("Failed to subscribe to shard {}", shardId, cause);
          }
        });
  }

  @Override
  public void responseReceived(final SubscribeToShardResponse subscribeToShardResponse) {
    logger.debug("Subscribed to shard {}", shardId);
  }

  @Override
  public void onEventStream(final SdkPublisher<SubscribeToShardEventStream> sdkPublisher) {
    Flux.from(sdkPublisher).subscribe(event -> {
      if (event instanceof SubscribeToShardEvent subscribeToShardEvent) {
        handleSubscribeToShardEvent(subscribeToShardEvent);
      }
    });
  }

  @VisibleForTesting
  void handleSubscribeToShardEvent(final SubscribeToShardEvent event) {
    logger.trace("Received subscribeToShardEvent; records.size() = {}; millisBehindLatest = {}",
        event.records().size(), event.millisBehindLatest());

    if (!event.records().isEmpty()) {
      final List<DirectoryEntry> directoryEntries = new ArrayList<>(event.records().size());

      for (final Record record : event.records()) {
        try {
          final Account account = OBJECT_MAPPER.readValue(record.data().asUtf8String(), Account.class);

          final DirectoryEntry directoryEntry = account.canonicallyDiscoverable() ?
              new DirectoryEntry(account.e164(),
                  UUIDUtil.toByteArray(account.uuid()),
                  UUIDUtil.toByteArray(account.pni()),
                  account.uak()) :
              DirectoryEntry.deletionEntry(account.e164());

          directoryEntries.add(directoryEntry);
        } catch (final JsonProcessingException e) {
          logger.error("Discarded record {}; could not parse JSON", record.sequenceNumber(), e);
        }
      }

      if (!directoryEntries.isEmpty()) {
        enclave.loadData(directoryEntries, false).join();
        entriesFromStreamCounter.increment(directoryEntries.size());
      }
    }

    continuationSequenceNumber = event.continuationSequenceNumber();

    if (event.millisBehindLatest() == 0) {
      if (!finishedInitialAccountPopulation) {
        logger.info("Accounts synchronized after processing {} entries from account stream",
            Double.valueOf(entriesFromStreamCounter.count()).longValue());
      }

      finishedInitialAccountPopulation = true;
    }
  }

  @Override
  public void exceptionOccurred(final Throwable throwable) {
    logger.warn("Caught an exception while subscribing to or following shard {}", shardId, throwable);
  }

  @Override
  public void complete() {
    logger.debug("Subscription to stream {} complete", shardId);
  }
}
