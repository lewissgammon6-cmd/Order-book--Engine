#include <gtest/gtest.h>
#include "order_book.hpp"

using namespace obe;

TEST(OrderBook, BasicCross) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 100, 1);
    auto fills = book.addLimitOrder(2, Side::Buy, 10000, 100, 2);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].quantity, 100u);
    EXPECT_EQ(fills[0].price, 10000);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.hasOrder(2));
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST(OrderBook, PriceTimePriorityWithinLevel) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 50, 1);
    book.addLimitOrder(2, Side::Sell, 10000, 50, 2);

    auto fills = book.addLimitOrder(3, Side::Buy, 10000, 60, 3);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].resting_order_id, 1u);
    EXPECT_EQ(fills[0].quantity, 50u);
    EXPECT_EQ(fills[1].resting_order_id, 2u);
    EXPECT_EQ(fills[1].quantity, 10u);
    EXPECT_TRUE(book.hasOrder(2)); // partially filled remainder still resting
}

TEST(OrderBook, PriceImprovementAcrossLevels) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10100, 10, 1);
    book.addLimitOrder(2, Side::Sell, 10000, 10, 2); // better price, arrived second

    auto fills = book.addLimitOrder(3, Side::Buy, 10200, 10, 3);
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].resting_order_id, 2u); // best price wins over time priority across levels
    EXPECT_EQ(fills[0].price, 10000);
}

TEST(OrderBook, PartialFillLeavesRemainderResting) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 30, 1);
    auto fills = book.addLimitOrder(2, Side::Buy, 10000, 100, 2);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].quantity, 30u);

    Price bid_px; Quantity bid_qty;
    ASSERT_TRUE(book.bestBid(bid_px, bid_qty));
    EXPECT_EQ(bid_px, 10000);
    EXPECT_EQ(bid_qty, 70u);
}

TEST(OrderBook, NoCrossWhenPricesDoNotMeet) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10100, 10, 1);
    auto fills = book.addLimitOrder(2, Side::Buy, 10000, 10, 2);

    EXPECT_TRUE(fills.empty());
    Price bid_px, ask_px; Quantity q;
    EXPECT_TRUE(book.bestBid(bid_px, q));
    EXPECT_TRUE(book.bestAsk(ask_px, q));
    EXPECT_EQ(bid_px, 10000);
    EXPECT_EQ(ask_px, 10100);
}

TEST(OrderBook, CancelRemovesOrderAndEmptyLevel) {
    OrderBook book;
    book.addLimitOrder(1, Side::Buy, 9900, 20, 1);
    EXPECT_TRUE(book.hasOrder(1));

    EXPECT_EQ(book.cancelOrder(1), ResultCode::Cancelled);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_EQ(book.bidLevelCount(), 0u);
}

TEST(OrderBook, CancelUnknownOrderReturnsNotFound) {
    OrderBook book;
    EXPECT_EQ(book.cancelOrder(12345), ResultCode::NotFound);
}

TEST(OrderBook, ModifyReduceKeepsTimePriority) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 50, 1);
    book.addLimitOrder(2, Side::Sell, 10000, 50, 2);

    EXPECT_EQ(book.modifyOrder(1, 20, 3), ResultCode::Modified);

    auto fills = book.addLimitOrder(3, Side::Buy, 10000, 30, 4);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].resting_order_id, 1u);
    EXPECT_EQ(fills[0].quantity, 20u);
    EXPECT_EQ(fills[1].resting_order_id, 2u);
    EXPECT_EQ(fills[1].quantity, 10u);
}

TEST(OrderBook, ModifyIncreaseLosesTimePriority) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 10, 1);
    book.addLimitOrder(2, Side::Sell, 10000, 10, 2);

    EXPECT_EQ(book.modifyOrder(1, 100, 3), ResultCode::Modified);

    auto fills = book.addLimitOrder(3, Side::Buy, 10000, 15, 4);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0].resting_order_id, 2u); // order 2 now has priority
    EXPECT_EQ(fills[1].resting_order_id, 1u);
}

TEST(OrderBook, ModifyToZeroCancels) {
    OrderBook book;
    book.addLimitOrder(1, Side::Buy, 9900, 10, 1);
    EXPECT_EQ(book.modifyOrder(1, 0, 2), ResultCode::Cancelled);
    EXPECT_FALSE(book.hasOrder(1));
}

TEST(OrderBook, MarketOrderIsImmediateOrCancel) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 5, 1);
    auto fills = book.addMarketOrder(2, Side::Buy, 20, 2);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].quantity, 5u);
    EXPECT_EQ(book.orderCount(), 0u); // unfilled 15 is dropped, not resting
}

TEST(OrderBook, MultiLevelSweepConsumesBestPricesFirst) {
    OrderBook book;
    book.addLimitOrder(1, Side::Sell, 10000, 10, 1);
    book.addLimitOrder(2, Side::Sell, 10100, 10, 2);
    book.addLimitOrder(3, Side::Sell, 10200, 10, 3);

    auto fills = book.addLimitOrder(4, Side::Buy, 10200, 25, 4);
    ASSERT_EQ(fills.size(), 3u);
    EXPECT_EQ(fills[0].price, 10000);
    EXPECT_EQ(fills[1].price, 10100);
    EXPECT_EQ(fills[2].price, 10200);
    EXPECT_EQ(fills[2].quantity, 5u);

    Price ask_px; Quantity ask_qty;
    ASSERT_TRUE(book.bestAsk(ask_px, ask_qty));
    EXPECT_EQ(ask_px, 10200);
    EXPECT_EQ(ask_qty, 5u);
}

TEST(OrderBook, EmptyBookHasNoBestBidOrAsk) {
    OrderBook book;
    Price p; Quantity q;
    EXPECT_FALSE(book.bestBid(p, q));
    EXPECT_FALSE(book.bestAsk(p, q));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
