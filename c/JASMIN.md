<!--
Copyright 2026 Signal Messenger, LLC
SPDX-License-Identifier: AGPL-3.0-only
-->

# Jasmin Code Idioms

This document explains how Jasmin is used in this repository, focusing on code patterns, conventions, and practical implications that are not obvious without prior exposure.

It is not a complete or authoritative description of the Jasmin language, but a guide to understanding and reviewing the code found here.

## Jasmin Documentation

Jasmin documentation is available [here](https://jasmin-lang.readthedocs.io/en/stable/index.html).

Jasmin is part of the [Formosa Crypto project](https://formosa-crypto.org/). While documentation is limited, they have a helpful [Zulip chat](https://formosa-crypto.zulipchat.com/) where Jasmin developers will answer questions and help dive into problems.

## File Naming Conventions

### `.jinc` Files
These files contain core logic, often with `inline` functions. They are meant to be included (`require`d) into other files rather than compiled directly. Examples:
- `c/util/osort.jinc` - oblivious sorting algorithm
- `c/path_oram/bucket.jinc` - bucket operations for Path ORAM

### `.jazz` Files
These files export functions for external (C) linkage. They perform:
1. **Register preparation**: Assigning function parameters to the correct registers for the calling convention (the `param = param;` pattern ensures proper register assignment).
2. **MSF initialization**: Setting up the speculative load hardening state with `msf = #init_msf();`.
3. **Calling `.jinc` functions**: Invoking the inline functions from `.jinc` files.

Example from `c/ratelimit/jratelimit.jazz`:
```jasmin
export
fn odd_even_msort_uint64s(
  reg uptr data,
  reg u64 lb ub
)
{
  #msf reg u64 msf;
  data = data;    // register preparation
  lb = lb;
  ub = ub;
  msf = #init_msf();  // initialize speculative load hardening
  msf = osort(data, lb, ub, msf);
}
```

## Function Naming Conventions

Exported Jasmin functions follow one of two naming patterns depending on
whether C retains a wrapper around the export.

### Direct replacement (no suffix)

When the Jasmin function **replaces** the C function entirely — same name, same
signature, no C wrapper — the export uses the canonical name with no suffix.
The C function is deleted and callers link directly to the Jasmin symbol.

Examples:
- `halfsiphash` — the Jasmin export *is* the public API
- `odd_even_msort_uint64s` — replaces the former C `bitonic_sort_uint64s`
  (algorithm changed, so the name changed too)

### Wrapper pattern (`_jazz` suffix)

When C retains a **thin wrapper** around the Jasmin export — for example to
update statistics, perform error handling, or adapt calling conventions — the
Jasmin export takes a `_jazz` suffix to distinguish it from the C public API:

```c
// C wrapper: public API
error_t oram_get(oram* oram, u64 block_id, u64 buf[]) {
    error_t err = oram_get_jazz(oram, block_id, buf);  // Jasmin impl
    oram->statistics.accesses++;
    return err;
}
```

The suffix signals: "this is the Jasmin implementation; the C function of the
same base name is the caller-facing API."

### Struct layout validators (`_jazz` suffix)

Functions that export Jasmin's view of struct layout — sizes and field offsets —
always use the `_jazz` suffix. These exist purely for cross-language validation
and have no C counterpart:

```c
// In C, validated at startup:
CHECK(offsetof(ohtable, capacity) == ohtable_capacity_offset_jazz());
CHECK(sizeof(ohtable) == ohtable_sizeof_jazz());
```

The `_jazz` suffix here means: "this value comes from Jasmin's struct layout
definition."

### Summary

| Scenario | Suffix | Example |
|----------|--------|---------|
| Jasmin *is* the public API | none | `halfsiphash(...)` |
| C wrapper around Jasmin impl | `_jazz` | `oram_get_jazz(...)` |
| Struct layout validator | `_jazz` | `oram_sizeof_jazz()` |

## Parameter Sets (prod/test)

Modules use different compile-time parameters for production and test builds.
All parameters live in a single shared `params.jinc` per build variant:

- `c/jasmin.prod/params.jinc` — production parameters (larger ORAM, more shards)
- `c/jasmin.test/params.jinc` — test parameters (smaller sizes for faster tests)

Source files include parameters via the build-system-defined `ENV` include root:

```jasmin
from ENV require "params.jinc"
```

The include roots are set globally in `Makefile.base` via `JASMIN_PROD_FLAGS`
and `JASMIN_TEST_FLAGS`, so most modules need no per-module build configuration.

## Using `osort` (Oblivious Sort)

The `osort` module (`c/util/osort.jinc`) is not standalone. Before including it, you must define:

1. **`BLOCK_SIZE`**: Size in bytes of each element to sort.
2. **`cmp_function(reg u64 addr1, reg u64 addr2) -> reg u8`**: Returns 1 if the first element is greater, 0 otherwise.
3. **`oblv_cond_swap(reg u8 cond, reg u64 a, reg u64 b)`**: Obliviously swaps elements if `cond` is true.

### Example: `ratelimit.jinc`

See `c/ratelimit/ratelimit.jinc`:

```jasmin
// block size in bytes (64 bits = 8 bytes)
param int BLOCK_SIZE = 8;

// obliviously swap two u64 values if `cond` is true
inline
fn oblv_cond_swap(reg u8 cond, reg uptr a b)
{
  _cond_obv_swap_u64(cond, a, b);
}

// return 1 if *lhs > *rhs, else 0
inline
fn cmp_function(reg uptr lhs rhs) -> reg u8
{
  reg u64 a;
  reg u8 r;
  reg bool cond;
  a = [:u64 lhs];
  cond = a > [:u64 rhs];
  r = #SETcc(cond);
  return r;
}
```

Then in the `.jazz` file, include both the definitions and osort:
```jasmin
require "ratelimit.jinc"      // definitions
from UTIL require "osort.jinc" // osort algorithm
```

## Using `ocompact` (Oblivious Compaction)

The `ocompact` module (`c/util/ocompact.jinc`) compacts elements matching a predicate to the front of the array. Before including it, you must define:

1. **`BLOCK_SIZE`**: Size in bytes of each element.
2. **`pred_function(reg u64 addr) -> reg u8`**: Returns 1 if the element should be kept, 0 otherwise.
3. **`oblv_cond_swap(reg u8 cond, reg u64 a, reg u64 b)`**: Obliviously swaps elements if `cond` is true.

### Example: `sharded_ohtable.jinc`

See `c/sharded_ohtable/sharded_ohtable.jinc`:

```jasmin
param int BLOCK_SIZE = BatchableRequest::BLOCK_SIZE;

inline
fn pred_function(reg uptr data) -> reg u8
{
  reg u8 cond;
  cond = flag_is_set(data);
  return cond;
}

inline
fn oblv_cond_swap(reg u8 cond, reg uptr a b)
{
  inline int i;
  for i = 0 to BatchableRequest::BLOCK_SIZE / (8 * 4)
  {
    _cond_obv_swap_u256(cond, a, b);
    a += 32;
    b += 32;
  }
}
```

### Multiple Sort/Compact Operations with Different Comparators

When you need multiple osort or ocompact operations with different comparison/predicate functions in the same file, use namespaces to isolate them. See `c/sharded_ohtable/sharded_ohtable.jinc` for an example:

```jasmin
namespace Query {
  inline
  fn cmp_function(reg uptr a b) -> reg u8
  {
    reg u8 cond;
    cond = cmp_shard_flag_key(a, b);
    return cond;
  }

  from UTIL require "osort.jinc"
  from UTIL require "ocompact.jinc"
  // ... wrapper functions using Query::osort, Query::ocompact
}

namespace Insert {
  inline
  fn cmp_function(reg uptr a b) -> reg u8
  {
    reg u8 cond;
    cond = cmp_stable_shard_flag_key(a, b);
    return cond;
  }

  from UTIL require "osort.jinc"
  from UTIL require "ocompact.jinc"
  // ... wrapper functions using Insert::osort, Insert::ocompact
}
```

This pattern allows defining multiple comparison functions without redefinition conflicts. The shared `BLOCK_SIZE`, `oblv_cond_swap`, and `pred_function` are defined once at the outer scope.

## Jasmin Execution Model

This section provides intuition for reading Jasmin code in this repository. It is not a formal description of the language semantics.

### Instruction Granularity

Each Jasmin statement is written to map closely to a single assembly instruction. For this reason, lines are intentionally split into minimal operations.
```c
// in C
u64 step = (1ULL << l) - 1;

// in Jasmin
reg u64 step = 1;
step <<= l;
step -= 1;
```

## `require` Semantics

`require` performs literal textual inclusion. This explains why `require` statements may appear in the middle of files. Inclusion order can affect name resolution and behavior. This feature is exploited, for example, when using generic [`osort`](#multiple-sortcompact-operations-with-different-comparators).

## Control Flow Idioms

### `for` Loops

`for` loops are fully unrolled at compile time. Therefore, loop bounds must be statically known. Unrolling can significantly increase code size, and consequently the compilation time.

### `while` Loops

`while` loops compile to explicit jumps. This explains why [`msf`](#speculative-load-hardening-msf) must be updated only in while loops when protecting against speculative executions.

## Memory and Data Layout

### Raw Memory Access

Memory addresses are treated as raw values. Then, pointer arithmetic is explicit.

### Arrays

In Jasmin, dynamic allocation is not supported. However, we can create fixed-size arrays which are preferred rather than explicitly handle raw addresses.
They can also be sliced and passed as parameters, e.g. `c/path_oram/path_oram.jinc:access`:
```jasmin
reg ptr u64[DECRYPTED_BLOCK_SIZE_QWORDS] target_block;
...
// array[starting_index:number_of_qwords]
target_block[2:BLOCK_DATA_SIZE_QWORDS] =
  Accessor::read(target_block[2:BLOCK_DATA_SIZE_QWORDS], vargs);
```

## Spill and Stack Management

As discussed earlier, Jasmin code must be read with the target assembly in mind. Register pressure is explicit, and developers must account for the limited number of available registers. There are sixteen general-purpose registers, in addition to MMX and vector registers. Dead variables are automatically eliminated by the compiler.

### Spill / Unspill Notation

To save up registers, developers can spill unused values to the stack and unspill when needed again. There are two notations for this:
- Explicit notation:
  ```jasmin
  reg u64 a;
  stack u64 a_s;
  ...
  a_s = a; // spill or store in the stack
  ...
  a = a_s; // reload value or unspill
  ```
- Syntactic sugar notation:
  ```jasmin
  reg u64 a b;
  ...
  () = #spill(a, b); // multiple spills are possible
  ...
  () = #unspill(a, b);
  ```

### MMX Registers

In the processor there are also `#mmx` registers. They are used for special operations, but in Jasmin, they're often used to spill registers because spilling into registers is much faster than into the stack.

Developers can either explicitly spill using `#mmx` registers:
```jasmin
reg u64 a;
#mmx reg u64 a_mmx;
...
a_mmx = a;
...
a = a_mmx;
```
or using `#spill` notation and declaring the variable as `#spill_to_mmx`:
```jasmin
#spill_to_mmx reg u64 a;
...
() = #spill(a);
```

## Language Limitations

- Jasmin does not support recursion.
- Jasmin does not support function parameters in the conventional sense.

## Compiler Quirks

### Immediates as Left Operands

Jasmin cannot use an immediate (literal constant) as the left operand of subtraction. You must load the constant into a register first:
```jasmin
// Does NOT compile:
result = 64 - result;

// Correct:
reg u64 sixtyfour;
sixtyfour = 64;
result = sixtyfour - result;
```

### Flag Returns on x86 Instructions

Most x86 arithmetic/logic instructions set CPU flags (CF, SF, OF, ZF, PF) as side effects. Jasmin forces you to explicitly capture all outputs, including these flags. Use `_` to discard unwanted flag values:
```jasmin
// LZCNT returns 5 flags + the result (6 outputs total)
_, _, _, _, _, result = #LZCNT(n);

// SHL, SHR, NEG, etc. follow the same pattern
_, _, _, _, _, x = #SHL(x, shift);
```

### Not All x86 Instructions Are Available

Jasmin exposes x86 instructions as intrinsics (`#LZCNT`, `#SHL`, `#NEG`, etc.), but not every instruction is supported. For example, `#BSR` (Bit Scan Reverse) is not available — use `#LZCNT` instead. When in doubt, test with `jasminc`.

### Register Allocation with Inline Functions

When a function is `inline`, its variables share the register space of the caller. An expression like `n -= 1` that works in isolation may cause register allocation conflicts when inlined into a caller that still needs `n`. The fix is to copy into a fresh variable:
```jasmin
// May conflict when inlined:
n -= 1;
_, _, _, _, _, result = #LZCNT(n);

// Safe — n is preserved for the caller:
reg u64 nm1;
nm1 = n;
nm1 -= 1;
_, _, _, _, _, result = #LZCNT(nm1);
```

### In-Place NOT for Boolean Toggles

The x86 `NOT` instruction operates in-place, so `b = !a` requires `a` and `b` to be in the same physical register. Declaring them as separate variables causes a register allocation conflict:

```jasmin
// Does NOT compile when register pressure is high:
reg u8 is_assigned not_assigned;
not_assigned = !is_assigned;

// Correct — toggle in-place, using comments to track meaning:
reg u8 is_assigned;
is_assigned = !is_assigned;  // temporarily holds !is_assigned
// ... use is_assigned as "not_assigned" ...
is_assigned = !is_assigned;  // restore is_assigned
is_assigned |= assignable;
```

This pattern appears in the stash bucket-assignment functions wherever a boolean needs to be temporarily inverted.

### CLI Flag Syntax for Include Paths

`jasminc` requires a space between `-I` and the identifier binding: `-I UTIL=path/to/util`. The no-space form (`-IUTIL=...`) is rejected as an unknown option. `jasmin-ct` accepts both forms.

The Makefile uses the spaced form for `JASMIN_FLAGS` (consumed by `jasminc`) and the no-space form for `JASMIN_CT_FLAGS`/`JASMIN_SCT_FLAGS` (consumed by `jasmin-ct`), but both could use the spaced form.

## Self-Assignments and Register Preparation

### The `x = x;` Pattern

The self-assignment is used for re-assigning values into other registers. Common places where these appear are:
- [Export functions](#jazz-files): to properly assign registers when calling the Jasmin function.
- Before fixed-register instructions: e.g. `DIV` needs dividend and divisor in RDX and RAX respectively, then we might need to re-assign the value stored in those registers.

## Randomness

`#randombytes` is compiled to a function call to `__jasmin_syscall_randombytes__` which must be provided at link time.

### Current implementation

The symbol is defined in `c/noise-c/src/backend/ref/dh-kyber.c:35` by mlkem-libjade (part of noise-c). It delegates to `noise_rand_bytes`. This definition is linked into every binary that uses noise-c:

- **Test binaries** (`%.test`): always link `libnoise.a` (see `Makefile` rule `%.test: ... libnoise.a`)
- **Enclave binaries** (`enclave.bin`, `enclave.testbin`): always link `libnoise.a`

### Name collision history

Both mlkem-libjade (inside noise-c) and the Jasmin toolchain use `__jasmin_syscall_randombytes__` as the shim name. If you try to provide a second (non-weak) definition alongside noise-c, the linker will report a multiple-definition error. The solution is to rely exclusively on noise-c's definition rather than defining the symbol ourselves.

### If noise-c is replaced or no longer provides this symbol

You will need to provide the symbol yourself. Place the following in a new `.c` file compiled into the enclave library:

```c
#include <stdint.h>
#include <openenclave/enclave.h>

uint8_t* __jasmin_syscall_randombytes__(uint8_t* x, uint64_t xlen) {
    oe_random(x, xlen);
    return x;
}
```

If noise-c is still linked alongside this new definition, mark it `__attribute__((weak))` so noise-c's strong definition wins — or remove noise-c's definition from `dh-kyber.c` instead.

## Security Annotations

There are two states in constant-time executions: public and secret. Values read from memory are assumed secret. Therefore, developers have to explicitly make them public using the directive `#declassify`.

Additionally, the `#inline` directive informs the constant-time checker that certain control-flow constructs (e.g., `if` statements) do not introduce runtime-dependent jumps because the condition is known at compile time. See `c/path_oram/path_oram.jinc:access`.

## Speculative Load Hardening (MSF)

`msf` is the misspeculation flag which is a register that will mask secret values if branch predictor is misspeculating.

`#init_msf` initializes the msf. The compiler translates this to a `LFENCE`, i.e. it ensures that all load instructions preceding are completed before any subsequent instructions begin execution. From that point on, developers have to update the msf on jumps and protect values if they are exposed to speculative executions using `#update_msf` and `#protect` respectively.

In speculative executions, a new state `transient` is introduced. Values that **might** contain a secret value are marked as transient, so they must be protected in order to make them public.

## Compilation and Export

Jasmin compiler takes a file as argument and it will compile all `export` funtions in that file and everything they need. To test, or optimize compilation there is a flag `--slice` which lets compile a (or multiple) passed functions.

### Calling Jasmin from C

Exported Jasmin function can be imported in C, but not the other way around. To cleanly import Jasmin functions in C, developers have to declare an `extern` function in C and link the outputted assembly (or binary converted) from the Jasmin compiler.

## Jasmin Toolchain Version Management

The Jasmin compiler (`jasminc`) and constant-time checker (`jasmin-ct`) are
installed via OPAM inside the Docker build environment. To ensure deterministic
builds, we pin both the jasmin version and all of its transitive OCaml
dependencies using an OPAM lockfile.

### Files

- `c/docker/cds-jasmin.opam` -- declares the jasmin version we depend on.
- `c/docker/cds-jasmin.opam.locked` -- pins every transitive dependency to an
  exact version. Generated by `opam lock`.
- `c/docker/Dockerfile` -- installs jasmin via `opam install --locked`.

Note: the Docker image installs `rsync` because OPAM uses it to fetch
locally pinned packages (the wrapper lives in `/home/signal/cds-jasmin`). If
you customize the base image, ensure `rsync` is present or OPAM will fail
when installing the local `cds-jasmin` package.

The wrapper package is named `cds-jasmin` (not `jasmin`) so that `opam install
--locked` does not shadow the real `jasmin` package in the OPAM repository.

### Bumping the Jasmin Version

1. Update the version constraint in `cds-jasmin.opam`:
   ```
   depends: [
     "jasmin" {= "NEW_VERSION"}
   ]
   ```

2. Regenerate the lockfile. From the `c/docker/` directory, run:
   ```bash
   docker run --rm \
     -v "$(pwd)/cds-jasmin.opam:/work/cds-jasmin.opam" \
     amd64/debian:bookworm bash -c '
       apt-get update -qq && \
       apt-get install -y -qq opam rsync build-essential m4 bubblewrap \
         libgmp-dev libpcre3-dev libmpfr-dev libppl-dev autoconf \
         unzip pkg-config > /dev/null 2>&1 && \
       export OPAMYES=1 OPAMJOBS=2 && \
       opam init -y --disable-sandboxing -c ocaml-base-compiler.4.12.0 \
         > /dev/null 2>&1 && \
       eval $(opam env) && \
       opam install -y jasmin.NEW_VERSION > /dev/null 2>&1 && \
       cd /work && \
       opam lock cds-jasmin 2>&1 && \
       cat cds-jasmin.opam.locked
     '
   ```
   Copy the output into `cds-jasmin.opam.locked`.

    Do not rename the files; the Dockerfile expects
    `cds-jasmin.opam` and `cds-jasmin.opam.locked` under `c/docker/`.

   Alternatively, run `make dockersh`, then inside the container:
   ```bash
   eval $(opam env)
   cd /path/to/c/docker
   opam lock cds-jasmin
   ```

3. Rebuild the Docker image: `make docker_all` (or just `make dockersh` to
   verify the build).

4. Run `make docker_tests` to confirm the new compiler version works with all
   existing Jasmin code.
