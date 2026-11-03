# Generated protocol glue

Generated with the locally installed `wayland-scanner` from:

- `/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml`
- `/usr/share/wayland-protocols/stable/presentation-time/presentation-time.xml`
- `/usr/share/wayland-protocols/stable/viewporter/viewporter.xml` (edit-b6ai.7, wayland-scanner 1.26.0)
- `/usr/share/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml` (edit-b6ai.7, wayland-scanner 1.26.0; staging protocol, MIT licence like the others)

Commands, per protocol `P` with output stem `S` (`viewporter`, `fractional-scale-v1`):
`wayland-scanner client-header P S-client.h`, `wayland-scanner server-header P S-server.h`,
`wayland-scanner private-code P S.inc`.

Client/server headers use `client-header` / `server-header`; the `.inc` bodies use `private-code`. `src/wayland/protocol.c` compiles those bodies only when the Makefile enables Wayland. Checked-in glue avoids requiring the scanner or protocol package on consumers that already have client headers/libraries.

All generated opcode, enum, signature, version and array literals derive from that upstream XML's wire ABI. Copyright and permission notices are retained in each generated file. Server headers are used only by the optional mock test/fuzzer.
