/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.enclave;

import static org.signal.cdsi.metrics.MetricsUtil.name;

import com.google.common.annotations.VisibleForTesting;
import com.google.protobuf.ByteString;
import com.google.protobuf.InvalidProtocolBufferException;
import io.micrometer.core.instrument.DistributionSummary;
import io.micrometer.core.instrument.Gauge;
import io.micrometer.core.instrument.MeterRegistry;
import io.micrometer.core.instrument.Tags;
import io.micrometer.core.instrument.TimeGauge;
import io.micronaut.context.annotation.Context;
import io.micronaut.scheduling.annotation.Scheduled;
import jakarta.inject.Named;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;
import java.util.stream.LongStream;
import org.signal.cdsi.limits.TokenRateLimiter;
import org.signal.cdsi.proto.EnclaveLoad;
import org.signal.cdsi.proto.TableStatistics;
import org.signal.libsignal.attest.AttestationDataException;
import org.signal.libsignal.cds2.Cds2Metrics;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Context
public class Enclave implements AutoCloseable {

  private final long id;

  private final TokenRateLimiter tokenRateLimiter;
  private final MeterRegistry meterRegistry;
  private final ExecutorService jniExecutor;
  private final Clock clock;

  private final ByteString tokenSecret;
  private final int numShards;
  private final int maxOutstandingRequests;
  private final boolean omitPermitsUsed;

  private final AtomicBoolean isClosed = new AtomicBoolean(false);
  private final AtomicInteger outstandingRequestCount = new AtomicInteger(0);

  private final AtomicReference<Instant> lastAttestationTimestamp = new AtomicReference<>();
  private final AtomicInteger openClientCount;
  private final Map<String, AtomicLong> attestationMetrics = new HashMap<>();
  @VisibleForTesting
  final AtomicLong activeEntries;
  private final long capacity;

  private final DistributionSummary requestSizeDistributionSummary;

  private static final Logger logger = LoggerFactory.getLogger(Enclave.class);

  @VisibleForTesting
  static final String JNI_EXECUTOR_NAME = "enclave-jni";

  private static final String CLIENT_CREATE_TIMER_NAME = name(Enclave.class, "nativeClientCreate");
  private static final String LOAD_DATA_TIMER_NAME = name(Enclave.class, "nativeLoadData");
  private static final String HANDSHAKE_TIMER_NAME = name(Enclave.class, "nativeClientHandshake");
  private static final String RATE_LIMIT_TIMER_NAME = name(Enclave.class, "nativeClientRateLimit");
  private static final String RUN_TIMER_NAME = name(Enclave.class, "nativeClientRun");
  private static final String ATTEST_TIMER_NAME = name(Enclave.class, "nativeAttest");
  private static final String CLOSE_CLIENT_TIMER_NAME = name(Enclave.class, "nativeClientClose");
  private static final String TABLE_STATISTICS_TIMER_NAME = name(Enclave.class, "nativeEnclaveTableStatistics");

  private static final AtomicReference<String> LOADED_LIBRARY = new AtomicReference<>();

  @VisibleForTesting
  static void loadSharedLibrary(String enclaveId) throws IOException {
    String old = LOADED_LIBRARY.getAndSet(enclaveId);
    if (old == null) {
      System.load(extractResource("libjnishim-" + enclaveId + ".so").getAbsolutePath());
    } else if (!old.equals(enclaveId)) {
      throw new IllegalStateException("Attempt to load different enclave ID shared library, only one can be loaded per run of CDSI");
    }
  }

