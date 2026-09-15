// A dependency-free latency/throughput probe using std::chrono, so real
// numbers can be produced for the README even without Google Benchmark
// installed. The proper Google-Benchmark-based suite lives in
// benchmarks/bench_order_book.cpp for use once you build with CMake.
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include "order_book.hpp"

using namespace obe;
using Clock = std::chrono::steady_clock;

static double ns_per_op(Clock::duration total, std::size_t ops) {
    return std::chrono::duration<double, std::nano>(total).count() / static_cast<double>(ops);
}

int main() {
    constexpr std::size_t N = 2'000'000;
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> price_dist(9900, 10100); // 200-tick spread of resting liquidity
    std::uniform_int_distribution<int> qty_dist(1, 50);
    std::bernoulli_distribution side_dist(0.5);

    OrderBook book(1 << 20);

    // --- Benchmark 1: Add (resting, non-crossing) orders ---
    std::vector<OrderId> ids;
    ids.reserve(N);
    auto t0 = Clock::now();
    for (std::size_t i = 0; i < N; ++i) {
        OrderId id = i + 1;
        Side side = side_dist(rng) ? Side::Buy : Side::Sell;
        // Keep bids strictly below asks so nothing crosses -- we're timing
        // pure "add to book" cost, not matching cost, in this pass.
        Price price = side == Side::Buy ? price_dist(rng) - 500 : price_dist(rng) + 500;
        book.addLimitOrder(id, side, price, static_cast<Quantity>(qty_dist(rng)), i);
        ids.push_back(id);
    }
    auto t1 = Clock::now();
    double add_ns = ns_per_op(t1 - t0, N);

    // --- Benchmark 2: Cancel every order just added ---
    std::shuffle(ids.begin(), ids.end(), rng); // cancel in random order, not FIFO
    auto t2 = Clock::now();
    for (auto id : ids) {
        book.cancelOrder(id);
    }
    auto t3 = Clock::now();
    double cancel_ns = ns_per_op(t3 - t2, N);

    // --- Benchmark 3: Mixed add+match workload (crossing orders) ---
    OrderBook book2(1 << 20);
    // Seed a deep book first.
    for (int i = 0; i < 100000; ++i) {
        book2.addLimitOrder(1'000'000 + i, Side::Sell, 10000 + (i % 500), 10, i);
        book2.addLimitOrder(2'000'000 + i, Side::Buy, 9500 - (i % 500), 10, i);
    }
    std::uniform_int_distribution<int> cross_price(9800, 10200);
    std::size_t total_fills = 0;
    auto t4 = Clock::now();
    for (std::size_t i = 0; i < N; ++i) {
        Side side = side_dist(rng) ? Side::Buy : Side::Sell;
        auto fills = book2.addLimitOrder(3'000'000 + i, side, cross_price(rng), static_cast<Quantity>(qty_dist(rng)), i);
        total_fills += fills.size();
    }
    auto t5 = Clock::now();
    double match_ns = ns_per_op(t5 - t4, N);

    printf("=== Order Book Engine -- std::chrono micro-benchmark (N=%zu) ===\n", N);
    printf("%-32s %10.1f ns/op   (%.2fM ops/sec)\n", "Add order (non-crossing)", add_ns, 1000.0 / add_ns);
    printf("%-32s %10.1f ns/op   (%.2fM ops/sec)\n", "Cancel order", cancel_ns, 1000.0 / cancel_ns);
    printf("%-32s %10.1f ns/op   (%.2fM ops/sec)\n", "Add+match (mixed workload)", match_ns, 1000.0 / match_ns);
    printf("Total fills generated in mixed workload: %zu\n", total_fills);
    printf("Final book depth: %zu bid levels, %zu ask levels, %zu resting orders\n",
           book2.bidLevelCount(), book2.askLevelCount(), book2.orderCount());
    return 0;
}
