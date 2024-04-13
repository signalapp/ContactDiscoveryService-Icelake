# CDSI SGX Enclave

The Contact Discovery Service (CDSI) SGX Enclave provides endpoints to expose a table of Signal user records 
indexed by phone number that allows users to securely discover which of their contacts are also Signal users.

While SGX provides memory encryption and attestation that are essential to the security of this enclave, 
it does not guard against memory access pattern or timing side-channels. To close memory-access pattern
side channels at the architectural level, the table or user records is stored in [Oblivious RAM (ORAM)](https://dl.acm.org/doi/10.1145/3177872). To
close timing and access-pattern side-channels at the local level, the code is written using branchless idioms
and oblivious algorithms. Please see [Side-channel Resistant Programming Idioms](#side-channel-resistant-programming-idioms) before
contributing.

# What the Enclave Does

The enclave calls can be seen in [cds.edl](./cds.edl) and fall into two categories: *administrative calls* 
to control lifecycle, monitoring, and data loading, and *client calls* that allow clients to create a secure
connection and submit a query. 

The administrative calls are: `enclave_init`, `enclave_stop_shards`, `enclave_run_shard`, `enclave_load_pb`, `enclave_attest`, and `enclave_table_statistics`.

The client calls are: `enclave_new_client`, `enclave_close_client`, `enclave_handshake`, `enclave_rate_limit`, `enclave_run`, and `enclave_retry_response`


These are implemented in [/enclave/enc.c](./enclave/enc.c) and can be seen in use in 
[the enclave tests](./testhost/tests/testhost.c).

# Environment Setup

You need to have the Open Enclave packages and dependencies installed in order to build this project. The details depend on
whether you are building on Linux with SGX2, Linux without SGX2, or non-Linux.

-   _Linux with SGX_: Follow the steps [in the Open Enclave docs](https://github.com/openenclave/openenclave/blob/master/docs/GettingStartedDocs/install_oe_sdk-Ubuntu_20.04.md).
-   _Linux without SGX_: You will only be able to run in simulation mode. Follow the steps in the link above, but skip
    the driver installation.
-   _Non-Linux_: In a non-Linux environment it is easiest to use the `make docker_*` commands described below.  
    Alternatively you can use the Dockerfile in `c/docker` to create a shell and work as with Linux without SGX.

# Building

Once your environment is set up, you can build with

```
make all # or make docker_all
```

By default, enclave binaries will be built with a small-memory-footprint, test configuration. To build a full-scale version that uses up to 120 GiB of memory, use:

```
make CONFIG=cds.conf install
```

# Tests

To test the ORAM library and enclave calls:

```
make tests # or make docker_tests
make valgrinds # or make docker_valgrinds
```


# Organization

-   `enclave/` contains the ECALL implementations that define the behavior of the enclave.
-   `testhost/` contains a simple host process that creates an enclave, inserts data, then queries it.
-   `fixedset/`  a hashset where keys are fixed-sized byte strings.
-   `jnishim/` expose enclave functions to Java
-   `noiseutil/` wrappers around `noise-c` to support messages larger than 64K and provide in-place decryption
-   `ohtable/` contains an ORAM-backed hashtable
-   `path_oram/` contains the ORAM implementation
-   `proto/` protobuf utilities
-   `ratelimit/` client request reate-limiting functions
-   `sharded_ohtable/` contains a multi-threaded sharded wrapper around `ohtable`
-   `util/` common macros and error codes
-   `cds.edl` defines the enclave interface.

# Side-channel Resistant Programming Idioms

In an effort to close timing and memory based side channels, several idioms and patterns are used
throughout the code that look different from textbook C. Before making changes be sure to be aware
of these and their purposes.

* **Avoid short-ciruit boolean operators.** Use bitwise operators instead, but ensure that the arguments to these operators are either `0` or `1`.
* **Avoid the C ternary operator with `bool`**. Use the macro `U64_TERNARY` defined in [/util/util.h](/util/util.h)
instead.
* **No `if`s or loop limits that depend on secret data.** Good examples using loops are the ORAM accessor functions 
in [/path_oram/path_oram.c](./path_oram/path_oram.c) and [ohtable/ohtable.c](./ohtable/ohtable.c) which loop through ORAM 
blocks obliviously operating on the data using all of these idioms. For simpler example, one common situation is that 
we want to increment a counter when a condition is true. Here is how that is done:
```c
// Don't do this:
if(condition) counter++;

// Instead do this:
counter += U64_TERNARY(condition, 1, 0);
```
* **No division or modulo operators that depend on secret data.** Division and modulo are not guaranteed to be constant time. 
If the divisor is known in advance then you can use the method of Granlund and Montgomery,
"Division by Invariant Integers using Multiplication" (https://gmplib.org/~tege/divcnst-pldi94.pdf) which is implemented in `util/util.h`
by the functions `prep_ct_div`, `ct_div`, and `ct_mod`.
* **Use oblivious algorithms for bigger problems.** There is often a straightforward way to make an
algorithm oblivious at the expense of the running time, such as using a linear scan of an array instead of a binary search.  
There is often a more efficient alternative oblivious algorithm and these should be used when they improve performance. 
One important example is the oblivious [bitonic sort](http://www.cs.kent.edu/~batcher/sort.pdf), which sorts in 
time $O(N\log^{2}N)$ with small constants.