  /**
   * Constructs and initializes a new enclave instance, loading any native resources that may be needed.
   * <p/>
   * When an enclave is constructed, a JNI "shim" library is loaded from the application's resource directory. JNI shims
   * are paired with specific enclave binaries; once an enclave and its paired JNI shim have been loaded, no other
   * enclaves may be loaded in the same JVM.
   * <p/>
   * Enclaves allow multiple concurrent native calls, but the maximum number of calls is determined by the
   * {@code NumTCS} build-time parameter. The enclave uses an executor service to manage JNI calls to the enclave. The
   * executor service must not allow more than {@code NumTCS} concurrent operations. The provided executor service
   * should not be shared with other callers because the enclave will shut down the executor service when closed.
   *
   * @param enclaveConfiguration the runtime configuration for the loaded enclave
   * @param tokenRateLimiter the rate limiter for requests to the enclave
   * @param meterRegistry a registry for metrics from the enclave
   * @param jniExecutor an executor service to manage access to native enclave functions; must not allow more than
   *                    {@code NumTCS} (defined in the enclave's build configuration file) concurrent operations
   * @param clock a time source used to identify the time of the most recent successful attestation
   *
   * @throws IOException if the enclave binary or its JNI shim library could not be read for any reason
   * @throws EnclaveException if the enclave reported a problem of any kind during initialization
   */
  public Enclave(final EnclaveConfiguration enclaveConfiguration,
      final TokenRateLimiter tokenRateLimiter,
      final MeterRegistry meterRegistry,
      @Named(JNI_EXECUTOR_NAME) final ExecutorService jniExecutor,
      final Clock clock) throws IOException, EnclaveException {

    this.tokenRateLimiter = tokenRateLimiter;
    this.meterRegistry = meterRegistry;
    this.jniExecutor = jniExecutor;
    this.clock = clock;

    this.tokenSecret = ByteString.copyFrom(enclaveConfiguration.getTokenSecret(), StandardCharsets.UTF_8);
    this.maxOutstandingRequests = enclaveConfiguration.getMaxOutstandingRequests();
    this.numShards = enclaveConfiguration.getShards();
    this.omitPermitsUsed = enclaveConfiguration.isOmitPermitsUsed();

    loadSharedLibrary(enclaveConfiguration.getEnclaveId());

    this.id = nativeEnclaveInit(enclaveConfiguration.getAvailableEpcMemory(),
        enclaveConfiguration.getLoadFactor(),
        this.numShards,
        extractResource("enclave-" + enclaveConfiguration.getEnclaveId() + ".signed").getAbsolutePath(),
        enclaveConfiguration.isSimulated());

    this.requestSizeDistributionSummary = DistributionSummary.builder(name(getClass(), "requestSize"))
        .distributionStatisticExpiry(Duration.ofHours(2))
        .register(meterRegistry);
    this.openClientCount = meterRegistry.gauge(name(getClass(), "openClients"), new AtomicInteger(0));
    this.activeEntries = meterRegistry.gauge(name(getClass(), "activeEntries"), new AtomicLong(0));

    this.capacity = getCapacity(getTableStatistics().join());

    meterRegistry.gauge(name(getClass(), "entries"), this, enclave -> getEntryCount(enclave.getTableStatistics().join()));
    meterRegistry.gauge(name(getClass(), "capacity"), this, enclave -> enclave.capacity);

    // publish the server's clock time so metric consumers may compare it to expiration times in
    // the attestation metrics
    meterRegistry.more().timeGauge(
        name(getClass(), "currentTime"),
        Tags.empty(),
        this,
        TimeUnit.SECONDS,
        enclave -> enclave.clock.instant().getEpochSecond());
  }

  @Scheduled(fixedDelay = "${enclave.attestationRefreshInterval:10m}", initialDelay = "${enclave.attestationInitialDelay:0s}")
  void renewAttestation() {
    try {
      runAsync(ATTEST_TIMER_NAME, () -> {
        try {
          nativeEnclaveAttest(id);

          synchronized (lastAttestationTimestamp) {
            lastAttestationTimestamp.set(clock.instant());
            lastAttestationTimestamp.notifyAll();
          }

          // fetch the ereport we just made
          final ByteBuffer ereport = ByteBuffer.allocateDirect(32 << 10);
          nativeClientClose(id, nativeClientCreate(id, ereport));
          publishAttestationMetrics(ByteBuffer.allocate(ereport.remaining()).put(ereport).array());
        } catch (final EnclaveException | AttestationDataException e) {
          throw new CompletionException(e);
        }
      }).join();
    } catch (final CompletionException e) {
      if (e.getCause() instanceof RejectedExecutionException) {
        logger.debug("Attempted to renew attestation after enclave closure");
      } else {
        logger.warn("Failed to renew attestation", e);
      }
    }
  }

