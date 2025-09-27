# What is happening?
In summary, I am trying to improve throughput in a round-robin scheduler in the kernel 
(choice of round-robin is questionable, but maybe something will come out of it).

## A description of the queue
We force multiple-producer multiple-consumer situations into multiple-producer single-consumer situations 
to utilize the Vyukov Queue design. However, we also include additional logic to prevent the hazards
from the Vyukov Queue. Two solutions include check-and-undo and sentinel-node. The sentinel node solution
provides better guarantees on the number of consumers that may successfully `pop` at a time (if there is an 
item) and performed better than the check-and-undo solution. The queue is faster than most traditional queue 
designs that satisfy our requirements (most traditional lock-free queues don't satisfy the memory hazard 
requirement).

## Requirements/assumptions/tradeoffs

We make the following requirements:
 - The queue must succeed on every `push`, but may fail on `pop` in a multithreaded environment so long as data is not lost
 - The queue must not perform dynamic memory operations (all add/remove objects come with pre-allocated space, no more can be added)
 - The queue should not have dramatic spikes in latency in any of their operations
 - The queue cannot make assumptions about the validity of memory of items returned from the queue (memory hazards)

We make the following assumptions:
 - On an actual OS, useful work may be done with the CPU time after a failed queue operation
 - Only one queue can own/hold a specific item (they're designed for TCBs)
 - The cost of atomic instructions is not high from extreme contention
 - The less time spent in queue operations, the better

We make the following tradeoffs:
 - Guarantees on latency for guarantees on success
