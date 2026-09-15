# High-Performance Limit Order Book & Matching Engine

A price-time-priority limit order book and matching engine in modern
C++17, built to demonstrate low-latency systems design: custom data
structures, allocation-free hot paths, and a real Google Benchmark suite.

## Architecture

```
orderbook-engine/
├── CMakeLists.txt
├── include/
│   ├── types.hpp          # OrderId, Price (fixed-point ticks), Side, Fill
│   ├── memory_pool.hpp    # fixed-block free-list allocator (no new/delete on hot path)
│   ├── order.hpp          # Order node + PriceLevel intrusive doubly-linked FIFO list
│   └── order_book.hpp     # OrderBook: bid/ask trees, order lookup map, matching API
├── src/
│   └── order_book.cpp     # matching engine implementation
├── examples/
│   ├── smoke_test.cpp     # dependency-free correctness checks (plain g++, no CMake needed)
│   └── latency_probe.cpp  # dependency-free std::chrono micro-benchmark
├── tests/
│   └── test_order_book.cpp    # GoogleTest suite (13 cases)
└── benchmarks/
    └── bench_order_book.cpp   # Google Benchmark suite (4 benchmarks)
```

### Data structures

| Structure | Choice | Why |
|---|---|---|
| Order | Intrusive doubly-linked-list node (`prev`/`next`/`level` pointers embedded in the struct) | Lets a resting order be unlinked in O(1) with no separate list-node allocation |
| Price level (orders at one price) | Doubly-linked list, FIFO (`push_back` at tail, match from `head`) | Preserves strict time priority within a price level |
| Book side (all price levels) | `std::map<Price, PriceLevel>` — `std::greater<Price>` for bids, `std::less<Price>` for asks | Red-black tree gives O(log n) insert/erase of a *level* and O(1) best-price access via `begin()`. Levels themselves rarely get created/destroyed compared to how often individual orders are added, so paying O(log n) only when a level is born or emptied (not per-order) is the right trade-off versus a flatter structure |
| Order lookup | `std::unordered_map<OrderId, Order*>` | O(1) average-case direct cancel/modify without walking either tree |
| Order allocation | Custom `MemoryPool<Order>` — free-list over pre-allocated chunks | Avoids `new`/`delete` (and its allocator lock contention) on the add/cancel hot path |

`Price` is a fixed-point integer (ticks), not a `double` — floating-point
price comparisons are a classic source of matching-engine bugs (`0.1 +
0.2 != 0.3`), so real venues quote in integer ticks and this engine does
the same.

### Matching semantics

- **Price-time priority**: an incoming order matches the best price
  first (`map::begin()`), and within a price level, the oldest resting
  order first (linked-list `head`).
- **Order types**: `addLimitOrder` (matches while price allows, rests
  any remainder) and `addMarketOrder` (matches at whatever price is
  available; unfilled quantity is dropped — IOC semantics — rather than
  resting an unpriced order in the book).
- **Cancel**: O(1) average — hash lookup, then O(1) intrusive unlink. If
  the price level becomes empty it's erased from the tree (O(log n),
  off the dominant cost path).
- **Modify**: reducing quantity keeps the order's place in the FIFO
  queue (matches real exchange "reduce" semantics). Increasing quantity
  is treated as cancel + re-add — this *does* lose time priority, which
  mirrors how most real venues behave and is called out explicitly in
  the code and tests.

## Building

