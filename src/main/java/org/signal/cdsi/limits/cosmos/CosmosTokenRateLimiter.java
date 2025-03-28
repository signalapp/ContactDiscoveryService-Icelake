/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import static org.signal.cdsi.metrics.MetricsUtil.name;

import com.azure.cosmos.ConsistencyLevel;
import com.azure.cosmos.CosmosAsyncContainer;
import com.azure.cosmos.CosmosException;
import com.azure.cosmos.implementation.ConflictException;
import com.azure.cosmos.implementation.GoneException;
import com.azure.cosmos.implementation.InternalServerErrorException;
import com.azure.cosmos.implementation.NotFoundException;
import com.azure.cosmos.implementation.RequestEntityTooLargeException;
import com.azure.cosmos.implementation.RequestRateTooLargeException;
import com.azure.cosmos.implementation.RequestTimeoutException;
import com.azure.cosmos.implementation.ServiceUnavailableException;
import com.azure.cosmos.models.CosmosBatch;
import com.azure.cosmos.models.CosmosBatchItemRequestOptions;
import com.azure.cosmos.models.CosmosBatchRequestOptions;
import com.azure.cosmos.models.CosmosBatchResponse;
import com.azure.cosmos.models.CosmosItemRequestOptions;
import com.azure.cosmos.models.CosmosItemResponse;
import com.azure.cosmos.models.CosmosQueryRequestOptions;
import com.azure.cosmos.models.PartitionKey;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import io.micrometer.core.instrument.DistributionSummary;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Timer;
import io.micrometer.core.instrument.Timer.Sample;
import io.micronaut.core.annotation.Creator;
import jakarta.inject.Singleton;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.format.DateTimeParseException;
import java.util.Base64;
import java.util.Comparator;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import org.signal.cdsi.limits.RateLimitExceededException;
import org.signal.cdsi.limits.RateLimits;
import org.signal.cdsi.limits.TokenRateLimitConfiguration;
import org.signal.cdsi.limits.TokenRateLimiter;
import org.signal.cdsi.util.CompletionExceptions;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;

/**
 * Provides a rate limiter for tokens backed by a cosmosdb container
 * <p>
 * The container stores two types of records: {@link TokenCost}: Stores the cost of a token before it is returned to the
 * user. If no token cost exists, that token is free {@link TokenBucket}: A leaky bucket per user to enforce rate
 * limits
 * <p>
 * All items in the container have an aci that serves as a partition key
 *
 * @see org.signal.cdsi.limits.TokenRateLimiter
 */
@Singleton
public class CosmosTokenRateLimiter implements TokenRateLimiter {

  private static final Logger logger = LoggerFactory.getLogger(CosmosTokenRateLimiter.class);
  static final String PARTITION_KEY_PATH = "/key";
  private static final int MAX_COSMOS_BATCH_SIZE = 100;

  private final Clock clock;
  private final TokenRateLimitConfiguration configuration;
  private final CosmosAsyncContainer container;
  private final MeterRegistry meterRegistry;
  private final boolean gcOldTokens;

  private final static String validateCounterName = name(CosmosTokenRateLimiter.class, "validate");
  private final static String tokenGcCounterName = name(CosmosTokenRateLimiter.class, "tokenGc");

  private final DistributionSummary userTokenCountDist;

  private final Timer tokenGcTimer;
  private final Timer prepareTimer;
  private final Timer validateTimer;

  @Creator
  public CosmosTokenRateLimiter(
      CosmosAsyncContainer container,
      Clock clock,
      TokenRateLimitConfiguration configuration,
      MeterRegistry meterRegistry) {
    this(container, clock, configuration, meterRegistry, true);
  }

  @VisibleForTesting
  CosmosTokenRateLimiter(
      CosmosAsyncContainer container,
      Clock clock,
      TokenRateLimitConfiguration configuration,
      MeterRegistry meterRegistry,
      boolean gcOldTokens) {
    this.clock = clock;
    this.configuration = configuration;
    this.container = container;
    this.meterRegistry = meterRegistry;
    this.gcOldTokens = gcOldTokens;

    userTokenCountDist = DistributionSummary.builder(name(getClass(), "userTokenCount"))
        .distributionStatisticExpiry(Duration.ofHours(2))
        .register(meterRegistry);
    tokenGcTimer = meterRegistry.timer(name(getClass(), "tokenGcTimer"));
    prepareTimer = meterRegistry.timer(name(getClass(), "prepare"));
    validateTimer = meterRegistry.timer(name(getClass(), "validate"));
  }

