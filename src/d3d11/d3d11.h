#ifndef EDIT_D3D11_H
#define EDIT_D3D11_H
#include "render/render.h"

int render_d3d11_backend(render_backend *b);
/* After factory/query, attach zeroed caller-owned state to b->state, then bind
 * the HWND here before render_backend_init with that SAME state. No plat layout
 * dependency. Borrowed HWND and state must survive physical worker completion,
 * failed-init cleanup, and successful quiescent shutdown; IsWindow cannot pin
 * a handle. The HWND owner keeps pumping messages while init/cleanup runs on
 * the worker, including while waiting to join: DXGI may synchronously message
 * it. On close during init, defer DestroyWindow, discard adoption, wait for
 * physical completion, shut down any successful backend, then destroy it.
 * Remove the borrowed latency HANDLE from UI wait sets before shutdown.
 *
 * WM_SIZE during an active frame: keep the existing swapchain dimensions,
 * coalesce the latest client size, and resize only after T5/T6 release the slot.
 * Minimize/session/suspend follow set_suspended and the recovery protocol below.
 * A bootstrap may use GDI/CPU on this HWND. An existing flip-model bootstrap
 * swapchain must be fully destroyed/flushed BEFORE this backend's initial INIT;
 * there is no chain sharing/transfer or deferred finish_init acquisition here.
 * Background init creates its chain without presenting a diagnostic frame. */
int render_d3d11_backend_for_window(render_backend *b, void *hwnd);
/* Init-only diagnostic options. Default: hardware, falling back to WARP.
 * force_warp tests the software device; readback reserves a staging snapshot
 * and copies the actual back buffer immediately before each Present. */
int d3d11_diagnostics(render_backend *b, bool force_warp, bool readback);
/* UI: nonblocking; routes a private poll through adapter event(). Integrate
 * the borrowed latency HANDLE into the Win32 wait set; while active also poll
 * on a finite event-loop timeout. Readiness alone is not a T6: matching
 * GetFrameStatistics/GetFrameStatisticsMedia must acknowledge the present. */
void *d3d11_latency_handle(const render_backend *b);
/* If the event loop consumed the latency signal in its wait set, acknowledge
 * that readiness here before retrying present (auto-reset HANDLE). */
int d3d11_notify_latency_ready(render_backend *b);
int d3d11_poll_completions(render_backend *b);
/* UI: set for session lock/disconnect, suspend, display/compositor unavailable;
 * clear on resume. Hidden/minimized HWNDs are detected automatically. Before
 * Present, BUSY retains the copied frame without drawing invisible work.
 * A committed frame with unavailable exact statistics requests recovery:
 * poll/present returns UNSUPPORTED and recovery_required is true, without T6.
 * Adapter keeps its active slot. Stop input, remove borrowed wait handles,
 * quiescent shutdown, re-init on worker while pumping, then full repaint with
 * a fresh grid/backend lifetime. DEVICE instead denotes native device failure.
 * Poll/retry on a finite UI timeout while active; idle needs no polling. */
int d3d11_set_suspended(render_backend *b, bool suspended);
bool d3d11_recovery_required(const render_backend *b);
/* Diagnostic-only blocking read of the last pre-Present staging snapshot.
 * Output is opaque 0xffRRGGBB, top down; stride/capacity in uint32_t pixels. */
int d3d11_read_pixels(render_backend *b, uint32_t *out, size_t capacity, size_t stride);
typedef struct d3d11_present_info {
    uint32_t expected, observed;
    int32_t status;
    uint64_t sync_qpc;
    bool statistics_matched;
} d3d11_present_info;
int d3d11_present_stats(const render_backend *b, d3d11_present_info *out);
/* Borrowed ID3D11Device for diagnostics; NULL before successful init. */
void *d3d11_device(const render_backend *b);
bool d3d11_is_warp(const render_backend *b);
bool d3d11_debug_enabled(const render_backend *b);
#endif
