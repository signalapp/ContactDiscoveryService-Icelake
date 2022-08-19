/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.azure.cosmos.ConsistencyLevel;
import com.azure.cosmos.CosmosAsyncContainer;
import com.azure.cosmos.CosmosAsyncDatabase;
import com.azure.cosmos.CosmosClientBuilder;
import com.azure.cosmos.implementation.NotFoundException;
import com.azure.cosmos.models.PartitionKey;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CompletionException;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Disabled;
import org.junit.jupiter.api.Test;
import org.signal.cdsi.limits.TokenRateLimitConfiguration;
import org.signal.cdsi.limits.RateLimitExceededException;

/**
 * You can run this suite by following <a href="https://docs.microsoft.com/en-us/azure/cosmos-db/local-emulator?tabs=ssl-netstd21">
 * these instructions</a> to set up the cosmosdb emulator. This includes adding the self-signed cert from the emulator
 * to a keystore and providing the path to it below in system properties
 */
@Disabled("This test requires you to manually set up the cosmosdb emulator")
public class CosmosTokenRateLimiterEmulatorTest {

  static {
    System.setProperty("javax.net.ssl.trustStore", "/home/ravi/cacerts");
    System.setProperty("javax.net.ssl.trustStorePassword", "password");
  }

  private static final String DB_NAME = "CdsiTest";
  private static final String TABLE_NAME = "CosmosRateLimitTest";
  // not a secret, publicly known key for the cosmosdb emulator
  private static final String KEY = "C2y6yDjf5/R+ob0N8A7Cgv30VRDJIWEHLM+4QDU5DE2nQ9nDuVTqobD4b8mGGyPMbIZnqyMsEcaGQy67XIw/Jw==";
  private static final String ENDPOINT = "https://localhost:8081";
  private static final ByteBuffer EMPTY_TOKEN = ByteBuffer.wrap(new byte[0]);

  private int token;
  private CosmosAsyncContainer container;
  private Clock clock;
  private CosmosTokenRateLimiter tokenRateLimiter;


