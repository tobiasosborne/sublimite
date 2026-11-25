# v2-sve · Resume a full-mailbox continuation on drain

A job that retains an unpublished result requests the existing `work_continue` and returns. The invocation records whether its latest publish was refused for fullness. Connecting that result to the continuation is the smallest caller contract because the lease already carries the argument, generation, cancellation and physical retirement, and the pool already knows which mailbox refused the publish.

## Invariant

A continuation waiting for ring space remains in its class FIFO under the original lease. It can run on its producing worker once that worker's mailbox has room. The scheduler skips waiting entries while looking for runnable jobs. Cancellation and shutdown remove waiting entries using the same queue ownership rules as for runnable entries.

The producer arms its mailbox with an acquire-release exchange and rechecks head before it can sleep. Ordinary drain and selective receive publish the reclaimed head before exchanging the arm. When reclamation wins the exchange order, the producer's recheck sees the released head. When arming wins, the nonblocking worker wake remains readable until consumed, including when the UI reclaims space before the worker enters its wait. A selective hole behind a live head returns no ring capacity. A producer with an owned parked entry and no runnable work waits indefinitely on its OS wait set. Ordinary idle workers use their existing class condition variable. Queue submissions and shutdown can wake workers on either wait path. Drain takes no queue lock and allocates no memory.

Physical retirement clears the slot's busy bit and publishes its finished epoch before signaling the UI pool wake. The worker does this on final return, including after cancellation. UI queued cancellation retires synchronously and signals too. Pending results continue to hold their own reservations through their callbacks. The UI registers the pool wake, drains it through the mailbox protocol, then checks finished handles, readiness and pending mail before sleeping. [Signed app spec, erratum §8](wave1-app-spec.md#8-runnable-service-and-finish-before-wake)

## State and ownership

| Axis | States and owner | Why it has its own state |
| --- | --- | --- |
| Lease space, `work_space_state` | The slot starts at `NONE`. Outside the queue mutex, the dequeuing worker alone may set `REFUSED_FULL` during its invocation. When that worker reacquires `pool.mu`, a continuing refusal becomes `PARKED`. Dequeue, submit and retirement set `NONE` under the mutex. | Refusal and parking are successive phases of one lease. The queue can contain `NONE` or `PARKED`, and only a dequeued invocation can record `REFUSED_FULL`. |
| Worker wait, `work_wait_mode` | The worker sets `CLASS` or `SPACE` under `pool.mu`. Class notifications read it under the same mutex to decide whether to signal the worker wake descriptor as well as the condition variable. | A worker can execute another job while a lease remains parked. A queued cancellation can also retire its last parked lease while the worker is still in the descriptor wait. |
| Mailbox wake arm, `work_mailbox_arm` | The producer exchanges `ARMED` into one atomic `uint32_t` before rechecking head. The UI exchanges `DISARMED` after releasing head, without taking `pool.mu`. | The producer can arm during its queue scan and then run another job. The UI can disarm while the worker is still in the descriptor wait. The arm records whether reclamation owes a wake independent of the worker's current wait mode. |

The lease uses one field across the dequeue and return handoff. Cancel and shutdown may invalidate a running epoch under `pool.mu`, and they touch the lease's space state only when they remove its queue entry. The three enums are handled by exhaustive switches with `-Wswitch-enum` and no default. The state encoding preserves the reachable scheduler and wake behavior, so the five regression tests exercise the same cases.

## Simplest alternative and counter-example

Use the existing class condition variable for parked continuations as well, and have drain signal it once after releasing head. Consider a worker that checks the full mailbox under the queue mutex. The UI can reclaim the last needed entry and signal after that check, while the worker is still about to enter the condition wait. No later drain need occur and the signal is lost. Acquiring the queue mutex in drain would rule out the race by adding a blocking acquisition to the UI. The existing OS wake retains readiness across that gap. [OS wait and wake contract](v2-0fv.13.md)

Each configured worker now acquires a wake descriptor, an epoll descriptor and the wait set's timer descriptor at pool initialization. Its thread uses the set without arming the timer. Partial initialization unwinds the descriptors and any threads already started. [Work implementation](../../src/work/work.c)