  private void publishAttestationMetrics(final byte[] ereport) throws AttestationDataException {

    // Lazily instantiate this gauge on successful attestation to avoid situations where we report a
    // nonsense value while the service is starting up and completing its first attestation.
    Gauge.builder(
            name(Enclave.class, "attestationAge"),
            () -> Duration.between(lastAttestationTimestamp.get(), clock.instant()).toMillis())
        .baseUnit("millisecond")
        .register(meterRegistry);

    final Map<String, Long> metrics = Cds2Metrics.extract(ereport);

    for (final Entry<String, Long> metric : metrics.entrySet()) {
      final String metricKey = name(Enclave.class, metric.getKey());

      if (attestationMetrics.containsKey(metricKey)) {
        // update the already registered metric
        attestationMetrics.get(metricKey).set(metric.getValue());
      } else {
        // register a metric
        AtomicLong gauge = new AtomicLong(metric.getValue());
        attestationMetrics.put(metricKey, gauge);
        if (metric.getKey().endsWith("_ts")) {
          // Interpret a metric that ends with "ts" as a timestamp. Currently,
          // all metrics from libsignal.cds2 are epoch timestamps in seconds.
          // (only used a hint for base-unit information)
          TimeGauge
              .builder(metricKey, gauge::get, TimeUnit.SECONDS)
              .register(meterRegistry);
        } else {
          Gauge.builder(metricKey, gauge::get).register(meterRegistry);
        }
      }
    }
  }


  public Optional<Instant> getLastAttestationTimestamp() {
    return Optional.ofNullable(lastAttestationTimestamp.get());
  }

  @VisibleForTesting
  public void waitForInitialAttestation() throws InterruptedException {
    synchronized (lastAttestationTimestamp) {
      while (lastAttestationTimestamp.get() == null) {
        lastAttestationTimestamp.wait();
      }
    }
  }

  @VisibleForTesting
  boolean isClosed() {
    return isClosed.get();
  }

  private static File extractResource(final String resource) throws IOException {

    final File tempFile = Files.createTempFile("resource", "").toFile();
    tempFile.deleteOnExit();

    logger.debug("Extracting resource '{}' into '{}'", resource, tempFile.getAbsolutePath());

    try (final OutputStream out = new FileOutputStream(tempFile);
        final InputStream in = Enclave.class.getResourceAsStream(resource)) {

      if (in == null) {
        throw new IOException("No such resource: " + resource);
      }

      in.transferTo(out);
    }

    return tempFile;
  }

  private static ByteBuffer direct(ByteBuffer in) {
    if (in.isDirect()) return in;
    return ByteBuffer.allocateDirect(in.remaining()).put(in).flip();
   }

  public CompletableFuture<EnclaveClient> newClient(String key) {
    return supplyAsync(CLIENT_CREATE_TIMER_NAME, () -> {
      final ByteBuffer ereport = ByteBuffer.allocateDirect(32 << 10);
      final long clientId;
      try {
        clientId = nativeClientCreate(id, ereport);
      } catch (final EnclaveException e) {
        throw new CompletionException(e);
      }

      final EnclaveClient client = new EnclaveClient(this, clientId, key, this.tokenRateLimiter, ereport);
      openClientCount.addAndGet(1);
      return client;
    });
  }

  public CompletableFuture<Void> loadData(final List<DirectoryEntry> entries, final boolean clearAll) {
    try (final ByteString.Output triples = ByteString.newOutput()) {
      entries.forEach(entry -> {
        try {
          entry.writeTo(triples);
          if (entry.isDeletion()) {
            activeEntries.decrementAndGet();
          } else {
            activeEntries.incrementAndGet();
          }
        } catch (InvalidEntryException e) {
          logger.warn("Invalid entry received", e);
        }
      });

      final EnclaveLoad load = EnclaveLoad.newBuilder()
          .setE164AciPniUakTuples(triples.toByteString())
          .setClearAll(clearAll)
          .setSharedTokenSecret(tokenSecret)
          .build();

      return runAsync(LOAD_DATA_TIMER_NAME,
          () -> {
            try {
              nativeLoadData(id, direct(load.toByteString().asReadOnlyByteBuffer()));
            } catch (final EnclaveException e) {
              throw new CompletionException(e);
            }
          });
    } catch (final IOException e) {
      // This can never happen for an in-memory ByteString
      throw new AssertionError(e);
    }
  }

