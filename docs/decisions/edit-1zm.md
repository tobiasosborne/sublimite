# edit-1zm: Wayland review disposition

The input ring now drops the newest event and increments a retained counter,
matching X11. Overflow does not mark the connection as failed. Queue capacity
is unchanged: (E) inherited X11_QUEUE_CAP, not a new throughput budget.

Wayland socket names have no protocol-defined private numbering convention.
Tests and benches clear inherited compositor names and inherited descriptors
unless EDIT_ALLOW_REAL_DISPLAY is present. EDIT_WAYLAND_DISPLAY explicitly
selects a private compositor and is idempotent. The application uses the same
policy when Wayland is selected. Existing X11 selection and guard policy stay
in place. An explicit private name is authorization; comparing it against the
currently selected name would incorrectly reject a repeated guard invocation.

Startup errors retain return codes and acquire readable Wayland diagnostics.
Main records the diagnostic at the failing Wayland setup call, rather than
mapping unrelated renderer/editor errors through overlapping numeric domains.
The additional socket-path error is (P) the next distinct platform setup code
following the existing error domain. Long runtime directories use a Linux
/proc/self/fd directory alias in sockaddr_un.sun_path. This avoids both pathname
truncation and a process-wide chdir. The ABI field size derives from
sizeof(sockaddr_un.sun_path); paths that still cannot fit return the distinct
error. Connection setup and directory storage occur before typing; temporary
path storage uses src/base's arena.

Startup output geometry comes from current wl_output modes. Choose the
highest integer-scale output as a conservative initial buffer/font reserve.
(E) Desktop DPI uses the existing Xft/editor convention of 96 logical pixels
per inch, multiplied by the protocol scale. (P) Orthogonal output transforms
swap mode axes. EGL buffer size and native pointer coordinates use physical
pixels; xdg-shell configure dimensions are logical pixels and multiply by
scale. Buffer sizes round down to scale multiples within the requested reserve,
with one logical pixel as the protocol minimum. This is a startup snapshot,
not surface-enter tracking or a runtime font/DPI migration implementation.
An inherited WAYLAND_SOCKET is already a protocol connection; main skips its
preliminary query so that platform initialization can consume it exactly once.
That launch form retains legacy font defaults, although its platform buffer and
pointer scale still use output metadata. Correct font startup there needs
sharing the persistent connection across font initialization and window setup.

The native-allocation red probe exposed libwayland-client allocation while
reading and dispatching a real mock key packet. The enforced G1 allocation
bracket starts at decoded platform input, includes the decoder, queue, editor
mutation, layout and backend submit, and ends at submit. Native transport
before decoded ingress and frame/proxy creation in present after submit are
reported separately with a power stamp; neither is silently exempted. This
matches the existing editor on_io boundary and the shared render contract's
submit/present distinction. The test uses a null renderer, calibrates the
counting allocator with an explicit malloc and makes no claim about actual
EGL-driver allocations or physical-key latency. Sanitizer allocator counting
is explicitly inert, as in the existing base allocator hook.

The missing-wp_presentation finding exceeds a local guard fix. The current
CPU backend is an XShm/Present backend that explicitly rejects Wayland, and
the frozen render contract requires separate device and matching display
completion. Supporting this case needs wl_shm buffer ownership/release,
presentation without that optional protocol, and an explicit contract decision
about unavailable display timing. Treating a frame scheduling callback as a
display timestamp would mislabel measurements. Keep the failing opt-in
acceptance probe, document the missing renderer in src/wayland/STATUS.md and
leave the bead incomplete under the user's larger-than-one-bead instruction.
