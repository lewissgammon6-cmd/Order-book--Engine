#pragma once
#include "types.hpp"

namespace obe {

struct PriceLevel; // forward declaration; Order keeps a back-pointer to it
                    // so cancel/modify can update level aggregates in O(1)
                    // without a second lookup.

// An intrusive doubly-linked-list node. Orders resting at the same price
// are chained in FIFO order (time priority): new orders are appended at
// the tail, and matching always consumes from the head first.
struct Order {
    OrderId id{};
    Side side{};
    Price price{};
    Quantity quantity{};
    Timestamp timestamp{};

    Order* prev = nullptr;
    Order* next = nullptr;
    PriceLevel* level = nullptr;
};

// All resting orders at a single price. A doubly-linked list gives O(1)
// append (new order at back), O(1) pop-front (match oldest order first),
// and O(1) removal of an arbitrary order given its pointer (cancel).
struct PriceLevel {
    Price price{};
    Order* head = nullptr; // oldest order (matched first)
    Order* tail = nullptr; // newest order
    Quantity total_quantity = 0;
    std::size_t order_count = 0;

    bool empty() const { return head == nullptr; }

    void push_back(Order* order) {
        order->level = this;
        order->prev = tail;
        order->next = nullptr;
        if (tail) tail->next = order;
        tail = order;
        if (!head) head = order;
        total_quantity += order->quantity;
        ++order_count;
    }

    // O(1) removal given a pointer already known to live in this level.
    void unlink(Order* order) {
        if (order->prev) order->prev->next = order->next;
        else head = order->next;

        if (order->next) order->next->prev = order->prev;
        else tail = order->prev;

        order->prev = order->next = nullptr;
        total_quantity -= order->quantity;
        --order_count;
    }
};

} // namespace obe