  CompletableFuture<ByteBuffer> clientHandshake(final EnclaveClient client, final ByteBuffer in) {
    // see noise.h: NOISE_HANDSHAKEWRITE_SIZE
    final ByteBuffer out = ByteBuffer.allocateDirect(1632);

    return supplyAsync(HANDSHAKE_TIMER_NAME, () -> {
      try {
        nativeClientHandshake(id, client.getId(), Enclave.direct(in), out);
      } catch (final EnclaveException e) {
        throw new CompletionException(e);
      }
      return out;
    });
  }

  CompletableFuture<ByteBuffer> clientRateLimit(final EnclaveClient client, final ByteBuffer request,
      final ByteBuffer newTokenHash) {
    final ByteBuffer oldTokenHash = ByteBuffer.allocateDirect(32);
    // 32b proto field, +protofield metadata, +noise encryption overhead
    final ByteBuffer out = ByteBuffer.allocateDirect(128);

    requestSizeDistributionSummary.record(request.remaining());

    return supplyAsync(
        RATE_LIMIT_TIMER_NAME,
            () -> {
              try {
                return nativeClientRate(id, client.getId(), Enclave.direct(request), out, oldTokenHash, newTokenHash);
              } catch (final EnclaveException e) {
                throw new CompletionException(e);
              }
            })
        .thenCompose(size -> tokenRateLimiter
            .prepare(client.getRateLimitKey(), size, oldTokenHash, newTokenHash)
            .thenApply(ignore -> out));
  }

  CompletableFuture<ByteBuffer> clientRun(final EnclaveClient client, final int permitsUsed, final ByteBuffer clientAck,
      final ByteBuffer out) {
    return supplyAsync(RUN_TIMER_NAME, () -> {
          try {
            nativeClientRun(id, client.getId(), omitPermitsUsed ? 0 : permitsUsed, direct(clientAck), out);
          } catch (final EnclaveException e) {
            throw new CompletionException(e);
          }
          return out;
        });
  }

  public void close() throws EnclaveException, InterruptedException {
    jniExecutor.shutdown();

    if (!jniExecutor.awaitTermination(1, TimeUnit.MINUTES)) {
      logger.warn("Failed to shut down JNI executor after 60 seconds");
    }

    if (isClosed.compareAndSet(false, true)) {
      nativeEnclaveClose(id);
    }
  }

  CompletableFuture<Void> closeClient(long clientId) {
    return runAsync(CLOSE_CLIENT_TIMER_NAME, () -> {
      try {
        nativeClientClose(id, clientId);
      } catch (final EnclaveException e) {
        throw new CompletionException(e);
      }
      // Only decrement this if we succeeded in closing.
      openClientCount.addAndGet(-1);
    });
  }

  CompletableFuture<TableStatistics> getTableStatistics() {
    final ByteBuffer out = ByteBuffer.allocateDirect(this.numShards * 1024);

    return supplyAsync(TABLE_STATISTICS_TIMER_NAME, () -> {
      try {
        nativeEnclaveTableStatistics(id, out);
      } catch (final EnclaveException e) {
        throw new CompletionException(e);
      }
      return out;
    }).thenApply(ignored -> {
      try {
        return TableStatistics.parseFrom(out);
      } catch (final InvalidProtocolBufferException e) {
        throw new CompletionException(e);
      }
    });
  }

  private <T> CompletableFuture<T> supplyAsync(final String timerName, final Supplier<T> toWrap) {
    try {
      var out = CompletableFuture.supplyAsync(() -> meterRegistry.timer(timerName).record(toWrap), jniExecutor);
      outstandingRequestCount.addAndGet(1);
      out.whenComplete((unusedVal, unusedErr) -> outstandingRequestCount.addAndGet(-1));
      return out;
    } catch (final RejectedExecutionException e) {
      return CompletableFuture.failedFuture(e);
    }
  }

  private CompletableFuture<Void> runAsync(final String timerName, final Runnable toWrap) {
    try {
      var out = CompletableFuture.runAsync(() -> meterRegistry.timer(timerName).record(toWrap), jniExecutor);
      outstandingRequestCount.addAndGet(1);
      out.whenComplete((unusedVal, unusedErr) -> outstandingRequestCount.addAndGet(-1));
      return out;
    } catch (final RejectedExecutionException e) {
      return CompletableFuture.failedFuture(e);
    }
  }