  /**
   * Build a transaction component that will update the rate limit table when executed
   *
   * @param key   the bucket to update
   * @param delta the amount to add to the bucket
   * @return a stage that will complete with the transaction component when ready, or a {@link
   * RateLimitExceededException} if the amount is more than available in the bucket
   */
  private Mono<CosmosBatch> buildBucketUpdate(final String key, final int delta) {
    final CosmosBatch batch = CosmosBatch.createCosmosBatch(new PartitionKey(key));
    Instant now = clock.instant();
    return container.readItem(TokenBucket.ID, new PartitionKey(key), new CosmosItemRequestOptions(), TokenBucket.class)
        .map(response -> {
          // bucket was present, update it
          TokenBucket bucket = Objects.requireNonNull(response.getItem());
          Instant lastUpdated = Optional.ofNullable(bucket.getTs())
              .map(Instant::parse)
              .orElse(Instant.EPOCH);
          double priorSize = bucket.getAmount();
          try {
            double newAmount = RateLimits.calculateBucketUtilization(configuration, lastUpdated, now, priorSize, delta);
            bucket.setAmount(newAmount);
            bucket.setTs(now.toString());
            // move the gcts forward by however much time it would have taken to
            // accumulate these permits
            advanceGcts(bucket, delta);
            batch.replaceItemOperation(
                TokenBucket.ID,
                bucket,
                new CosmosBatchItemRequestOptions().setIfMatchETag(response.getETag()));

            return batch;
          } catch (RateLimitExceededException e) {
            logger.debug("{} would exceed bucket limit, suggesting retry time of {}", delta, e.getRetryDuration());
            throw CompletionExceptions.wrap(e);
          }
        })
        .onErrorMap(NotFoundException.class, e -> {
          logger.error("Tried to validate a token for {} without a previous call to prepare", key);
          return e;
        });
  }

  /**
   * Check if the provided delta exceeds the bucket's current remaining limit
   *
   * @param bucket the rate limit bucket
   * @param delta the amount requested to add
   * @throws RateLimitExceededException if the bucket cannot currently accept delta
   */
  private void checkRateLimit(final TokenBucket bucket, final int delta) throws RateLimitExceededException{
    Instant now = clock.instant();
    Instant lastUpdated = Optional.ofNullable(bucket.getTs())
        .map(Instant::parse)
        .orElse(Instant.EPOCH);
    RateLimits.calculateBucketUtilization(configuration, lastUpdated, now, bucket.getAmount(), delta);
  }

