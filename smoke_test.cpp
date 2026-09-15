// Standalone smoke test: exercises matching, price-time priority,
// partial fills, cancel, and modify -- with plain assert() so it can be
// compiled and run with nothing but g++ (no GTest/CMake required).
#include <cassert>
#include <cstdio>
#include "order_book.hpp"

using namespace obe;

static void test_basic_cross() {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 100, 1); // ask 100.00 x100
    auto fills = book.addLimitOrder(2, Side::Buy, 10000, 100, 2); // crosses fully
    assert(fills.size() == 1);
    assert(fills[0].quantity == 100);
    assert(fills[0].price == 10000);
    assert(!book.hasOrder(1));
    assert(!book.hasOrder(2));
    assert(book.orderCount() == 0);
    printf("test_basic_cross OK\n");
}

static void test_price_time_priority() {
    OrderBook book;
    // Two resting asks at the same price; order 1 arrived first and must
    // be filled before order 2 (FIFO time priority).
    book.addLimitOrder(1, Side::Sell, 10000, 50, 1);
    book.addLimitOrder(2, Side::Sell, 10000, 50, 2);
    auto fills = book.addLimitOrder(3, Side::Buy, 10000, 60, 3);
    assert(fills.size() == 2);
    assert(fills[0].resting_order_id == 1 && fills[0].quantity == 50);
    assert(fills[1].resting_order_id == 2 && fills[1].quantity == 10);
    assert(book.hasOrder(2)); // partially filled, still resting with qty 40
    printf("test_price_time_priority OK\n");
}

static void test_partial_fill_and_resting_remainder() {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 30, 1);
    auto fills = book.addLimitOrder(2, Side::Buy, 10000, 100, 2);
    assert(fills.size() == 1 && fills[0].quantity == 30);
    // 70 remaining should now rest on the bid side
    Price bid_px; Quantity bid_qty;
    assert(book.bestBid(bid_px, bid_qty));
    assert(bid_px == 10000 && bid_qty == 70);
    printf("test_partial_fill_and_resting_remainder OK\n");
}

static void test_no_cross_when_price_does_not_meet() {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10100, 10, 1); // ask 101.00
    auto fills = book.addLimitOrder(2, Side::Buy, 10000, 10, 2); // bid 100.00, doesn't cross
    assert(fills.empty());
    Price bid_px, ask_px; Quantity q;
    assert(book.bestBid(bid_px, q) && bid_px == 10000);
    assert(book.bestAsk(ask_px, q) && ask_px == 10100);
    printf("test_no_cross_when_price_does_not_meet OK\n");
}

static void test_cancel() {
    OrderBook book;
    book.addLimitOrder(1, Side::Buy, 9900, 20, 1);
    assert(book.hasOrder(1));
    auto rc = book.cancelOrder(1);
    assert(rc == ResultCode::Cancelled);
    assert(!book.hasOrder(1));
    assert(book.bidLevelCount() == 0); // level should be cleaned up
    assert(book.cancelOrder(999) == ResultCode::NotFound);
    printf("test_cancel OK\n");
}

static void test_modify_reduce_keeps_priority() {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 50, 1);
    book.addLimitOrder(2, Side::Sell, 10000, 50, 2);
    book.modifyOrder(1, 20, 3); // order 1 reduces to 20, keeps front-of-queue
    auto fills = book.addLimitOrder(3, Side::Buy, 10000, 30, 4);
    assert(fills.size() == 2);
    assert(fills[0].resting_order_id == 1 && fills[0].quantity == 20); // still filled first
    assert(fills[1].resting_order_id == 2 && fills[1].quantity == 10);
    printf("test_modify_reduce_keeps_priority OK\n");
}

static void test_modify_increase_loses_priority() {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 10, 1);
    book.addLimitOrder(2, Side::Sell, 10000, 10, 2);
    book.modifyOrder(1, 100, 3); // increase -> re-queued behind order 2
    auto fills = book.addLimitOrder(3, Side::Buy, 10000, 15, 4);
    assert(fills.size() == 2);
    assert(fills[0].resting_order_id == 2); // order 2 now has priority
    assert(fills[1].resting_order_id == 1);
    printf("test_modify_increase_loses_priority OK\n");
}

static void test_market_order_ioc() {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 5, 1);
    auto fills = book.addMarketOrder(2, Side::Buy, 20, 2); // only 5 available
    assert(fills.size() == 1 && fills[0].quantity == 5);
    // remaining 15 should NOT rest in the book (IOC semantics)
    assert(book.orderCount() == 0);
    printf("test_market_order_ioc OK\n");
}

static void test_multi_level_sweep() {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 10, 1);
    book.addLimitOrder(2, Side::Sell, 10100, 10, 2);
    book.addLimitOrder(3, Side::Sell, 10200, 10, 3);
    auto fills = book.addLimitOrder(4, Side::Buy, 10200, 25, 4);
    assert(fills.size() == 3);
    assert(fills[0].price == 10000 && fills[0].quantity == 10);
    assert(fills[1].price == 10100 && fills[1].quantity == 10);
    assert(fills[2].price == 10200 && fills[2].quantity == 5);
    Price bid_px; Quantity bid_qty;
    assert(book.bestAsk(bid_px, bid_qty)); // level at 10200 still has 5 left
    assert(bid_px == 10200 && bid_qty == 5);
    printf("test_multi_level_sweep OK\n");
}

int main() {
    test_basic_cross();
    test_price_time_priority();
    test_partial_fill_and_resting_remainder();
    test_no_cross_when_price_does_not_meet();
    test_cancel();
    test_modify_reduce_keeps_priority();
    test_modify_increase_loses_priority();
    test_market_order_ioc();
    test_multi_level_sweep();
    printf("\nAll smoke tests passed.\n");
    return 0;
}