  /**
   * Indicates whether the enclave has more pending requests than the configured maximum.
   */
  public boolean isOverloaded() {
    return outstandingRequestCount.get() >= maxOutstandingRequests;
  }

  public int getRunningShardThreadCount() {
    return Enclave.nativeGetRunningShardThreadCount(id);
  }

  @VisibleForTesting
  static long getCapacity(final TableStatistics tableStatistics) {
    return getShardStatisticValues(tableStatistics, "capacity").sum();
  }

  @VisibleForTesting
  static long getEntryCount(final TableStatistics tableStatistics) {
    return getShardStatisticValues(tableStatistics, "num_items").sum();
  }

  private static LongStream getShardStatisticValues(final TableStatistics tableStatistics, final String name) {
    return tableStatistics.getShardStatisticsList().stream()
        .map(shardStatistics -> shardStatistics.getValuesList().stream()
            .filter(value -> value.getName().equals(name))
            .findFirst())
        .filter(Optional::isPresent)
        .mapToLong(entry -> entry.get().getValue());
  }

  /**
   * Create an enclave, returning a handle (pointer, as long) to it.
   * <p>
   * This function should start the shard runners in threads as part of its operation. Future calls assume that all
   * shards are running.
   *
   * @param availableMemory   the maximum memory to use building the enclave tables, in bytes.
   * @param loadFactor Ratio of number of ORAM blocks to ORAM locations.
   * @param num_shards the number of ORAM shards/threads inside the enclave
   * @param enclavePath the path to the enclave shared library object
   * @param simulated whether the enclave should simulate attestation
   * @return handle to enclave
   */
  @VisibleForTesting
  static native long nativeEnclaveInit(long availableMemory, double loadFactor, int num_shards, String enclavePath, boolean simulated)
      throws EnclaveException;

  /** Load data into an existing enclave
   *
   * @param enclaveHandle Enclave to use
   * @param pbEnclaveLoad A serialized EnclaveLoad protobuf.
   */
  private static native void nativeLoadData(long enclaveHandle, Buffer pbEnclaveLoad)
      throws EnclaveException;

  /** Create a new client, returning a handle to it.
   *
   * @param enclaveHandle Enclave to use
   * @return handle to client
   */
  private static native long nativeClientCreate(
      long enclaveHandle,
      Buffer ereport)
      throws EnclaveException;

  /** Create a new client, returning a handle to it.
   *
   * @param enclaveHandle Enclave to use
   * @param clientHandle Client to use
   * @param handshakeStart The handshake bytes passed by the client.
   * @param outHandshakeResponse A buffer in which to write the handshake response
   */
  private static native void nativeClientHandshake(
      long enclaveHandle, long clientHandle,
      Buffer handshakeStart,
      Buffer outHandshakeResponse)
      throws EnclaveException;

  /** Compute the size of the client's request and return rate limiting information.
   *
   * @param enclaveHandle Enclave to use
   * @param clientHandle Client to use
   * @param clientRequest Noise-encrypted serialized ClientRequest
   * @param outClientToken Noise-encrypted serialized ClientResponse containing token for client
   * @param outPrevTokenHash Plaintext hash of previous token, 16B
   * @param outNewTokenHash Plaintext hash of current token, 16B
   * @return Computed size of client's request, based on client-provided token
   */
  private static native int nativeClientRate(
      long enclaveHandle, long clientHandle,
      Buffer clientRequest,
      Buffer outClientToken, Buffer outPrevTokenHash, Buffer outNewTokenHash)
      throws EnclaveException;

  /** Run the client's request, returning its response.
   *
   * @param enclaveHandle Enclave to use
   * @param clientHandle Client to use
   * @param permitsUsed Number of new query permits used by this request
   * @param clientAck Noise-encrypted serialized ClientRequest containing ack_token=true
   * @param outFinalResponse Noise-encrypted serialized ClientResponse containing contact information
   */
  private static native void nativeClientRun(
      long enclaveHandle,
      long clientHandle,
      int permitsUsed,
      Buffer clientAck,
      Buffer outFinalResponse)
      throws EnclaveException;