  @Override
  public CompletableFuture<Void> prepare(final String key, final int amountDelta, final ByteBuffer oldTokenHash,
      final ByteBuffer newTokenHash) {
    Preconditions.checkArgument(newTokenHash.hasRemaining());
    final Instant now = clock.instant();
    final Sample sample = Timer.start();
    String oldTokenId = base64Encode(oldTokenHash);
    String newTokenId = base64Encode(newTokenHash);
    Preconditions.checkArgument(!Objects.equals(newTokenId, TokenBucket.ID),
        "token cannot be the bucket identifier constant");

    // just need a final reference to the TokenBucket later on in the pipeline
    final TokenBucket[] bucketRef = new TokenBucket[1];

    // read the token bucket
    return container.readItem(
            TokenBucket.ID,
            new PartitionKey(key),
            new CosmosItemRequestOptions().setConsistencyLevel(ConsistencyLevel.STRONG),
            TokenBucket.class)
        .onErrorResume(NotFoundException.class, e -> {
          // there wasn't an existing bucket, create a bucket with no rate limit used
          logger.trace("Creating empty token bucket for {} on first use", key);
          final TokenBucket bucket = TokenBucket.create(key, 0.0, now.toString(), getTtl());
          return container.createItem(bucket, new PartitionKey(key),
              new CosmosItemRequestOptions().setContentResponseOnWriteEnabled(true));
        })
        // attempt to clean up any old tokens
        .flatMap(bucketResponse -> {
          // save off the TokenBucket for later
          bucketRef[0] = bucketResponse.getItem();
          return tryGarbageCollect(bucketResponse, key, oldTokenId, now);
        })
        .then(getNewTokenCost(key, oldTokenId, amountDelta))
        .flatMap(requestSize -> {
          logger.trace("Computed cost for new token {} is {}", KeyToken.of(key, newTokenId), requestSize);
          if (requestSize > configuration.getBucketSize()) {
            logger.warn(
                "Will not prepare token: request size {} is more than the configured bucket limit, can never succeed",
                requestSize);
            return Mono.error(CompletionExceptions.wrap(new IllegalArgumentException("request too large")));
          }
          if (requestSize == 0) {
            logger.debug("Skipping update for {} since token cost is 0", KeyToken.of(key, newTokenId));
            // this token doesn't cost anything, so we don't need to
            // put anything in token storage
            return Mono.empty();
          }
          try {
            // check if the request would be rate limited without actually using the rate limit
            checkRateLimit(bucketRef[0], requestSize);
          } catch (RateLimitExceededException e) {
            meterRegistry.counter(validateCounterName, "outcome", "requestWouldExceedRateLimit").increment();
            return Mono.error(e);
          }
          final TokenCost cost = TokenCost.create(key, newTokenId, requestSize, now.toString(), getTtl());

          // fails if newTokenId somehow already exists (would be a bug)
          return this.container.createItem(cost, new PartitionKey(cost.getKey()), new CosmosItemRequestOptions());
        })
        .onErrorMap(CosmosException.class, CosmosTokenRateLimiter::marshal)
        .doOnError(e -> !(CompletionExceptions.unwrap(e) instanceof RateLimitExceededException), e ->
            logger.warn("Failed to persist token cost for {}", KeyToken.of(key, newTokenId), e))
        .doFinally(ignore -> sample.stop(prepareTimer))
        .toFuture().thenApply(ignored -> null);
  }

  /**
   * @return the amount of permits leaked between two instances
   */
  private double leakSince(Instant before, Instant now) {
    final Duration bucketLifetime = Duration.between(before, now);
    final long leakCount = bucketLifetime.toSeconds() / configuration.getLeakRateDuration().toSeconds();
    return leakCount * configuration.getLeakRateScalar();
  }

  /**
   * Move the gcts forward by the amount of time it takes to leak delta permits.
   * This effectively reduces the amount of permits available to a subsequent GC
   * by delta.
   *
   * @param bucket to update
   * @param delta amount of permits used
   */
  private void advanceGcts(TokenBucket bucket, long delta) {
    Instant lastUpdated = Optional.ofNullable(bucket.getTs())
        .map(Instant::parse)
        .orElse(Instant.EPOCH);
    Instant oldGcts = Optional.ofNullable(bucket.getGcts()).map(Instant::parse).orElse(lastUpdated);
    long advanceSecs = (delta + configuration.getLeakRateScalar() - 1) / configuration.getLeakRateScalar();
    bucket.setGcts(oldGcts.plusSeconds(advanceSecs).toString());
  }

  /**
   * Try to parse a string Instant
   */
  private Optional<Instant> parseInstant(final String ts) {
    try {
      return Optional.of(Instant.parse(ts));
    } catch (DateTimeParseException e) {
      logger.warn("Invalid timestamp {}", ts);
      return Optional.empty();
    }
  }

  /**
   * Calculate the number of permits we can use to drop old tokens
   * <p>
   * GC will only use permits that the user hasn't used. Conceptually, these are the permits that accumulate while the
   * leaky bucket is "empty"
   *
   * @param bucket current bucket state
   * @param now    current instant
   * @return how many permits we can use
   */
  private double calculateGcPermits(final TokenBucket bucket, final Instant now) {
    final Instant gcts = Instant.parse(bucket.getGcts());
    final double gcPermits = leakSince(gcts, now);
    return Math.max(0, gcPermits);
  }

  /**
   * Remove old tokens associated with key using spare rate limit capacity
   *
   * @param key      the partition key where the bucket/tokens exist
   * @param oldToken the current predecessor token (will not be collected)
   * @param now      current instant
   * @return Mono that completes (with empty) when the collection attempt has finished
   */
  @VisibleForTesting
  Mono<Void> tryGarbageCollect(final CosmosItemResponse<TokenBucket> bucketResponse, final String key,
      final String oldToken, final Instant now) {

    if (!gcOldTokens) {
      logger.trace("Skipping gc (not enabled)");
      return Mono.empty();
    }
    logger.trace("Successfully read tokenBucket for {}, initiating garbage collection", key);
    final Sample sample = Timer.start();
    return garbageCollect(
        bucketResponse,
        tokenCost -> !tokenCost.getId().equals(oldToken) && !tokenCost.getId().equals(TokenBucket.ID), now)
        // can ignore errors, shouldn't fail operation because of a gc failure
        .onErrorResume(e -> {
          logger.info("Failed to garbage collect partition {}, skipping", key, e);
          return Mono.empty();
        })
        .doFinally(ignored -> sample.stop(tokenGcTimer))
        .then();
  }

