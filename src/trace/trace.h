/* trace.h - per-thread lock-free event ring for the keystroke pipeline (P0.3).
 *
 * Single writer per ring: each thread calls trace_thread_register() once and
 * then trace_record() from that thread only. No locks, no allocation after
 * trace_init(). trace_dump() and trace_reset() must be called while no thread
 * is recording (after the workers are quiescent). */
#ifndef EDITOR_TRACE_TRACE_H
#define EDITOR_TRACE_TRACE_H

#include <stdint.h>
#include <stdio.h>

#define TRACE_RING_CAP 65536u
#define TRACE_MAX_THREADS 16u

enum trace_ev {
    TRACE_T0_INGRESS = 0,
    TRACE_T1_DEQUEUE,
    TRACE_T2_MUTATION_DONE,
    TRACE_T3_RENDER_DONE,
    TRACE_T4_PRESENT_SUBMITTED,
    TRACE_T5_DEVICE_DONE,
    TRACE_T6_PRESENT_COMPLETE,
    TRACE_EV_COUNT
};

typedef struct trace_rec {
    uint64_t ns;
    uint32_t frame_id;
    uint16_t ev;
    uint16_t thread;
} trace_rec;

/* Resets all rings and the registration counter. Call once, before threads start. */
void trace_init(void);

/* Claims a ring for the calling thread. Idempotent per thread.
 * Returns the ring index (0..15), or -1 if all rings are taken. */
int trace_thread_register(void);

/* Records an event stamped with CLOCK_MONOTONIC. Silently drops the event if the
 * calling thread is not registered. */
void trace_record(enum trace_ev ev, uint32_t frame_id);

/* Records an event with an explicit timestamp (for replay and tests). */
void trace_record_at(uint64_t ns, enum trace_ev ev, uint32_t frame_id);

/* CLOCK_MONOTONIC in nanoseconds. */
uint64_t trace_now_ns(void);

/* Writes all rings in the binary dump format (see trace_fmt.h). Returns 0 on success. */
int trace_dump(FILE *f);

/* Clears all recorded events; ring registrations are kept. */
void trace_reset(void);

#endif