  /** Create a retry response for the client with the indicated retry time
   *
   * @param enclaveHandle Enclave to use
   * @param clientHandle Client to use
   * @param outFinalResponse Noise-encrypted serialized ClientResponse containing a retry_after_secs
   */
  private static native void nativeClientRetryResponse(
      long enclaveHandle, long clientHandle,
      int retryAfterSecs,
      Buffer outFinalResponse)
      throws EnclaveException;

  /** Discard a client handle, cleaning up any state associated with it.
   *
   * @param enclaveHandle Enclave to use
   * @param clientHandle Client to clean up
   */
  private static native void nativeClientClose(long enclaveHandle, long clientHandle)
      throws EnclaveException;

  /** Create/update enclave ereport. */
  private static native void nativeEnclaveAttest(long enclaveHandle)
      throws EnclaveException;

  /** Stop all threads and destroy the enclave
   *
   * @param enclaveHandle enclave to close
  */
  private static native void nativeEnclaveClose(long enclaveHandle)
      throws EnclaveException;

  /**
   * Collect health statistics for each shard in an enclave's table. This method populates a given buffer with the
   * serialized form of a {@link org.signal.cdsi.proto.TableStatistics} entity that has one
   * {@link org.signal.cdsi.proto.ShardStatistics} entity per configured shard. Each {@code ShardStatistics} entity has
   * the following key/value pairs:
   *
   * <dl>
   *   <dt>max_trace_length</dt>
   *   <dd>The longest number of steps needed to upsert a record in the shard's table</dd>
   *
   *   <dt>total_displacement</dt>
   *   <dd>"displacement" of a record is the distance between its actual placement and the position given by its hash
   *   value. "total_displacement" is the sum of displacements for all records. Divide by "num_items" to get mean
   *   displacement.</dd>
   *
   *   <dt>num_items</dt>
   *   <dd>Number of items in the shard's table.</dd>
   *
   *   <dt>capacity</dt>
   *   <dd>Capacity of the shard's table.</dd>
   *
   *   <dt>oram_recursion_depth</dt>
   *   <dd>Number of ORAM levels needed to back the shard's table.</dd>
   *
   *   <dt>oram_access_count</dt>
   *   <dd>Number of accesses to the shard's ORAM.</dd>
   *
   *   <dt>stash_overflow_count</dt>
   *   <dd>Current number of blocks in the ORAM overflow stash.</dd>
   *
   *   <dt>max_stash_overflow_count</dt>
   *   <dd>Highest number of blocks observed in the ORAM overflow stash.</dd>
   *
   *   <dt>sum_stash_overflow_count</dt>
   *   <dd>Sum of number of blocks in the ORAM overflow stash after each access. Divide by "oram_access_count" to get
   *   mean overflow count.</dd>
   *
   *   <dt>posmap_stash_overflow_count</dt>
   *   <dd>Current number of blocks in the ORAM overflow stash for the top-level position map.</dd>
   *
   *   <dt>posmap_max_stash_overflow_count</dt>
   *   <dd>Highest number of blocks observed in the ORAM overflow stash for the top-level position map.</dd>
   *
   *   <dt>posmap_stash_overflow_count</dt>
   *   <dd>Sum of number of blocks in the ORAM overflow stash after each access for the top-level position map. Divide
   *   by "oram_access_count" to get mean overflow count.</dd>
   *
   *   <dt>stash_overflow_ema10k</dt>
   *   <dd>10,000 times the exponential moving average (EMA) of the size of the ORAM overflow stash. The EMA has a
   *   half-life of 10,000 accesses.</dd>
   *
   *   <dt>posmap_stash_overflow_ema10k</dt>
   *   <dd>10,000 times the exponential moving average (EMA) of the size of the ORAM overflow stash for the top-level
   *   position map. The EMA has a half-life of 10,000 accesses.</dd>
   * </dl>
   *
   * @param enclaveHandle Enclave to use
   * @param outFinalResponse a buffer into which a serialized {@code TableStatistics} entity will be written
   *
   * @throws EnclaveException if statistics could not be gathered for any reason
   */
  private static native void nativeEnclaveTableStatistics(long enclaveHandle, Buffer outFinalResponse)
      throws EnclaveException;

  private static native int nativeGetRunningShardThreadCount(long enclaveHandle);
}