  private Mono<CosmosBatchResponse> garbageCollect(CosmosItemResponse<TokenBucket> bucketResponse,
      Predicate<TokenCost> tokenFilter, Instant now) {
    final TokenBucket bucket = bucketResponse.getItem();
    final double gcPermits = calculateGcPermits(bucket, now);
    if (gcPermits <= 0) {
      logger.debug("No permits available to use for garbage collection");
      return Mono.empty();
    }

    // list all items under the key, skipping the TokenBucket and the current predecessor token
    return this.container.readAllItems(new PartitionKey(bucket.getKey()), new CosmosQueryRequestOptions(),
            TokenCost.class)
        .filter(tokenFilter)
        .collect(Collectors.toList())
        .flatMap(tokens -> {
          logger.trace(
              "discovered {} tokens in partition {} potentially eligible to be garbage collected. Can use {} permits",
              tokens.size(), bucket.getKey(), gcPermits);

          userTokenCountDist.record(tokens.size());

          // oldest tokens first
          tokens.sort(Comparator.comparing(c -> parseInstant(c.getTs()).orElse(Instant.EPOCH)));

          final CosmosBatch batch = CosmosBatch.createCosmosBatch(new PartitionKey(bucket.getKey()));
          long use = 0;
          int maxToDelete = MAX_COSMOS_BATCH_SIZE - 1;
          for (int i = 0; i < tokens.size() && batch.getOperations().size() < maxToDelete; i++) {
            TokenCost token = tokens.get(i);
            if (use + token.getCost() <= gcPermits) {
              use += token.getCost();
              batch.deleteItemOperation(token.getId());
            }
          }

          // not enough gc permits to collect anything
          if (use == 0) {
            return Mono.empty();
          }

          long numGcTokens = batch.getOperations().size();
          if (numGcTokens == maxToDelete && tokens.size() > batch.getOperations().size()) {
            logger.warn("Only garbage collecting {}/{} old tokens, because more would exceed batch limit",
                batch.getOperations().size(), tokens.size());
          }
          logger.debug("Garbage collecting {} tokens in partition {} using {} spare permits",
              numGcTokens, bucket.getKey(), use);

          if (numGcTokens == tokens.size()) {
            // To deal with the case where any old/underutilized account gets a bunch of
            // "free" gc permits, when the user is all "caught up" on garbage collection
            // we move their timestamp up to the present
            logger.trace("GC will clear all old tokens, advancing gc timestamp to now");
            bucket.setGcts(now.toString());
          } else {
            advanceGcts(bucket, use);
          }
          batch.replaceItemOperation(TokenBucket.ID, bucket,
              new CosmosBatchItemRequestOptions().setIfMatchETag(bucketResponse.getETag()));
          return container.executeCosmosBatch(batch)
              .doFinally(signalType ->
                meterRegistry.counter(tokenGcCounterName, "outcome", switch (signalType) {
                  case ON_COMPLETE -> "success";
                  case ON_ERROR -> "failed";
                  default -> "unknown";
                }).increment());
          });
  }


