#include <benchmark/benchmark.h>
#include <random>
#include "order_book.hpp"

using namespace obe;

// Add a non-crossing limit order to an otherwise-empty spot in the book.
// This isolates the cost of the hot "add" path: pool acquire, map[] level
// lookup/insert, linked-list push_back, and hash-map insert.
static void BM_AddOrder(benchmark::State& state) {
    OrderBook book(1 << 20);
    OrderId next_id = 1;
    std::mt19937_64 rng(1);
    std::uniform_int_distribution<int> price_dist(9000, 9900); // always below any ask -> never crosses
    std::uniform_int_distribution<int> qty_dist(1, 100);

    for (auto _ : state) {
        book.addLimitOrder(next_id++, Side::Buy, price_dist(rng), qty_dist(rng), next_id);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddOrder);

// Add then immediately cancel, to isolate cancel-path cost (hash lookup +
// O(1) intrusive unlink + possible empty-level erase).
static void BM_AddThenCancel(benchmark::State& state) {
    OrderBook book(1 << 20);
    OrderId next_id = 1;

    for (auto _ : state) {
        OrderId id = next_id++;
        book.addLimitOrder(id, Side::Buy, 9900, 10, id);
        book.cancelOrder(id);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddThenCancel);

// Steady-state matching throughput against a pre-seeded, continuously
// replenished book -- the closest proxy here to real exchange load.
static void BM_MatchingThroughput(benchmark::State& state) {
    OrderBook book(1 << 20);
    for (int i = 0; i < 50000; ++i) {
        book.addLimitOrder(1'000'000 + i, Side::Sell, 10000 + (i % 200), 10, i);
        book.addLimitOrder(2'000'000 + i, Side::Buy, 9800 - (i % 200), 10, i);
    }

    std::mt19937_64 rng(2);
    std::uniform_int_distribution<int> price_dist(9700, 10300);
    std::uniform_int_distribution<int> qty_dist(1, 30);
    std::bernoulli_distribution side_dist(0.5);
    OrderId next_id = 3'000'000;

    for (auto _ : state) {
        Side side = side_dist(rng) ? Side::Buy : Side::Sell;
        auto fills = book.addLimitOrder(next_id++, side, price_dist(rng), qty_dist(rng), next_id);
        benchmark::DoNotOptimize(fills);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MatchingThroughput);

// Modify (reduce) an already-resting order -- should be nearly free since
// it's just an integer decrement plus an aggregate update, no relinking.
static void BM_ModifyReduce(benchmark::State& state) {
    OrderBook book(1 << 20);
    OrderId id = 1;
    book.addLimitOrder(id, Side::Buy, 9900, 1'000'000'000ULL, 1);

    Quantity qty = 999'999'999ULL;
    for (auto _ : state) {
        book.modifyOrder(id, qty--, 1);
        if (qty < 2) qty = 999'999'999ULL; // never hit zero mid-benchmark
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ModifyReduce);

BENCHMARK_MAIN();
