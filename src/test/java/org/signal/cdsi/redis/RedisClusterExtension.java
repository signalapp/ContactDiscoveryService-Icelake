/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.signal.cdsi.redis;

import io.lettuce.core.FlushMode;
import io.lettuce.core.RedisClient;
import io.lettuce.core.RedisURI;
import io.lettuce.core.cluster.RedisClusterClient;
import io.lettuce.core.cluster.api.StatefulRedisClusterConnection;
import io.lettuce.core.internal.HostAndPort;
import io.lettuce.core.resource.ClientResources;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import io.lettuce.core.resource.DnsResolvers;
import io.lettuce.core.resource.MappingSocketAddressResolver;
import org.junit.jupiter.api.extension.AfterAllCallback;
import org.junit.jupiter.api.extension.AfterEachCallback;
import org.junit.jupiter.api.extension.BeforeAllCallback;
import org.junit.jupiter.api.extension.BeforeEachCallback;
import org.junit.jupiter.api.extension.ExtensionContext;
import org.testcontainers.containers.ComposeContainer;
import org.testcontainers.containers.wait.strategy.Wait;
import org.testcontainers.containers.wait.strategy.WaitStrategy;

public class RedisClusterExtension implements BeforeAllCallback, BeforeEachCallback, AfterAllCallback, AfterEachCallback {

  private static final int REDIS_PORT = 6379;
  private static final WaitStrategy WAIT_STRATEGY = Wait.forListeningPort().withStartupTimeout(Duration.ofMinutes(1));
  private static final Duration CLUSTER_STARTUP_TIMEOUT = Duration.ofSeconds(30);

  private static final String[] REDIS_SERVICE_NAMES = new String[] { "redis-0-1", "redis-1-1", "redis-2-1" };

  // The image we're using is bitnami/redis-cluster:6.2; please see
  // https://hub.docker.com/layers/bitnami/redis-cluster/6.2/images/sha256-764cc3c12b39f215255926d46471fbe7f70689c629b2d4ac60ea5407d90bf7ff
  private static final String CLUSTER_COMPOSE_FILE_CONTENTS = """
      services:
        redis-0:
          image: docker.io/bitnami/redis-cluster@sha256:d973a2aa8b6688190ca4e4544b2ff859ef1e9f8081518558270df34e23ff1df7
          environment:
            - 'ALLOW_EMPTY_PASSWORD=yes'
            - 'REDIS_NODES=redis-0 redis-1 redis-2'

        redis-1:
          image: docker.io/bitnami/redis-cluster@sha256:d973a2aa8b6688190ca4e4544b2ff859ef1e9f8081518558270df34e23ff1df7
          environment:
            - 'ALLOW_EMPTY_PASSWORD=yes'
            - 'REDIS_NODES=redis-0 redis-1 redis-2'

        redis-2:
          image: docker.io/bitnami/redis-cluster@sha256:d973a2aa8b6688190ca4e4544b2ff859ef1e9f8081518558270df34e23ff1df7
          depends_on:
            - redis-0
            - redis-1
          environment:
            - 'ALLOW_EMPTY_PASSWORD=yes'
            - 'REDIS_CLUSTER_REPLICAS=0'
            - 'REDIS_NODES=redis-0 redis-1 redis-2'
            - 'REDIS_CLUSTER_CREATOR=yes'
      """;

  private ComposeContainer composeContainer;
  private Map<HostAndPort, HostAndPort> exposedAddressesByInternalAddress;
  private List<RedisURI> redisUris;

  private RedisClusterClient redisClusterClient;

  public static RedisClusterExtensionBuilder builder() {
    return new RedisClusterExtensionBuilder();
  }

