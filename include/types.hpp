#pragma once
#include <cstdint>

namespace obe {

using OrderId = std::uint64_t;
using Price = std::int64_t;   // fixed-point price in integer ticks (avoids float rounding
                               // issues on the hot path -- see README for rationale)
using Quantity = std::uint64_t;
using Timestamp = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : std::uint8_t { Limit = 0, Market = 1 };

enum class ResultCode : std::uint8_t {
    Accepted,
    FullyFilled,
    PartiallyFilled,
    Rejected,
    NotFound,
    Cancelled,
    Modified
};

struct Fill {
    OrderId resting_order_id;
    OrderId aggressor_order_id;
    Price price;
    Quantity quantity;
};

} // namespace obe
