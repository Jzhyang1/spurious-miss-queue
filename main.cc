#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <vector>
#include <chrono>
#include <cassert>

template <typename T>
struct Node {
    std::atomic<Node*> next;
    T val;
    Node(): next(nullptr), val(T()) {}
    Node(const T& v): next(nullptr), val(v) {}
    Node(T&& v): next(nullptr), val(std::move(v)) {}
};

// =============================================
// 1. Simple Mutex Queue
// =============================================
template<typename T>
class MutexQueue {
    Node<T>* first = nullptr, *last = nullptr;
    std::mutex m;
public:
    void push(Node<T>* t) {
        std::lock_guard<std::mutex> lock(m);
        if (first == nullptr) {
            // Empty
            first = last = t;
        } else {
            last->next.store(t);
            last = t;
        }
    }
    bool pop(Node<T>*& val) {
        std::lock_guard<std::mutex> lock(m);
        if (first == nullptr) return false;
        val = first;
        first = first->next.load();
        return true;
    }
};

// =============================================
// 2. Mutex Queue with spurious misses
// =============================================
template<typename T>
class MutexQueueMiss {
    Node<T>* first = nullptr, *last = nullptr;
    std::mutex m;
public:
    void push(Node<T>* t) {
        std::lock_guard<std::mutex> lock(m);
        if (first == nullptr) {
            // Empty
            first = last = t;
        } else {
            last->next.store(t);
            last = t;
        }
    }
    bool pop(Node<T>*& val) {
        if (!m.try_lock()) {
            return false;
        }
        if (first == nullptr) {
            m.unlock();
            return false;
        }
        val = first;
        first = first->next.load();
        m.unlock();
        return true;
    }
};

