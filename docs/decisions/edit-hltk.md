# edit-hltk: desktop measurement and private Xvfb startup

The journal retirement measurement leaves battery status as `unknown` when
opening BAT0 fails. An absent status file is identified explicitly in the log;
the existing AC stamp remains the desktop convention required by the brief.
When BAT0 is present, reading, newline removal and the discharge stamp follow
the original path.

On WSLg, the pathname socket directory is read-only. Manual Xvfb probes showed
that automatic `-displayfd` selection rejects the incomplete listener set, while
an explicit display plus `-displayfd` starts successfully using its abstract
socket. This is an inference from the contrasting startup results, rather than
an inspection of X server source. The worker report contains the commands.

Keep automatic selection unchanged when the socket directory is writable.
Otherwise, bind-probe abstract socket names and choose a free positive display
from the `uint16_t` range (P: the selected integer type bounds the search; zero
is excluded because it names the real desktop). Probing never connects to an
existing server. Pass the explicit display to Xvfb, keeping `-displayfd` for its
readiness notification. Xvfb's existing lock and bind arbitrate any race after
the probe closes; a collision fails startup without using the other server.
Existing process cleanup, screen configuration and TCP refusal are retained.

Route every `refproto_display_open` error through the existing display close
routine after emitting its diagnostic. That routine releases the connection
and any loaded library, and clears their handles. A fixture-local dynamic-load
seam reproduces the missing XTest runtime even on hosts where it is installed;
the existing protocol expectations are retained.