  @BeforeEach
  void setup() {
    var client = new CosmosClientBuilder()
        .endpoint(ENDPOINT)
        .key(KEY)
        .consistencyLevel(ConsistencyLevel.SESSION)
        .buildAsyncClient();

    CosmosAsyncDatabase database = client.getDatabase(
        client.createDatabaseIfNotExists(DB_NAME).block().getProperties().getId());

    database.delete().block();
    database = client.getDatabase(client.createDatabaseIfNotExists(DB_NAME).block().getProperties().getId());
    container = database.getContainer(
        database.createContainer(TABLE_NAME, CosmosTokenRateLimiter.PARTITION_KEY_PATH).block().getProperties()
            .getId());
    token = 0;

    clock = mock(Clock.class);
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(0));
    tokenRateLimiter = new CosmosTokenRateLimiter(container, clock, conf(), new SimpleMeterRegistry(), true);

  }

  private ByteBuffer token() {
    return ByteBuffer.allocate(4).putInt(token++).flip();
  }

  private static TokenRateLimitConfiguration conf() {
    TokenRateLimitConfiguration conf = new TokenRateLimitConfiguration();
    conf.setBucketSize(10);
    conf.setLeakRateDuration(Duration.ofSeconds(1));
    conf.setLeakRateScalar(1);
    return conf;
  }

  private void validate(CosmosTokenRateLimiter rateLimiter, String key, ByteBuffer token) throws Throwable {
    try {
      rateLimiter.validate(key, token).join();
    } catch (CompletionException e) {
      throw e.getCause();
    }
  }

  @Test
  public void testExhaustPermits() {
    ByteBuffer token1 = token();
    ByteBuffer token2 = token();
    ByteBuffer token3 = token();

    tokenRateLimiter.prepare("foo", 10, token1, token2).join();
    tokenRateLimiter.validate("foo", token2).join();

    tokenRateLimiter.prepare("foo", 1, token2, token3).join();

    // out of permits
    Assertions.assertThrows(RateLimitExceededException.class, () -> validate(tokenRateLimiter, "foo", token3));

    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1));
    // now should have enough
    tokenRateLimiter.validate("foo", token3).join();
  }

  @Test
  public void testChainedCosts() {
    ByteBuffer token1 = token(); // free
    ByteBuffer token2 = token(); // 9
    ByteBuffer token3 = token(); // 9 + 1
    ByteBuffer token4 = token(); // 9 + 1 + 1 (larger than bucket limit)

    tokenRateLimiter.prepare("foo", 9, token1, token2).join();
    tokenRateLimiter.prepare("foo", 1, token2, token3).join();

    final CompletionException completionException = assertThrows(CompletionException.class,
        () -> tokenRateLimiter.prepare("foo", 1, token3, token4).join(),
        "Token 4 should be rejected");

    assertTrue(completionException.getCause() instanceof IOException);

    // token 3 should use all permits
    tokenRateLimiter.validate("foo", token3).join();

    ByteBuffer token5 = token(); // 1
    tokenRateLimiter.prepare("foo", 1, token1, token5).join();
    Assertions.assertThrows(RateLimitExceededException.class, () -> validate(tokenRateLimiter, "foo", token5));
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1));
    // now should have enough
    tokenRateLimiter.validate("foo", token5).join();
  }

  @Test
  public void testGCOldTokens() {
    // create some tokens we won't use
    List<ByteBuffer> tokens = Stream.generate(this::token).limit(10).toList();
    for (ByteBuffer token : tokens) {
      tokenRateLimiter.prepare("foo", 1, EMPTY_TOKEN, token).join();
    }

    // prepare and use a token - haven't leaked any permits so nothing should be gced
    ByteBuffer usedToken = token();
    tokenRateLimiter.prepare("foo", 5, tokens.get(tokens.size() - 1), usedToken).join();
    assertEquals(tokens.size() + 1, countTokens("foo"));

    tokenRateLimiter.validate("foo", usedToken).join();
    assertEquals(countTokens("foo"), tokens.size());

    // advance the clock by 10. We used 5+1 tokens during that period, so there should be 4 tokens we can GC
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(10));

    // this should delete 4 of the 10 tokens and use all of the current bucket capacity
    ByteBuffer usedToken2 = token();
    tokenRateLimiter.prepare("foo", 10, EMPTY_TOKEN, usedToken2).join();
    assertEquals((tokens.size() - 4) + 1, countTokens("foo"));
    tokenRateLimiter.validate("foo", usedToken);

    // advance by 16, so we can delete the remaining 6 tokens
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(26));
    usedToken = token();
    tokenRateLimiter.prepare("foo", 10, EMPTY_TOKEN, usedToken).join();
    tokenRateLimiter.validate("foo", usedToken).join();
    assertEquals(0, countTokens("foo"));
  }

  @Test
  public void testGCMoreThanBatchSize() {
    final int maxTokensPerGc = 99;
    // create a lot of tokens we won't use (cosmos max batch size is 100)
    List<ByteBuffer> tokens = Stream.generate(this::token).limit(5 * maxTokensPerGc).collect(Collectors.toList());
    for (ByteBuffer token : tokens) {
      tokenRateLimiter.prepare("foo", 1, EMPTY_TOKEN, token).join();
    }

    // none of them should be gc-able yet
    assertEquals(tokens.size(), countTokens("foo"));

    // advance the clock enough that these can all be gced
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(500));

    int expectedTokens = tokens.size();
    while (expectedTokens > 2) {
      // prepare and use a token
      tokens.add(this.token());
      expectedTokens += 1;
      tokenRateLimiter.prepare("foo", 0, tokens.get(tokens.size() - 2), tokens.get(tokens.size() - 1)).join();
      expectedTokens = Math.max(expectedTokens - maxTokensPerGc, 2);
      assertEquals(expectedTokens, countTokens("foo"));
    }
    assertEquals(2, countTokens("foo"));
  }

  @Test
  public void testFinishedGcResetsGcts() {
    ByteBuffer token1 = this.token();
    ByteBuffer token2 = this.token();
    tokenRateLimiter.prepare("foo", 9, EMPTY_TOKEN, token1).join();
    tokenRateLimiter.prepare("foo", 1, token1, token2).join();

    // shouldn't have gced anything yet
    assertEquals(2, countTokens("foo"));

    tokenRateLimiter.validate("foo", token2).join();

    // token 1 should still be around
    assertEquals(1, countTokens("foo"));

    // should be able to gc token 1 now
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1000));
    tokenRateLimiter.prepare("foo", 1, EMPTY_TOKEN, token()).join();
    assertEquals(1, countTokens("foo"));

    // even though we moved the clock forward a bunch, we should have emptied out our gc permits since
    // we cleaned all eligible tokens during the last garbage collection
    tokenRateLimiter.prepare("foo", 1, EMPTY_TOKEN, token()).join();
    assertEquals(2, countTokens("foo"));

    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1002));
    // now we should be able to clean up
    tokenRateLimiter.prepare("foo", 1, EMPTY_TOKEN, token()).join();
    assertEquals(1, countTokens("foo"));
  }

  @Test
  public void testTtl() throws InterruptedException {
    container.createItem(TokenBucket.create("test", 1.0, Instant.now().toString(), 1)).block();
    container.createItem(TokenCost.create("test", "id", 1, Instant.now().toString(), 1)).block();
    container.readItem(TokenBucket.ID, new PartitionKey("test"), TokenBucket.class).block();
    container.readItem("id", new PartitionKey("test"), TokenCost.class).block();
    Thread.sleep(1000 * 5);
    try {
      container.readItem(TokenBucket.ID, new PartitionKey("test"), TokenBucket.class).block();
      Assertions.fail("Item should have expired");
    } catch (NotFoundException e) {
      // expected
    }
    try {
      container.readItem("id", new PartitionKey("test"), TokenCost.class).block();
      Assertions.fail("Item should have expired");
    } catch (NotFoundException e) {
      // expected
    }
  }

  private long countTokens(String partitionKey) {
    return container.readAllItems(new PartitionKey(partitionKey), TokenCost.class)
        .filter(cost -> !cost.getId().equals(TokenBucket.ID))
        .collect(Collectors.toList())
        .doOnSuccess(System.out::println)
        .block().size();
  }
}