// =============================================
// 3. Michael Scott Queue
// =============================================
template<typename T>
class MSQueue {
    std::atomic<Node<T>*> head;
    std::atomic<Node<T>*> tail;

public:
    MSQueue() {
        Node<T>* dummy = new Node<T>(); // dummy node
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    // Enqueue (push) - multiple producers allowed
    void push(Node<T>* node) {
        Node<T>* old_tail = nullptr;

        while (true) {
            old_tail = tail.load(std::memory_order_acquire);
            Node<T>* tail_next = old_tail->next.load(std::memory_order_acquire);

            // Is tail really the last node?
            if (old_tail == tail.load(std::memory_order_acquire)) {
                if (tail_next == nullptr) {
                    // Try to link new node at the end
                    if (old_tail->next.compare_exchange_weak(
                        tail_next, node,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        // Enqueue done; try to swing tail to the inserted node
                        tail.compare_exchange_strong(
                            old_tail, node,
                            std::memory_order_release,
                            std::memory_order_relaxed);
                        return;
                    }
                } else {
                    // Tail not pointing to last node, try to swing it forward
                    tail.compare_exchange_weak(
                        old_tail, tail_next,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                }
            }
            // otherwise retry
        }
    }

    // Dequeue (pop) - multiple consumers allowed
    // Returns false if queue empty; true and sets 'result' otherwise.
    bool pop(Node<T>*& result) {
        while (true) {
            Node<T>* old_head = head.load(std::memory_order_acquire);
            Node<T>* old_tail = tail.load(std::memory_order_acquire);
            Node<T>* head_next = old_head->next.load(std::memory_order_acquire);

            if (old_head == head.load(std::memory_order_acquire)) {
                if (old_head == old_tail) {
                    // Queue might be empty
                    if (head_next == nullptr) {
                        return false; // empty
                    }
                    // tail is falling behind; try to advance it
                    tail.compare_exchange_weak(
                        old_tail, head_next,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                } else {
                    // Nobody within the queue can delete memory
                    // Try to move head forward to head_next
                    if (head.compare_exchange_weak(
                        old_head, head_next,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        // Successfully removed old_head (dummy). Safe to delete.
                        result = head_next;
                        return true;
                    }
                    // else retry
                }
            }
        }
    }
};

// =============================================
// 4. Michael Scott Queue with Spurious Miss
// =============================================
template<typename T>
class MSQueueMiss {
    std::atomic<Node<T>*> head;
    std::atomic<Node<T>*> tail;

public:
    MSQueueMiss() {
        Node<T>* dummy = new Node<T>(); // dummy node
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    // Enqueue (push) - multiple producers allowed
    void push(Node<T>* node) {
        Node<T>* old_tail = nullptr;

        while (true) {
            old_tail = tail.load(std::memory_order_acquire);
            Node<T>* tail_next = old_tail->next.load(std::memory_order_acquire);

            // Is tail really the last node?
            if (old_tail == tail.load(std::memory_order_acquire)) {
                if (tail_next == nullptr) {
                    // Try to link new node at the end
                    if (old_tail->next.compare_exchange_weak(
                        tail_next, node,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        // Enqueue done; try to swing tail to the inserted node
                        tail.compare_exchange_strong(
                            old_tail, node,
                            std::memory_order_release,
                            std::memory_order_relaxed);
                        return;
                    }
                } else {
                    // Tail not pointing to last node, try to swing it forward
                    tail.compare_exchange_weak(
                        old_tail, tail_next,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                }
            }
            // otherwise retry
        }
    }

    // Dequeue (pop) - multiple consumers allowed
    // Returns false if queue empty; true and sets 'result' otherwise.
    bool pop(Node<T>*& result) {
        while (true) {
            Node<T>* old_head = head.load(std::memory_order_acquire);
            Node<T>* old_tail = tail.load(std::memory_order_acquire);
            Node<T>* head_next = old_head->next.load(std::memory_order_acquire);

            if (old_head == head.load(std::memory_order_acquire)) {
                if (old_head == old_tail) {
                    // Queue might be empty
                    if (head_next == nullptr) {
                        return false; // empty
                    }
                    // tail is falling behind; try to advance it
                    tail.compare_exchange_weak(
                        old_tail, head_next,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                } else {
                    // Nobody within the queue can delete memory
                    // Try to move head forward to head_next
                    if (head.compare_exchange_weak(
                        old_head, head_next,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        // Successfully removed old_head (dummy). Safe to delete.
                        result = head_next;
                        return true;
                    }
                    // ELSE SHORT CIRCUIT
                    return false;
                }
            }
        }
    }
};

// =============================================
// 5. Two lock queue
// =============================================
template <typename T>
class TwoLockQueue {
    Node<T>* head, * tail;
    std::mutex head_lock;
    std::mutex tail_lock;

public:
    TwoLockQueue() {
        Node<T>* dummy = new Node<T>();
        head = tail = dummy;
    }

    void push(Node<T>* node) {
        std::lock_guard<std::mutex> guard(tail_lock);
        tail->next = node;
        tail = node;
    }

    // Returns false if empty
    bool pop(Node<T>*& result) {
        std::lock_guard<std::mutex> guard(head_lock);
        Node<T>* node = head;
        Node<T>* new_head = node->next;
        if (new_head == nullptr) {
            return false; // empty
        }
        result = new_head;
        head = new_head;
        return true;
    }
};

// =============================================
// 6. Two lock queue
// =============================================
template <typename T>
class TwoLockQueueMiss {
    Node<T>* head, * tail;
    std::mutex head_lock;
    std::mutex tail_lock;

public:
    TwoLockQueueMiss() {
        Node<T>* dummy = new Node<T>();
        head = tail = dummy;
    }

    void push(Node<T>* node) {
        // add must succeed
        std::lock_guard<std::mutex> guard(tail_lock);
        tail->next = node;
        tail = node;
    }

    // Returns false if empty
    bool pop(Node<T>*& result) {
        // pop does not need to succeed
        if (!head_lock.try_lock()) return false;

        Node<T>* node = head;
        Node<T>* new_head = node->next;
        if (new_head == nullptr) {
            head_lock.unlock();
            return false; // empty
        }
        head = new_head;
        head_lock.unlock();
        result = new_head;
        return true;
    }
};


// =============================================
// 7. My implementation
// =============================================

// We implement a naive form of a lock-free queue
//  with a tiny modification that allows spurious misses
//  and prevents multiple consumers (for ABA and hazard
//  simplicity)
// This is O(1) for add/remove/remove_all

template <typename T>
class MyQueue {
    /*
    Guarantees that can be made about the queue:
    - Multiple producers can add concurrently with 100% success
    Non-guarantees:
    - At any moment in time, at least one consumer can remove an element
    - The removal of an item may fail even if the queue is not empty
    */
    std::atomic<Node<T>*> first{nullptr};
    std::atomic<std::atomic<Node<T>*>*> last{&first};
public:
    void push(Node<T>* t) {
        t->next.store(nullptr);
        auto prev = last.exchange(&t->next);
        prev->store(t);
    }

    bool pop(Node<T>*& ret) {
        auto my_first = first.exchange(nullptr); // de-facto lock
        if (my_first == nullptr) {
            my_first = first.exchange(nullptr); // double check
        }
        if (my_first == nullptr) { 
            return false; // queue is empty
        }

        auto next = my_first->next.load();
        if (next == nullptr) {
            // we removed the last element, reset the queue
            auto expected = &my_first->next;
            if (!last.compare_exchange_strong(expected, &first)) {
                // someone added something, try again later
                // revert first back to its original value
                first.store(my_first);
                return false;
            }
        } else {
            // advance the queue (we know this is safe from adds
            // because last must at least be after &next->next)
            // thus no add can interfere
            first.store(next);
        }
        
        ret = my_first;
        return true;
    }
};

// =============================================
// Benchmark Harness
// =============================================
template <typename Queue>
void run_benchmark(const std::string& name, int num_producers, int num_consumers, int items_per_thread, bool yields) {
    Queue q;
    MyQueue<Node<int>*> freelist;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    const int total_items = num_producers * items_per_thread;

    auto producer = [&](int id) {
        for (int i = 0; i < items_per_thread; i++) {
            q.push(new Node<int>(i + id * items_per_thread));
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto consumer = [&]() {
        Node<int>* val;
        while (consumed.load(std::memory_order_relaxed) < total_items) {
            if (q.pop(val)) {
                freelist.push(new Node<Node<int>*>(val));
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else if (yields) {
                std::this_thread::yield();
            }
        }
    };

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_producers; i++)
        threads.emplace_back(producer, i);
    for (int i = 0; i < num_consumers; i++)
        threads.emplace_back(consumer);

    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    auto ms = ns / 1000000;

    std::cout << name << ": " << total_items / ms << " ops/ms"
              << " (" << total_items << " items in " << ms << "ms)\n";

    // delete everything from freelist
    Node<Node<int>*>* node;
    while (freelist.pop(node)) {
        delete node->val;
        delete node;
    }
}

// =============================================
// Main
// =============================================
extern "C" int main() {
    int producers = 4;
    int consumers = 4;
    int items = 1000000;

    run_benchmark<MutexQueue<int>>("MutexQueue", producers, consumers, items, true);
    run_benchmark<MutexQueueMiss<int>>("MutexQueueMiss", producers, consumers, items, true);
    run_benchmark<MSQueue<int>>("MSQueue", producers, consumers, items, true);
    run_benchmark<MSQueueMiss<int>>("MSQueueMiss", producers, consumers, items, true);
    run_benchmark<TwoLockQueue<int>>("TwoLockQueue", producers, consumers, items, true);
    run_benchmark<TwoLockQueueMiss<int>>("TwoLockQueueMiss", producers, consumers, items, true);
    run_benchmark<MyQueue<int>>("MyQueue", producers, consumers, items, true);

    run_benchmark<MutexQueue<int>>("MutexQueue", producers, consumers, items, false);
    run_benchmark<MutexQueueMiss<int>>("MutexQueueMiss", producers, consumers, items, false);
    run_benchmark<MSQueue<int>>("MSQueue", producers, consumers, items, false);
    run_benchmark<MSQueueMiss<int>>("MSQueueMiss", producers, consumers, items, false);
    run_benchmark<TwoLockQueue<int>>("TwoLockQueue", producers, consumers, items, false);
    run_benchmark<TwoLockQueueMiss<int>>("TwoLockQueueMiss", producers, consumers, items, false);
    run_benchmark<MyQueue<int>>("MyQueue", producers, consumers, items, false);
}