  @Override
  public void beforeAll(final ExtensionContext context) throws Exception {
    final File clusterComposeFile = File.createTempFile("redis-cluster", ".yml");
    clusterComposeFile.deleteOnExit();

    try (final FileOutputStream fileOutputStream = new FileOutputStream(clusterComposeFile)) {
      fileOutputStream.write(CLUSTER_COMPOSE_FILE_CONTENTS.getBytes(StandardCharsets.UTF_8));
    }

    // Unless we specify an explicit list of files to copy to the container, `ComposeContainer` will copy ALL files in
    // the compose file's directory. Please see
    // https://github.com/testcontainers/testcontainers-java/blob/main/docs/modules/docker_compose.md#build-working-directory.
    composeContainer = new ComposeContainer(clusterComposeFile)
        .withCopyFilesInContainer(clusterComposeFile.getName());

    for (final String serviceName : REDIS_SERVICE_NAMES) {
      composeContainer = composeContainer.withExposedService(serviceName, REDIS_PORT, WAIT_STRATEGY);
    }

    composeContainer.start();

    exposedAddressesByInternalAddress = Arrays.stream(REDIS_SERVICE_NAMES)
        .collect(Collectors.toMap(serviceName -> {
              final String internalIp = composeContainer.getContainerByServiceName(serviceName).orElseThrow()
                  .getContainerInfo()
                  .getNetworkSettings()
                  .getNetworks().values().stream().findFirst().orElseThrow()
                  .getIpAddress();

              if (internalIp == null) {
                throw new IllegalStateException("Could not determine internal IP address of service container: " + serviceName);
              }

              return HostAndPort.of(internalIp, REDIS_PORT);
            },
            serviceName -> HostAndPort.of(
                composeContainer.getServiceHost(serviceName, REDIS_PORT),
                composeContainer.getServicePort(serviceName, REDIS_PORT))));

    redisUris = Arrays.stream(REDIS_SERVICE_NAMES)
        .map(serviceName -> RedisURI.create(
            composeContainer.getServiceHost(serviceName, REDIS_PORT),
            composeContainer.getServicePort(serviceName, REDIS_PORT)))
        .toList();

    // Wait for the cluster to be fully up; just having the containers running isn't enough since they still need to do
    // some post-launch cluster setup work.
    boolean allNodesUp;
    final Instant deadline = Instant.now().plus(CLUSTER_STARTUP_TIMEOUT);

    final ClientResources clientResources = ClientResources.builder()
        .socketAddressResolver(MappingSocketAddressResolver.create(DnsResolvers.UNRESOLVED,
            hostAndPort -> exposedAddressesByInternalAddress.getOrDefault(hostAndPort, hostAndPort)))
        .build();

    try {
      do {
        allNodesUp = redisUris.stream()
            .allMatch(redisUri -> {
              try (final RedisClient redisClient = RedisClient.create(clientResources, redisUri)) {
                final String clusterInfo = redisClient.connect().sync().clusterInfo();
                return clusterInfo.contains("cluster_state:ok") && clusterInfo.contains("cluster_slots_ok:16384");
              } catch (final Exception e) {
                return false;
              }
            });

        if (Instant.now().isAfter(deadline)) {
          throw new RuntimeException("Cluster did not start before deadline");
        }

        if (!allNodesUp) {
          Thread.sleep(100);
        }
      } while (!allNodesUp);
    } finally {
      clientResources.shutdown().await();
    }
  }

  @Override
  public void beforeEach(final ExtensionContext context) {
    redisClusterClient = RedisClusterClient.create(redisUris);

    try (final StatefulRedisClusterConnection<String, String> connection = redisClusterClient.connect()) {
      connection.sync().flushall(FlushMode.SYNC);
    }
  }

  @Override
  public void afterEach(final ExtensionContext context) {
    redisClusterClient.shutdown();
  }

  @Override
  public void afterAll(final ExtensionContext context) {
    composeContainer.stop();
  }

  public RedisClusterClient getRedisClusterClient() {
    return redisClusterClient;
  }

  public static class RedisClusterExtensionBuilder {

    private RedisClusterExtensionBuilder() {
    }

    public RedisClusterExtension build() {
      return new RedisClusterExtension();
    }
  }
}
