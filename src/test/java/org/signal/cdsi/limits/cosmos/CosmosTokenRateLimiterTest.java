/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.signal.cdsi.limits.cosmos.CosmosTokenRateLimiter.base64Encode;

import com.azure.cosmos.CosmosAsyncContainer;
import com.azure.cosmos.implementation.ConflictException;
import com.azure.cosmos.implementation.NotFoundException;
import com.azure.cosmos.models.CosmosBatch;
import com.azure.cosmos.models.CosmosItemOperation;
import com.azure.cosmos.models.CosmosItemOperationType;
import com.azure.cosmos.models.CosmosItemResponse;
import com.azure.cosmos.models.PartitionKey;
import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.concurrent.CompletionException;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentMatcher;
import org.signal.cdsi.limits.RateLimitExceededException;
import org.signal.cdsi.limits.TokenRateLimitConfiguration;
import org.signal.cdsi.util.CompletionExceptions;
import reactor.core.publisher.Mono;

public class CosmosTokenRateLimiterTest {

  private static final String KEY = "user1";
  private static final String ETAG = "boop";
  private static final int BUCKET_SIZE = 10;

  private int token = 0;
  private CosmosAsyncContainer container;
  private Clock clock;
  private CosmosTokenRateLimiter cosmosTokenRateLimiter;