Requires CMake ≥ 3.16 and a C++17 compiler. GoogleTest and Google
Benchmark are fetched automatically via `FetchContent` on first
configure (needs network access once, then they're cached locally).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

Run everything:

```bash
./smoke_test        # dependency-free correctness checks
ctest               # GoogleTest suite via CTest
./bench_order_book   # Google Benchmark suite
./latency_probe      # dependency-free std::chrono micro-benchmark
```

Turn off tests or benchmarks if you don't want the extra downloads:

```bash
cmake .. -DOBE_BUILD_TESTS=OFF -DOBE_BUILD_BENCHMARKS=OFF
```

Add `-DOBE_NATIVE_ARCH=ON` to compile with `-march=native` on hardware
you control (skip this for portable/CI builds).

## Correctness

The GoogleTest suite (`tests/test_order_book.cpp`) covers 13 cases:
basic crossing, time priority within a level, price priority *across*
levels, partial fills, non-crossing books, cancel (including cleanup of
now-empty levels), cancel-of-unknown-id, modify-reduce (priority kept),
modify-increase (priority lost), modify-to-zero, market-order IOC
semantics, and multi-level sweeps.

`examples/smoke_test.cpp` mirrors these same checks with plain
`assert()` so you can verify correctness with nothing but `g++`:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/order_book.cpp examples/smoke_test.cpp -o smoke_test
./smoke_test
```

This was run in the environment that produced this repo and all 9
scenario groups passed.

## Performance

Google Benchmark requires a network fetch on first CMake configure, so
this repo also ships a dependency-free `std::chrono` probe
(`examples/latency_probe.cpp`) that needs nothing but `g++`. Numbers
below are **real measurements from that probe**, run on the sandbox
machine that built this repo (a shared/virtualized CI-style VM, *not* a
tuned bare-metal box — expect meaningfully better and more stable
numbers on real hardware with CPU pinning, huge pages, and
`-march=native`):

```
$ g++ -std=c++17 -O3 -march=native -DNDEBUG -Iinclude src/order_book.cpp examples/latency_probe.cpp -o latency_probe
$ ./latency_probe

=== Order Book Engine -- std::chrono micro-benchmark (N=2000000) ===
Add order (non-crossing)             416.4 ns/op   (2.40M ops/sec)
Cancel order                         262.0 ns/op   (3.82M ops/sec)
Add+match (mixed workload)           140.9 ns/op   (7.10M ops/sec)
```

For an apples-to-apples comparison with other engines, run the real
Google Benchmark suite (`./bench_order_book`) once you've built with
CMake — it reports proper statistically-repeated timings with warm-up,
which is what you'd want in a README performance table for a portfolio
submission rather than a single-shot `chrono` measurement. Re-run
`latency_probe`/`bench_order_book` on your own machine and drop your
numbers into this section — that's more credible to a reviewer than a
number copied from someone else's hardware.

## Design choices worth calling out in an interview

- **Zero heap allocation on the order add/cancel hot path** via the
  intrusive linked list + `MemoryPool<Order>` — the only place that
  touches the global allocator is when a `MemoryPool` chunk itself
  needs to grow, which is amortized to effectively never after warm-up.
- **Fixed-point integer prices**, not floats, to avoid rounding bugs in
  price comparisons — a real bug class in naive matching engines.
- **`std::map` for price levels, not a flat vector or custom AVL tree**:
  this was a deliberate trade-off. A hand-rolled red-black/AVL tree
  would shave constant factors, but `std::map`'s red-black tree already
  gives O(log n) worst-case level operations with far less code and bug
  surface — and levels are created/destroyed far less often than
  individual order adds/cancels, which are the true hot path and are
  O(1) here regardless of the tree.
- **Cancel/modify never re-scans the book** — the `OrderId -> Order*`
  hash map plus each `Order`'s embedded `level` back-pointer means
  cancellation is O(1) average-case exactly as the "Order Lookup Map"
  requirement specifies, not just "fast."

## Possible extensions

- Multi-threaded order ingestion with a lock-free SPSC queue feeding a
  single-threaded matching core (the standard low-latency exchange
  pattern — a single writer thread avoids needing to make the book
  itself thread-safe).
- Persisted event log (append-only) for deterministic replay/recovery.
- Iceberg / hidden-quantity order support.
- A FIX or simple binary protocol adapter on top of the existing
  `OrderBook` API.
