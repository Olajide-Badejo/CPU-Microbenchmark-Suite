# Design decisions

Why the suite is built the way it is. Each entry is a correctness decision, not
a preference.

## Dependent load pointer chase

Ordinary array traversal is a poor latency probe because out of order execution
overlaps many accesses. A dependent load chain, where the next address is the
value just loaded, serializes the accesses, so the measured time per load is the
true load use latency. This is the only honest latency measurement.

## Single cycle permutation

The chase must visit one Hamiltonian cycle over all nodes. A permutation that
splits into shorter cycles keeps a small footprint resident in L1 regardless of
allocation size, silently reporting L1 latency for every working set. A unit
test proves the single cycle property for every swept size and rejects broken
multi cycle inputs.

## Random 64 byte stride

The stride prefetcher predicts regular patterns and hides latency. A random
permutation at one cache line stride defeats it.

## Three STREAM variants

The honest scalar baseline (vectorization suppressed on those functions only)
shows what the hardware does without SIMD. The compiler vectorized variant shows
what the optimizer gives for free. The AVX2 non temporal variant shows the best
case, where streaming stores avoid the read for ownership so a write costs one
transfer. Non temporal stores are used only for the DRAM test; on cache resident
tests they would bypass the cache being measured.

## Ten FMA chains

The FMA units have a latency of four to five cycles across two pipes, so at
least eight to ten in flight chains are needed to keep both pipes issuing. Fewer
chains stall on the latency of a single dependent chain. Non unity multipliers
prevent strength reduction; a contractive multiplier keeps values finite.

## Software clock

The reported clock is wrong under WSL2 (see the methodology). A dependent add
chain measures the real running frequency without a counter.

## Two segment least squares plateau detection

Four plateaus on one noisy desktop do not justify heavier machinery. Binary
segmentation in log latency space recovers the boundaries, cross checked against
sysfs. Log space is essential: in raw nanoseconds the single DRAM step dominates
the variance and the smaller L1 and L2 steps are missed.

## BLIS analytical tile model

The tile prediction is computed from measured cache capacities and
associativities, following the BLIS analytical model. It is validated against an
empirical GEMM sweep, and a miss is reported as a finding. The naive validation
kernel is not the packed kernel the model assumes, so a miss is expected and
instructive.

## LTO forbidden

Link time optimization lets the linker prove a measured loop dead and delete it.
It is forced off for every benchmark target and the build asserts it off.

## Timing inside workers

Parallel regions are timed inside the worker threads, with worker zero reducing
to the slowest per trial, rather than on the main thread. This removes a barrier
timing race that produced impossible bandwidth readings.
