#include "order_book.hpp"
#include <algorithm>

namespace obe {

template <typename BookSide>
void OrderBook::matchAgainst(BookSide& opposite, Side aggressor_side, Price limit_price,
                              bool is_market, Quantity& remaining, OrderId aggressor_id,
                              std::vector<Fill>& fills) {
    while (remaining > 0 && !opposite.empty()) {
        auto it = opposite.begin();
        const Price level_price = it->first;

        if (!is_market) {
            const bool price_crosses = (aggressor_side == Side::Buy)
                                            ? (level_price <= limit_price)
                                            : (level_price >= limit_price);
            if (!price_crosses) break; // no more matchable levels for a limit order
        }

        PriceLevel& level = it->second;
        while (remaining > 0 && level.head) {
            Order* resting = level.head;
            const Quantity traded = std::min(remaining, resting->quantity);

            fills.push_back(Fill{resting->id, aggressor_id, level_price, traded});
            remaining -= traded;

            if (traded == resting->quantity) {
                // Resting order fully consumed: unlink (which correctly
                // decrements the level's aggregate by its full remaining
                // quantity) and return it to the pool.
                level.unlink(resting);
                order_lookup_.erase(resting->id);
                pool_.release(resting);
            } else {
                resting->quantity -= traded;
                level.total_quantity -= traded;
            }
        }

        if (level.empty()) {
            opposite.erase(it);
        }
    }
}

void OrderBook::restOrder(Side side, OrderId id, Price price, Quantity quantity, Timestamp ts) {
    Order* order = pool_.acquire();
    order->id = id;
    order->side = side;
    order->price = price;
    order->quantity = quantity;
    order->timestamp = ts;
    order->prev = order->next = nullptr;
    order->level = nullptr;

    if (side == Side::Buy) {
        PriceLevel& level = bids_[price];
        level.price = price;
        level.push_back(order);
    } else {
        PriceLevel& level = asks_[price];
        level.price = price;
        level.push_back(order);
    }
    order_lookup_[id] = order;
}

std::vector<Fill> OrderBook::addLimitOrder(OrderId id, Side side, Price price, Quantity quantity, Timestamp ts) {
    std::vector<Fill> fills;
    Quantity remaining = quantity;

    if (side == Side::Buy) {
        matchAgainst(asks_, side, price, /*is_market=*/false, remaining, id, fills);
    } else {
        matchAgainst(bids_, side, price, /*is_market=*/false, remaining, id, fills);
    }

    if (remaining > 0) {
        restOrder(side, id, price, remaining, ts);
    }
    return fills;
}

std::vector<Fill> OrderBook::addMarketOrder(OrderId id, Side side, Quantity quantity, Timestamp ts) {
    std::vector<Fill> fills;
    Quantity remaining = quantity;

    if (side == Side::Buy) {
        matchAgainst(asks_, side, /*limit_price=*/0, /*is_market=*/true, remaining, id, fills);
    } else {
        matchAgainst(bids_, side, /*limit_price=*/0, /*is_market=*/true, remaining, id, fills);
    }
    // Any unfilled remainder is dropped: this models an IOC (immediate-or-
    // cancel) market order rather than resting an unpriced order in the book.
    (void)ts;
    return fills;
}

template <typename Map>
void OrderBook::eraseLevelIfEmpty(Map& side_map, typename Map::iterator it) {
    if (it != side_map.end() && it->second.empty()) {
        side_map.erase(it);
    }
}

ResultCode OrderBook::cancelOrder(OrderId id) {
    auto it = order_lookup_.find(id);
    if (it == order_lookup_.end()) return ResultCode::NotFound;

    Order* order = it->second;
    PriceLevel* level = order->level;
    const Side side = order->side;
    const Price price = order->price;

    level->unlink(order);
    order_lookup_.erase(it);
    pool_.release(order);

    if (level->empty()) {
        if (side == Side::Buy) eraseLevelIfEmpty(bids_, bids_.find(price));
        else eraseLevelIfEmpty(asks_, asks_.find(price));
    }
    return ResultCode::Cancelled;
}

ResultCode OrderBook::modifyOrder(OrderId id, Quantity new_quantity, Timestamp ts) {
    auto it = order_lookup_.find(id);
    if (it == order_lookup_.end()) return ResultCode::NotFound;
    Order* order = it->second;

    if (new_quantity == 0) {
        cancelOrder(id);
        return ResultCode::Cancelled;
    }

    if (new_quantity <= order->quantity) {
        // Reducing quantity keeps the order's place in the FIFO queue --
        // matches real exchange "reduce" semantics.
        const Quantity delta = order->quantity - new_quantity;
        order->quantity = new_quantity;
        order->level->total_quantity -= delta;
        return ResultCode::Modified;
    }

    // Increasing quantity is treated as cancel + re-add at the back of the
    // queue, since most venues revoke time priority when size increases.
    const Side side = order->side;
    const Price price = order->price;
    cancelOrder(id);
    restOrder(side, id, price, new_quantity, ts);
    return ResultCode::Modified;
}

bool OrderBook::bestBid(Price& out_price, Quantity& out_qty) const {
    if (bids_.empty()) return false;
    auto it = bids_.begin();
    out_price = it->first;
    out_qty = it->second.total_quantity;
    return true;
}

bool OrderBook::bestAsk(Price& out_price, Quantity& out_qty) const {
    if (asks_.empty()) return false;
    auto it = asks_.begin();
    out_price = it->first;
    out_qty = it->second.total_quantity;
    return true;
}

// Explicit template instantiations for the two concrete map types used.
template void OrderBook::matchAgainst(std::map<Price, PriceLevel, std::greater<Price>>&, Side, Price, bool, Quantity&, OrderId, std::vector<Fill>&);
template void OrderBook::matchAgainst(std::map<Price, PriceLevel, std::less<Price>>&, Side, Price, bool, Quantity&, OrderId, std::vector<Fill>&);
template void OrderBook::eraseLevelIfEmpty(std::map<Price, PriceLevel, std::greater<Price>>&, std::map<Price, PriceLevel, std::greater<Price>>::iterator);
template void OrderBook::eraseLevelIfEmpty(std::map<Price, PriceLevel, std::less<Price>>&, std::map<Price, PriceLevel, std::less<Price>>::iterator);

} // namespace obe
