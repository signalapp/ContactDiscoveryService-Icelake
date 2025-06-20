/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.account.aws;

import jakarta.annotation.PreDestroy;
import jakarta.inject.Singleton;
import org.signal.cdsi.util.CompletionExceptions;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;
import reactor.core.publisher.Sinks;
import reactor.core.publisher.Sinks.One;
import software.amazon.awssdk.services.kinesis.KinesisAsyncClient;
import software.amazon.awssdk.services.kinesis.model.DeregisterStreamConsumerRequest;
import software.amazon.awssdk.services.kinesis.model.DescribeStreamConsumerRequest;
import software.amazon.awssdk.services.kinesis.model.DescribeStreamRequest;
import software.amazon.awssdk.services.kinesis.model.RegisterStreamConsumerRequest;
import software.amazon.awssdk.services.kinesis.model.ResourceInUseException;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.TimeUnit;

/**
 * A Kinesis stream consumer supplier registers a Kinesis stream consumer, waits for the consumer to be ready, then
 * provides the consumer ARN when ready. At application shutdown, the supplier will make a best effort to destroy the
 * Kinesis stream consumer.
 */
@Singleton
class KinesisStreamConsumerSupplier {

  private final KinesisAsyncClient kinesisAsyncClient;

  private final String streamArn;
  private final String consumerArn;

  private final One<String> consumerArnSink = Sinks.one();

  private static final Logger logger = LoggerFactory.getLogger(KinesisStreamConsumerSupplier.class);

  public KinesisStreamConsumerSupplier(final KinesisAsyncClient kinesisAsyncClient,
      final AccountTableConfiguration accountTableConfiguration) {

    this.kinesisAsyncClient = kinesisAsyncClient;

    streamArn = kinesisAsyncClient.describeStream(DescribeStreamRequest.builder()
            .streamName(accountTableConfiguration.getStreamName())
            .build())
        .thenApply(describeStreamResponse -> describeStreamResponse.streamDescription().streamARN())
        .join();

    final String consumerName = getLocalHostname();

    consumerArn = kinesisAsyncClient.registerStreamConsumer(RegisterStreamConsumerRequest.builder()
            .consumerName(consumerName)
            .streamARN(streamArn)
            .build())
        .thenApply(registerStreamConsumerResponse -> registerStreamConsumerResponse.consumer().consumerARN())
        .exceptionallyCompose(throwable -> {
          if (CompletionExceptions.unwrap(throwable) instanceof ResourceInUseException) {
            // The consumer already exists (presumably because the application restarted)
            return kinesisAsyncClient.describeStreamConsumer(DescribeStreamConsumerRequest.builder()
                    .streamARN(streamArn)
                    .consumerName(consumerName)
                    .build())
                .thenApply(response -> response.consumerDescription().consumerARN());
          } else {
            // Something unexpected went wrong
            return CompletableFuture.failedFuture(throwable);
          }
        })
        .join();

    logger.info("Registered stream consumer {}", consumerArn);
    pollConsumerStatus();
  }

  private static String getLocalHostname() {
    try {
      return InetAddress.getLocalHost().getHostName().toLowerCase(Locale.US);
    } catch (final UnknownHostException e) {
      logger.warn("Failed to get hostname", e);
      return "unknown-" + UUID.randomUUID();
    }
  }

  /**
   * Returns a {@code Mono} that yields the ARN of this application instance's Kinesis stream consumer when the consumer
   * has been created and is ready.
   *
   * @return a {@code Mono} that yields the ARN of this application instance's Kinesis stream consumer
   */
  public Mono<String> getConsumerArn() {
    return consumerArnSink.asMono();
  }

  private void pollConsumerStatus() {
    kinesisAsyncClient.describeStreamConsumer(DescribeStreamConsumerRequest.builder()
            .consumerARN(consumerArn)
            .build())
        .thenAccept(response -> {
          switch (response.consumerDescription().consumerStatus()) {
            case ACTIVE -> {
              logger.debug("Stream consumer {} ready", consumerArn);
              consumerArnSink.tryEmitValue(consumerArn);
            }
            case CREATING -> {
              logger.debug("Waiting for consumer to be ready");
              CompletableFuture.runAsync(this::pollConsumerStatus,
                  CompletableFuture.delayedExecutor(1, TimeUnit.SECONDS));
            }
            case DELETING -> {
              logger.warn("Consumer is being deleted and will likely never be ready");
              consumerArnSink.tryEmitError(new IllegalStateException("Consumer deleted"));
            }
            case UNKNOWN_TO_SDK_VERSION -> {
              logger.error("Consumer state unrecognized by AWS SDK");
              consumerArnSink.tryEmitError(new IllegalStateException("Unrecognized consumer state"));
            }
          }
        });
  }

  @PreDestroy
  void deregisterConsumer() {
    kinesisAsyncClient.deregisterStreamConsumer(DeregisterStreamConsumerRequest.builder()
            .consumerARN(consumerArn)
            .build())
        .whenComplete((response, cause) -> {
          if (cause != null) {
            logger.error("Failed to de-register stream consumer {}", consumerArn, cause);
          } else {
            logger.info("De-registered stream consumer {}", consumerArn);
          }
        })
        .join();
  }
}