  @Override
  public CompletableFuture<Integer> validate(final String key, final ByteBuffer tokenHash) {
    final Sample sample = Timer.start();
    String tokenId = base64Encode(tokenHash);
    final int[] spent = new int[1];
    return this.container
        // read the weight of this token from token storage
        // because we were the ones that wrote this (in prepare), session
        // level read consistency is sufficient
        .readItem(
            tokenId,
            new PartitionKey(key),
            new CosmosItemRequestOptions(),
            TokenCost.class)

        // if the token doesn't have a cost, it's free! can immediately succeed (will short circuit the rest)
        .onErrorResume(NotFoundException.class, e -> {
          logger.debug("{} did not have a stored value, will not charge", KeyToken.of(key, tokenId));
          return Mono.empty();
        })

        // build the update for the rate limit bucket
        // this will immediately fail if the request
        // would go over our limit
        .flatMap(get -> {
          spent[0] =  Math.toIntExact(get.getItem().getCost());
          return this.buildBucketUpdate(key, spent[0]);
        })

        // In one transaction, delete the token (making this request free
        // in the future) and deplete the rate limit by the cost of the token
        .flatMap(batch -> {
          // delete the token cost as part of the batch
          batch.deleteItemOperation(tokenId);
          return this.container.executeCosmosBatch(batch, new CosmosBatchRequestOptions());
        })

        .doOnError(ex -> {
          ex = CompletionExceptions.unwrap(ex);
          if (ex instanceof RateLimitExceededException) {
            meterRegistry.counter(validateCounterName, "outcome", "rateLimitExceeded").increment();
          } else if (ex instanceof ConflictException) {
            // Failed due to a RMW conflict. The client should be told to
            // immediately retry
            logger.info("Failed to update rate limit for {} due to read-then-write lock conflict",
                KeyToken.of(key, tokenId));
            meterRegistry.counter(validateCounterName, "outcome", "updateConflict").increment();
          } else {
            meterRegistry.counter(validateCounterName, "outcome", "error").increment();
            logger.error("Failed to update rate limit for {}", KeyToken.of(key, tokenId), ex);
          }
        })
        .onErrorMap(CosmosException.class, CosmosTokenRateLimiter::marshal)
        .doOnSuccess(ignore -> meterRegistry.counter(validateCounterName, "outcome", "success").increment())
        .doFinally(ignored -> sample.stop(validateTimer))
        .toFuture()
        .thenApply(ignored -> spent[0]);
  }

  private Mono<Integer> getNewTokenCost(final String userId, String oldTokenId, final int amountDelta) {
    logger.trace("Getting token cost for {}", KeyToken.of(userId, oldTokenId));
    if (oldTokenId.isEmpty()) {
      return Mono.just(amountDelta);
    }
    return container.readItem(
            oldTokenId,
            new PartitionKey(userId),
            new CosmosItemRequestOptions().setConsistencyLevel(ConsistencyLevel.STRONG),
            TokenCost.class)
        .map(response -> {
          TokenCost cost = response.getItem();
          logger.trace("Old token {} exists, has cost {}", KeyToken.of(userId, oldTokenId), cost.getCost());
          return Math.toIntExact(cost.getCost() + amountDelta);
        })
        .onErrorReturn(NotFoundException.class, amountDelta);
  }

  @VisibleForTesting
  static String base64Encode(final ByteBuffer tokenHash) {
    byte[] bs = new byte[tokenHash.remaining()];
    tokenHash.duplicate().get(bs);
    // cosmosdb forbids /\\?# from appearing in the id field, so we use the url-safe base64 variant
    return Base64.getUrlEncoder().withoutPadding().encodeToString(bs);
  }

  private int getTtl() {
    // the ttl is how long it would take to leak a full bucket of permits
    // if the entry hasn't been modified in that long, we can just delete it because
    // buckets start out empty.
    double ttlSecs = Math.ceil((double) configuration.getBucketSize() / configuration.getLeakRateScalar()) * configuration.getLeakRateDuration().toSeconds();
    return (int) Math.min(ttlSecs, Integer.MAX_VALUE);
  }

  private static RuntimeException marshal(final CosmosException ex) {
    if (ex instanceof RequestRateTooLargeException
        || ex instanceof RequestTimeoutException
        || ex instanceof RequestEntityTooLargeException) {
      return new CompletionException(new IOException("Resource exhausted", ex));
    }
    if (ex instanceof ConflictException) {
      return new CompletionException(new IOException("Read-then-write lock conflict", ex));
    }
    if (ex instanceof InternalServerErrorException
        || ex instanceof GoneException
        || ex instanceof ServiceUnavailableException) {
      return new CompletionException(new IOException("Service unavailable", ex));
    }

    // otherwise, this is a serious unexpected error from cosmos
    return ex;
  }


  // key/token tuple for logging
  record KeyToken(String key, String tokenHashId) {

    @Override
    public String toString() {
      return "{ key = " + key
          + ", token = " + tokenHashId
          + "}";
    }

    static KeyToken of(String key, String tokenHash) {
      return new KeyToken(key, tokenHash);
    }

  }
}
