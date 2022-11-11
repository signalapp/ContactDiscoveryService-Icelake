/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.limits.cosmos;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.azure.cosmos.ConsistencyLevel;
import com.azure.cosmos.CosmosAsyncClient;
import com.azure.cosmos.CosmosAsyncContainer;
import com.azure.cosmos.CosmosAsyncDatabase;
import com.azure.cosmos.CosmosClientBuilder;
import com.azure.cosmos.implementation.NotFoundException;
import com.azure.cosmos.models.CosmosContainerProperties;
import com.azure.cosmos.models.CosmosDatabaseResponse;
import com.azure.cosmos.models.PartitionKey;
import io.micrometer.core.instrument.simple.SimpleMeterRegistry;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CompletionException;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Disabled;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.signal.cdsi.limits.RateLimitExceededException;
import org.signal.cdsi.limits.TokenRateLimitConfiguration;
import org.testcontainers.containers.CosmosDBEmulatorContainer;
import org.testcontainers.junit.jupiter.Container;
import org.testcontainers.junit.jupiter.Testcontainers;
import org.testcontainers.utility.DockerImageName;

@Testcontainers
@Disabled
public class CosmosTokenRateLimiterEmulatorTest {

  private static final String DB_NAME = "CdsiTest";
  private static final String TABLE_NAME = "CosmosRateLimitTest";
  private static final ByteBuffer EMPTY_TOKEN = ByteBuffer.wrap(new byte[0]);

  private int token;
  private CosmosAsyncContainer container;
  private CosmosContainerProperties properties;
  private Clock clock;
  private CosmosTokenRateLimiter tokenRateLimiter;

  @TempDir
  private Path tempFolder;

  @Container
  public CosmosDBEmulatorContainer emulator = new CosmosDBEmulatorContainer(
      DockerImageName.parse("mcr.microsoft.com/cosmosdb/linux/azure-cosmos-emulator")
  );


  @BeforeEach
  void setup() throws IOException, CertificateException, KeyStoreException, NoSuchAlgorithmException {
    Path keyStoreFile = tempFolder.resolve("azure-cosmos-emulator.keystore");
    KeyStore keyStore = emulator.buildNewKeyStore();
    keyStore.store(new FileOutputStream(keyStoreFile.toFile()), emulator.getEmulatorKey().toCharArray());

    System.setProperty("javax.net.ssl.trustStore", keyStoreFile.toString());
    System.setProperty("javax.net.ssl.trustStorePassword", emulator.getEmulatorKey());
    System.setProperty("javax.net.ssl.trustStoreType", "PKCS12");

    final CosmosAsyncClient client = new CosmosClientBuilder()
        .endpointDiscoveryEnabled(false)
        .endpoint(emulator.getEmulatorEndpoint())
        .key(emulator.getEmulatorKey())
        .consistencyLevel(ConsistencyLevel.SESSION)
        .buildAsyncClient();
    final CosmosDatabaseResponse response = client.createDatabaseIfNotExists(DB_NAME).block();
    CosmosAsyncDatabase database = client.getDatabase(response.getProperties().getId());

    properties = database
        .createContainer(new CosmosContainerProperties(TABLE_NAME, CosmosTokenRateLimiter.PARTITION_KEY_PATH))
        .block()
        .getProperties();

    container = database.getContainer(properties.getId());
    token = 0;
    clock = mock(Clock.class);
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(0));
    tokenRateLimiter = new CosmosTokenRateLimiter(container, clock, conf(), new SimpleMeterRegistry(), true);
  }

  @Test
  public void testExhaustPermits() {
    ByteBuffer token1 = token();
    ByteBuffer token2 = token();
    ByteBuffer token3 = token();

    tokenRateLimiter.prepare("foo", 10, token1, token2).join();
    tokenRateLimiter.validate("foo", token2).join();

    // out of permits
    Assertions.assertThrows(RateLimitExceededException.class, () -> prepare(tokenRateLimiter, "foo", 1, token2, token3));

    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1));
    // now should have enough
    tokenRateLimiter.prepare("foo", 1, token2, token3).join();
    tokenRateLimiter.validate("foo", token3).join();
  }

  @Test
  public void testExhaustPermitsValidate() {
    ByteBuffer token1 = token();
    ByteBuffer token2 = token();
    ByteBuffer token3 = token();

    // prepare both tokens in parallel before using the rate limit (would typically only happen
    // if there are multiple clients)
    tokenRateLimiter.prepare("foo", 10, token1, token2).join();
    tokenRateLimiter.prepare("foo", 1, token1, token3).join();

    tokenRateLimiter.validate("foo", token2).join();

    // out of permits
    Assertions.assertThrows(RateLimitExceededException.class, () -> validate(tokenRateLimiter, "foo",  token3));
    // move clock forward
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

    // Request can never succeed because it's larger than the bucket limit, maps
    // to a 4003 response
    assertTrue(completionException.getCause() instanceof IllegalArgumentException);

    // token 3 should use all permits
    tokenRateLimiter.validate("foo", token3).join();

    ByteBuffer token5 = token(); // 1
    Assertions.assertThrows(RateLimitExceededException.class, () -> prepare(tokenRateLimiter, "foo", 1, token1, token5));
    when(clock.instant()).thenReturn(Instant.ofEpochSecond(1));
    tokenRateLimiter.prepare( "foo", 1, token1, token5).join();
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

  private <T> void waitForDeletion(String id, PartitionKey partitionKey, Class<T> cls) throws InterruptedException {
    while (true) {
      Thread.sleep(Duration.ofMillis(100).toMillis());
      try {
        container.readItem(id, partitionKey, cls).block();
      } catch (NotFoundException e) {
        return;
      }
    }
  }

  @Test
  public void testTtl() {

    // -1 enables TTL with a default TTL of infinity
    container.replace(properties.setDefaultTimeToLiveInSeconds(-1)).block();

    container.createItem(TokenBucket.create("test", 1.0, Instant.now().toString(), 1)).block();
    container.createItem(TokenCost.create("test", "id", 1, Instant.now().toString(), 1)).block();
    container.readItem(TokenBucket.ID, new PartitionKey("test"), TokenBucket.class).block();
    container.readItem("id", new PartitionKey("test"), TokenCost.class).block();

    assertTimeoutPreemptively(
        Duration.ofSeconds(30),
        () -> waitForDeletion(TokenBucket.ID, new PartitionKey("test"), TokenBucket.class),
        "TokenBucket was not cleaned up via expiration");

    assertTimeoutPreemptively(
        Duration.ofSeconds(30),
        () -> waitForDeletion("id", new PartitionKey("test"), TokenCost.class),
        "TokenCost was not cleaned up via expiration");
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

  private void prepare(CosmosTokenRateLimiter rateLimiter, String key, int amountDelta, ByteBuffer oldTokenHash, ByteBuffer newTokenHash) throws Throwable {
    try {
      rateLimiter.prepare(key, amountDelta, oldTokenHash, newTokenHash).join();
    } catch (CompletionException e) {
      throw e.getCause();
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