  @BeforeEach
  public void setup() {
    container = mock(CosmosAsyncContainer.class);
    clock = mock(Clock.class);
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(0));
    TokenRateLimitConfiguration conf = new TokenRateLimitConfiguration();
    conf.setBucketSize(BUCKET_SIZE);
    conf.setLeakRateDuration(Duration.ofSeconds(1));
    conf.setLeakRateScalar(1);
    this.cosmosTokenRateLimiter = new CosmosTokenRateLimiter(container, clock, conf, new SimpleMeterRegistry(), false);
  }

  void mockRead(String id, TokenCost response) {
    mockRead(id, TokenCost.class, mockResponse(response));
  }

  <T> void mockRead(String id, Class<T> cls, Mono<CosmosItemResponse<T>> response) {
    when(container.readItem(eq(id), eq(new PartitionKey(KEY)), any(), eq(cls)))
        .thenReturn(response);
  }

  @Test
  public void oldTokenExists() {
    final ByteBuffer oldToken = token();
    final ByteBuffer newToken = token();

    // oldToken has cost of 5
    mockRead(base64Encode(oldToken), tokenCost(oldToken, 5));

    // existing empty bucket
    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(0)));

    Mono<CosmosItemResponse<TokenCost>> createTokenResponse = mockResponse(null);
    // should store a token with a cost of 5 + 4
    when(container.createItem(eq(tokenCost(newToken, 9)), any(), any()))
        .thenReturn(createTokenResponse);

    cosmosTokenRateLimiter.prepare(KEY, 4, oldToken, newToken).join();
  }

  @Test
  public void oldTokenAbsent() {
    final ByteBuffer oldToken = token();
    final ByteBuffer newToken = token();

    // existing empty bucket
    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(0)));

    mockRead(base64Encode(oldToken), TokenCost.class, Mono.error(new NotFoundException()));

    Mono<CosmosItemResponse<TokenCost>> createTokenResponse = mockResponse(null);
    when(container.createItem(eq(tokenCost(newToken, 6)), any(), any()))
        .thenReturn(createTokenResponse);

    cosmosTokenRateLimiter.prepare(KEY, 6, oldToken, newToken).join();
  }

  @Test
  public void prepareFreeToken() {
    final ByteBuffer oldToken = token();
    final ByteBuffer newToken = token();

    // existing empty bucket
    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(0)));

    mockRead(base64Encode(oldToken), TokenCost.class, Mono.error(new NotFoundException()));

    cosmosTokenRateLimiter.prepare(KEY, 0, oldToken, newToken).join();
    verify(container, never()).createItem(any(), any(), any());
    verify(container, never()).replaceItem(any(), any(), any());
    verify(container, never()).upsertItem(any(), any(), any());
  }

  @Test
  public void prepareTokenInitialBucket() {
    final ByteBuffer token = token();

    mockRead(TokenBucket.ID, TokenBucket.class, Mono.error(new NotFoundException()));

    // should write an empty token bucket
    final Mono<CosmosItemResponse<TokenBucket>> writeResponse = mockResponse(tokenBucket(0));
    when(container.createItem(eq(tokenBucket(0)), any(), any())).thenReturn(writeResponse);

    // should also write the token cost
    Mono<CosmosItemResponse<TokenCost>> createTokenResponse = mockResponse(null);
    when(container.createItem(eq(tokenCost(token, 5)), any(), any()))
        .thenReturn(createTokenResponse);

    cosmosTokenRateLimiter.prepare(KEY, 5, ByteBuffer.wrap(new byte[0]), token).join();
  }

  @Test
  public void spendPreparedToken() {
    final ByteBuffer token = token();

    mockRead(base64Encode(token), tokenCost(token, 4));

    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(6), ETAG));

    when(container.executeCosmosBatch(any(), any()))
        .thenReturn(Mono.empty());
    int spent = cosmosTokenRateLimiter.validate(KEY, token).join();
    assertEquals(spent, 4);

    // tx should delete token cost, and update bucket
    verify(container, times(1))
        .executeCosmosBatch(argThat(new BatchMatcher(CosmosItemOperationType.REPLACE, 10)), any());

  }

  @Test
  public void spendMissingToken() {
    final ByteBuffer token = token();

    mockRead(base64Encode(token), TokenCost.class, Mono.error(new NotFoundException()));

    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(6), ETAG));

    when(container.executeCosmosBatch(any(), any()))
        .thenReturn(Mono.empty());
    int spent = cosmosTokenRateLimiter.validate(KEY, token).join();
    assertEquals(spent, 0);

    verify(container, never()).executeCosmosBatch(any(), any());
  }

  @Test
  public void rateLimitExceededPrepare() {
    final ByteBuffer oldToken = token(); // 2
    final ByteBuffer newToken = token(); // 2 + 3

    // can only accept 4
    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(6), ETAG));
    mockRead(base64Encode(oldToken), tokenCost(oldToken, 2));

    try {
      // prepare should fail, there are only 4/5 permits available
      cosmosTokenRateLimiter.prepare(KEY, 3, oldToken, newToken).join();
      Assertions.fail("Rate limit should be exceeded");
    } catch (CompletionException e) {
      assertTrue(e.getCause() instanceof RateLimitExceededException);
      RateLimitExceededException rle = (RateLimitExceededException) e.getCause();
      // need 1 more permit, should wait 1 + 1 seconds
      assertEquals(rle.getRetryDuration().toSeconds(), 2);
    }

    // advance the clock
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1));

    // now should work (should write the token)
    Mono<CosmosItemResponse<TokenCost>> createTokenResponse = mockResponse(null);
    when(container.createItem(eq(tokenCost(newToken, 5)), any(), any()))
        .thenReturn(createTokenResponse);

    assertDoesNotThrow(() -> cosmosTokenRateLimiter.prepare(KEY, 3, oldToken, newToken).join());
    verify(container, times(1))
        .createItem(eq(tokenCost(newToken, 5)), eq(new PartitionKey(KEY)), any());
  }

  @Test
  public void rateLimitExceeded() {
    final ByteBuffer token = token();

    mockRead(base64Encode(token), tokenCost(token, 5));
    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(6), ETAG));

    try {
      // validate should fail, without doing any writes
      cosmosTokenRateLimiter.validate(KEY, token).join();
      Assertions.fail("Rate limit should be exceeded");
    } catch (CompletionException e) {
      assertTrue(e.getCause() instanceof RateLimitExceededException);
      RateLimitExceededException rle = (RateLimitExceededException) e.getCause();
      // need 1 more permit, should wait 1 + 1 seconds
      assertEquals(rle.getRetryDuration().toSeconds(), 2);
    }

    // advance the clock
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1));

    // now should work
    when(container.executeCosmosBatch(any(), any()))
        .thenReturn(Mono.empty());
    int spent = cosmosTokenRateLimiter.validate(KEY, token).join();
    assertEquals(spent, 5);

    // tx should delete token cost, and update bucket to be at 10 (was at 5, replenished 1 permit, spent 6)
    verify(container, times(1))
        .executeCosmosBatch(argThat(new BatchMatcher(CosmosItemOperationType.REPLACE, 10)), any());
  }

  @Test
  public void etagConflict() {
    final ByteBuffer token = token();

    mockRead(base64Encode(token), tokenCost(token, 4));

    mockRead(TokenBucket.ID, TokenBucket.class, mockResponse(tokenBucket(6), ETAG));

    when(container.executeCosmosBatch(any(), any()))
        .thenReturn(Mono.error(new ConflictException(null, null, null)));

    final CompletionException completionException =
        assertThrows(CompletionException.class, () -> cosmosTokenRateLimiter.validate(KEY, token).join());

    assertTrue(CompletionExceptions.unwrap(completionException) instanceof IOException);
  }

  private static class BatchMatcher implements ArgumentMatcher<CosmosBatch> {

    private final CosmosItemOperationType bucketOp;
    private final double expectedBucketAmount;
    private String mismatchReason = "";

    public BatchMatcher(final CosmosItemOperationType bucketOp, final double expectedBucketAmount) {
      this.bucketOp = bucketOp;
      this.expectedBucketAmount = expectedBucketAmount;
    }

    @Override
    public boolean matches(final CosmosBatch cosmosBatch) {
      if (cosmosBatch.getOperations().size() != 2) {
        mismatchReason = "expected a delete and an update operation";
        return false;
      }

      final long numDeletes = cosmosBatch
          .getOperations()
          .stream()
          .map(CosmosItemOperation::getOperationType)
          .filter(CosmosItemOperationType.DELETE::equals)
          .count();

      if (numDeletes != 1) {
        mismatchReason = "expected 1 delete operation";
        return false;
      }
      return cosmosBatch.getOperations()
          .stream()
          .filter(op -> op.getOperationType().equals(bucketOp))
          .findFirst()
          .map(op -> {
            final TokenBucket item = op.getItem();
            if (item.getAmount() != expectedBucketAmount) {
              mismatchReason = "expected bucketAmount " + expectedBucketAmount;
              return false;
            }
            return true;
          })
          .orElseGet(() -> {
            mismatchReason = "expected a op type of " + bucketOp;
            return false;
          });
    }

    @Override
    public String toString() {
      return mismatchReason;
    }
  }


  private TokenBucket tokenBucket(double amount) {
    return TokenBucket.create(KEY, amount, clock.instant().toString(), BUCKET_SIZE);
  }

  private TokenCost tokenCost(ByteBuffer token, long cost) {
    return TokenCost.create(KEY, base64Encode(token), cost, clock.instant().toString(), BUCKET_SIZE);
  }

  private ByteBuffer token() {
    return ByteBuffer.allocate(4).putInt(token++).flip();
  }

  private static <T> Mono<CosmosItemResponse<T>> mockResponse(T payload, String etag) {
    @SuppressWarnings("unchecked") CosmosItemResponse<T> mockResponse = mock(CosmosItemResponse.class);
    when(mockResponse.getItem()).thenReturn(payload);
    if (etag != null) {
      when(mockResponse.getETag()).thenReturn(etag);
    }
    return Mono.just(mockResponse);
  }

  private static <T> Mono<CosmosItemResponse<T>> mockResponse(T payload) {
    return mockResponse(payload, null);
  }

}
