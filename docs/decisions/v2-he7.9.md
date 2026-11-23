# W0.6b · Persistent X11 contract driver

The runner owns and authenticates the Xvfb. Start the helper with `build/xdriver --display :NUMBER` and pass the runner's `XAUTHORITY` in its environment. The helper requires a positive, explicit local display. It links only XCB, xkbcommon and the dynamic loader. The checked FakeInput request comes from the installed `libxcb-xtest.so.0`, so the XTest development package is unnecessary. Missing XKB, XTest or authentication fails startup.

The driver subscribes to root substructure events before its first QueryTree. It waits for a mapped InputOutput root child with a matching CARDINAL `_NET_WM_PID` and rejects an ambiguous PID. A successful bind clears verified focus. Each input command verifies active focus inside a short server grab covering verification and injection. Keys use the server's current XKB map and state. The driver presses explicit chord modifiers in argument order, resolves the final keysym in the resulting state and releases keys in reverse order. It can use mapped momentary level selectors for shifted symbols. Motion uses coordinates inside the bound window. A button press requires the pointer to be over that window.

The marker is a direct format-32 `_SUBLIMITE_CONTRACT` ClientMessage containing exactly the five caller-supplied words from the X11 envelope (P, ClientMessage data32). It uses the same X connection as XTest. The helper reports that X accepted the send. The caller receives inspection completion through the separate inspector channel.

Both selections support byte-preserving `UTF8_STRING`. A selection owned by the driver also advertises `TARGETS` and `TIMESTAMP`. The helper polls X events alongside stdin and serves selection requests during window, timestamp and selection waits. A read names the expected owner, checks it before sending ConvertSelection and again before accepting the result, and uses a fresh requestor window and server timestamp. A stale owner, malformed reply or missing INCR terminator fails the command. INCR writes wait for each property deletion before sending the next chunk and keep a byte snapshot after ownership changes. Chunk size comes from the server request limit less the ChangeProperty header and the extra BIG-REQUESTS length word (P, X11 request encoding).

## Line protocol

Startup emits `READY MAX_REQUEST_BYTES`. Send one ASCII command per newline and read its reply before sending the next. Success is `OK` with the result shown below. A command failure is `ERR CODE`. XIDs, timeouts and marker words accept decimal or `0x`-prefixed hexadecimal. A numeric keysym uses the `0x` prefix, so a bare digit is a keysym name. Replies use decimal XIDs and lowercase hex bytes.

| Command | Success |
|---|---|
| `find PID TIMEOUT_MS` | `OK XID` |
| `focus` | `OK` |
| `key KEYSYM` | `OK` |
| `chord MODIFIER [MODIFIER ...] KEYSYM` | `OK`, using XKB names such as `Control_L` |
| `motion X Y` | `OK` |
| `button NUMBER click\|down\|up` | `OK` |
| `owner CLIPBOARD\|PRIMARY` | `OK XID`, zero when unowned |
| `own SELECTION HEX\|- TIMEOUT_MS` | `OK XID` for the driver owner window |
| `read SELECTION OWNER_XID TIMEOUT_MS` | `OK HEX` or `OK -` for empty bytes |
| `marker WORD1 WORD2 WORD3 WORD4 WORD5` | `OK` |
| `quit` | `OK`, then process exit |

`XDriver` provides Python methods for each command and raises `XDriverError` for a reported refusal. Malformed replies, early EOF and pipe deadlines raise `XDriverProtocolError` and close the stream. Its process watchdog must leave room for server-side command deadlines.

## Verification

The conformance fixture chooses an explicit free display in the bead's reserved `:120`–`:129` range using the socket, lock and abstract bind checks. It starts Xvfb with `-noreset -nolisten tcp`, a mode-0600 authority file beneath `build/` (P, owner-only read/write), and a readiness pipe. It checks the returned display and reaps the exact subprocess it started. Independent XCB clients record raw input and verify every clipboard byte. The INCR payload is the reported request limit plus one byte, the smallest payload that exceeds the limit (P, X11 ChangeProperty size). The fixture generates its input deterministically and needs no random seed for event or byte assertions.

The private fixture uses a 15 s operation watchdog and a 30 s wrapper watchdog. Full local runs took 2.1–3.4 s, including during a repository build, leaving more than four times the full observed runtime at each operation. The deliberate 500 ms incomplete-transfer deadline expires a request whose fixture separately confirms it sent the advertised bytes without a terminator. The checks wait on X events and pipe readiness. These are local fixture hang guards pending calibration for the full oracle.

First run, before the driver existed, on authenticated `:120`. All seven tests reached setup and failed with `ModuleNotFoundError: No module named 'xdriver'`.

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum tests/contract/xdriver_test_client.c -lxcb -o build/xdriver-test-client
XDRIVER_TEST_DISPLAY=:120 python3 -B tests/contract/test_xdriver.py
```

Behavioral negative controls ran the unchanged assertions against temporary driver variants under `build/`. Bypassing focus, emitting a marker before keys, accepting the replacement owner and accepting INCR without a terminator each failed its intended assertion.

```sh
XDRIVER_TEST_DISPLAY=:120 python3 -B build/xdriver-negative.py > build/xdriver-negative.log 2>&1
```

The green run uses the release helper on authenticated `:120`. The eight tests cover focus refusal and loss, exact input order, stale owner, incomplete INCR, byte transfers in both directions, and malformed, missing and extra replies.

```sh
XDRIVER_TEST_DISPLAY=:120 make check-xdriver
make all
make check
```

The server reported a 16,777,212-byte maximum on `:120` and the INCR test transferred 16,777,213 bytes in each direction. SHA-256 of the tested release helper is `1402b68433e43c8762672f36d38667653315216d5f079ce1cc75ed788f584f34`. The concatenated Makefile, driver C, wrapper Python, fixture C and test Python sources in that order hash to `860ea3eabd674aa621c4fc3c40f17ae6e2db83594bd2aa6706668bd3514dbdf4`.
