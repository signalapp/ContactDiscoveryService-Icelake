# CDSI (Contact Discovery Service on Icelake)

## Code Locations

The top-level directory contains a Java (Micronaut) service which will act as
the host-side server for CDSI.  The `c/` subdirectory contains SGX-side C
code.

## Building

```
git submodule init
git submodule update
mvn verify
```

Note:  running tests locally currently requires the installation of OpenSSL 1.1.1,
which is standard in Ubuntu 20.04 but not in 22.04.  To install, you can:

```
wget https://ftp.openssl.org/source/openssl-1.1.1u.tar.gz
tar xvzf openssl-1.1.1u.tar.gz 
cd openssl-1.1.1u/
./config 
make  -j8
sudo make install -j8
sudo ldconfig
```

## Enclave releases

To create a new enclave release, run the following Maven command:

```shell
./mvnw exec:exec@enclave-release
```

...and commit the new files in `src/main/resources/org/signal/cdsi/enclave`.

## Configuration

The main CDSi application is built on the [Micronaut framework](https://micronaut.io/). It requires some runtime configuration to function outside a development environment.

### Running in a development environment

CDSi can run in a development environment in which it will use in-memory or mock implementations of most supporting services. This mode is absolutely not suitable for production usage, but may be helpful for testing or debugging. To run the CDSi application in a development environment, run in the `dev` environment:

```
./mvnw mn:run -Dmicronaut.environments=dev
```

The `dev` environment includes a test enclave binary and a reasonable default configuration. By default, the enclave will contain no records. To include a set of randomly-generated accounts for testing purposes, set the `random-account-populator.accounts` property (assuming no other account populator is configured) with the desired number of accounts, which must be less than or equal to the enclave's configured capacity.

### Configuring CDSi for production environments

To run CDSi in a "real" environment, certain properties must be set in some manner accessible to Micronaut (please see the [Micronaut configuration documentation](https://docs.micronaut.io/latest/guide/#config) for details).

#### Directory query rate limiter

The directory query rate limiter is backed by a Cosmos database, which must be configured with the following properties:

```yaml
cosmos:
  database: database-name
  container: container-name
  endpoint: "https://cosmos.example.com:443"
  key: <secret>
```

#### Connection rate limiter

The connection rate limiter is backed by a Redis cluster, and requires Redis cluster configuration to start:

```yaml
redis:
  uris: redis://redis.example.com
```

#### Account table/update stream

CDSi must load account data from an account data source. To configure account loading/synchronization from a DynamoDB table and Kinesis stream, the following properties must be set:

```yaml
accountTable:
  region: us-central-17
  tableName: dynamodb-table-name
  streamName: kinesis-stream-name
```

This system relies on an AWS Lambda (code in the `filter-cds-updates` subdirectory), which receives a DynamoDB update stream from the Account
table and filters out just the subset of updates that contact discovery would find useful.  The Lambda forwards those to a Kinesis stream, which
CDSi pulls from.

#### Authentication secret

End users communicate directly with CDSi. To authenticate users, CDSi uses Signal's standard external service credential system. Consequently, the following property must be set:

```yaml
authentication:
  sharedSecret: <base64-encoded-32-byte-secret>
```

#### Enclave configuration

All CDSi instances must have exactly one enclave that manages access to directory data. To configure the enclave, provide the following configuration properties:

```yaml
enclave:
  enclaveId: some-enclave-id
  availableEpcMemory: 32000000
  loadFactor: 1.6
  shards: 1
  token-secret: <secret>
  simulated: true
```

Additionally, the enclave requires a `ScheduledExecutorService` to manage concurrent access to native resources. The executor service is named `enclave-jni` and is managed by Micronaut. Reasonable defaults are provided for all environments, but the concurrency level can be adjusted in Micronaut's executor configuration section:

```yaml
micronaut:
  executors:
    enclave-jni:
      type: scheduled
      core-pool-size: 1
```

License
---------------------

Copyright 2022 Signal Messenger, LLC

Licensed under the AGPLv3: https://www.gnu.org/licenses/agpl-3.0.html
