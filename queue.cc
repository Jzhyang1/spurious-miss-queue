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
class MyQueue2Double {
    Node<T> dummy;
    std::atomic<Node<T>*> first{&dummy};
    std::atomic<std::atomic<Node<T>*>*> last{&dummy.next};

public:
    void push(Node<T>* n) {
        n->next.store(nullptr, std::memory_order_relaxed);
        std::atomic<Node<T>*>* prev = last.exchange(&n->next, std::memory_order_acq_rel);
        prev->store(n, std::memory_order_release);
    }

    bool pop(Node<T>*& ret) {
        auto my_first = first.exchange(nullptr); // de-facto lock
        if (my_first == nullptr) {
            my_first = first.exchange(nullptr); // double check
        }
        if (my_first == nullptr) { 
            return false; // queue is locked
        }

        auto next = my_first->next.exchange(nullptr);
        if (next == nullptr) {
            first.store(my_first); // restore dummy
            return false; // queue is empty
        }
        
        // at this point, my_first should be completely separated
        if (my_first == &dummy) {
            auto prev = last.exchange(&my_first->next);
            prev->store(my_first);

            my_first = next; // recreate the virtual state of just acquiring my_first
            next = my_first->next.exchange(nullptr); // we know next was not nullptr, so we use it
            // we also know that there is at minimum a dummy node so we proceed
            // !! but what if someone was adding and we happen to read next in an inconsistent state? !!
        }

        // advance the queue (we know this is safe from adds
        // because there is always at least the dummy in between
        // and we know that we are not the dummy
        first.store(next);
        ret = my_first;
        return true;
    }
};
