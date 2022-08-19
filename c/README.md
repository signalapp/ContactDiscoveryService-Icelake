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
connection and submit a query. These are implemented in [/enclave/enc.c](./enclave/enc.c) and can be seen in use in 
[the enclave tests](./testhost/tests/testhost.c).

# Environment Setup

You need to have the Open Enclave packages and dependencies installed in order to build this project. The details depend on
whether you are building on Linux with SGX2, Linux without SGX2, or non-Linux.

-   _Linux with SGX_: Follow the steps [in the Open Enclave docs](https://github.com/openenclave/openenclave/blob/master/docs/GettingStartedDocs/install_oe_sdk-Ubuntu_20.04.md).
-   _Linux without SGX_: You will only be able to run in simulation mode. Follow the steps in the link above, but skip
    the driver installation.
-   _Non-Linux_: Create a Docker container and proceed as in "_Linux without SGX_". On a Mac you can use the Dockerfile
    in this directory: build with `docker build -t oebuild ./docker` and then run with `docker run --rm -it -v "$(pwd)":/src oebuild /bin/bash`, then you can proceed as on Linux without SGX.

# Building

Once your environment is set up, you can build with

```
make install
```

By default, encalve binaries will be built with a small-memory-footprint, test configuration. To build a full-scale version that uses up to 120 GiB of memory, use:

```
make CONFIG=cds.conf install
```

# Tests

To test the ORAM library and enclave calls:

```
make tests
make longtests  # takes a while
```


# Organization

-   `enclave/` contains the ECALL implementations that define the behavior of the enclave.
-   `testhost/` contains a simple host process that creates an enclave, inserts data, then queries it.
-   `path_oram/` contains the ORAM implementation
-   `ohtable/` contains an ORAM-backed hashtable
-   `sharded_ohtable/` contains a multi-threaded sharded wrapper around `ohtable`
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
* **Use oblivious algorithms for bigger problems.** There is often a straightforward way to make an
algorithm oblivious while squaring the running time, as with the Oblivious Turing Machine described in 
[Arora and Barak 2.3.4](http://theory.cs.princeton.edu/complexity/), but there is often a more efficient alternative. 
One important example is the oblivious [bitonic sort](http://www.cs.kent.edu/~batcher/sort.pdf), which sorts in 
time $O(N\log^{2}N)$ with small constants.

