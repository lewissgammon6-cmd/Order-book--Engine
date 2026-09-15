#pragma once
#include <cstddef>
#include <vector>
#include <memory>
#include <cassert>

namespace obe {

// A simple fixed-block free-list allocator. Blocks of sizeof(T) are carved
// out of large contiguous chunks allocated up front; acquire()/release()
// are O(1) and never touch the global new/delete allocator once the pool
// has warmed up, which matters on the order-add/cancel hot path where
// heap allocation latency (and its lock contention under threads) is one
// of the biggest sources of tail latency in a matching engine.
template <typename T>
class MemoryPool {
public:
    explicit MemoryPool(std::size_t chunk_capacity = 4096)
        : chunk_capacity_(chunk_capacity) {
        grow();
    }

    // Non-copyable: a pool owns raw memory that objects' addresses point into.
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    ~MemoryPool() {
        for (auto& chunk : chunks_) {
            ::operator delete(chunk);
        }
    }

    template <typename... Args>
    T* acquire(Args&&... args) {
        if (!free_list_) {
            grow();
        }
        Node* node = free_list_;
        free_list_ = free_list_->next;
        T* obj = std::launder(reinterpret_cast<T*>(node));
        new (obj) T(std::forward<Args>(args)...); // placement-new construct
        return obj;
    }

    void release(T* obj) {
        obj->~T();
        Node* node = reinterpret_cast<Node*>(obj);
        node->next = free_list_;
        free_list_ = node;
    }

    std::size_t capacity() const { return chunks_.size() * chunk_capacity_; }

private:
    union Node {
        Node* next;
        alignas(T) unsigned char storage[sizeof(T)];
    };
    static_assert(sizeof(Node) >= sizeof(T), "Node must fit T");

    void grow() {
        auto* chunk = static_cast<Node*>(::operator new(sizeof(Node) * chunk_capacity_));
        chunks_.push_back(chunk);
        // thread the new block's nodes onto the free list
        for (std::size_t i = 0; i < chunk_capacity_; ++i) {
            chunk[i].next = free_list_;
            free_list_ = &chunk[i];
        }
    }

    std::size_t chunk_capacity_;
    Node* free_list_ = nullptr;
    std::vector<Node*> chunks_;
};

} // namespace obe
