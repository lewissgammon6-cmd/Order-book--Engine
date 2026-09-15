#pragma once
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>

#include "types.hpp"
#include "order.hpp"
#include "memory_pool.hpp"

namespace obe {

// Price-time priority limit order book and matching engine.
//
// Book sides:
//   bids_: std::map<Price, PriceLevel, greater<>>  -- highest bid first
//   asks_: std::map<Price, PriceLevel, less<>>     -- lowest ask first
// std::map is a red-black tree, giving O(log n) insert/erase of price
// levels and O(1) access to the best bid/ask via begin(). Levels
// themselves hold a FIFO linked list of orders (see order.hpp) for O(1)
// time-priority matching within a level.
//
// order_lookup_: OrderId -> Order* for O(1) direct cancel/modify without
// walking the tree, per the "Order Lookup Map" design requirement.
class OrderBook {
public:
    explicit OrderBook(std::size_t pool_capacity = 1 << 16) : pool_(pool_capacity) {}

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Adds a limit order, matching immediately against the opposite side
    // while price allows, then resting any remaining quantity. Returns
    // the fills generated (for order-fill notifications / P&L updates).
    std::vector<Fill> addLimitOrder(OrderId id, Side side, Price price, Quantity quantity, Timestamp ts);

    // Adds a market order: matches immediately at whatever price levels
    // are available (no resting remainder -- unfilled quantity is
    // cancelled, matching typical exchange semantics for IOC market orders).
    std::vector<Fill> addMarketOrder(OrderId id, Side side, Quantity quantity, Timestamp ts);

    // O(1) average-case cancellation via the order lookup map + intrusive
    // linked-list unlink. Empty price levels are erased from the map
    // (O(log n), off the common cancel path's dominant cost).
    ResultCode cancelOrder(OrderId id);

    // Reduces the resting quantity of an order without losing its place
    // in time priority (matches real exchange "reduce" semantics).
    // Increasing quantity is treated as cancel+re-add, which *does* lose
    // priority -- this mirrors how most venues actually behave.
    ResultCode modifyOrder(OrderId id, Quantity new_quantity, Timestamp ts);

    bool hasOrder(OrderId id) const { return order_lookup_.find(id) != order_lookup_.end(); }

    // Best bid / ask accessors. Returns false if that side is empty.
    bool bestBid(Price& out_price, Quantity& out_qty) const;
    bool bestAsk(Price& out_price, Quantity& out_qty) const;

    std::size_t bidLevelCount() const { return bids_.size(); }
    std::size_t askLevelCount() const { return asks_.size(); }
    std::size_t orderCount() const { return order_lookup_.size(); }

private:
    // std::greater<Price> orders bids highest-first; std::less<Price>
    // (default) orders asks lowest-first. begin() is therefore always
    // the best price on either side.
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>> asks_;
    std::unordered_map<OrderId, Order*> order_lookup_;
    MemoryPool<Order> pool_;

    template <typename BookSide>
    void matchAgainst(BookSide& opposite_side, Side aggressor_side, Price limit_price,
                       bool is_market, Quantity& remaining, OrderId aggressor_id,
                       std::vector<Fill>& fills);

    void restOrder(Side side, OrderId id, Price price, Quantity quantity, Timestamp ts);

    template <typename Map>
    void eraseLevelIfEmpty(Map& side_map, typename Map::iterator it);
};

} // namespace obe
