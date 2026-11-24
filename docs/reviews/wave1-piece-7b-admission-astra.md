# Piece admission at `17da429`

**HOLD.** (P) Two MUST-FIX findings. A deletion can keep a rolled-back ADD chunk mapped, and a capture can make separate base requests for a chunk and its directory. The first exact-plan exit, the post-drain replan, and the compound slot calculation are confirmed.

(G) denotes the contract or a stated input or allocator fixture. (P) denotes a static derivation. Step numbers, commit IDs, finding IDs and source lines are references. Code locations below are in `src/piece/piece.c` at the reviewed commit. The contract is `src/piece/piece.h:59–80`, `docs/decisions/v2-0fv.17.md` §8.1 and `docs/decisions/wave0-plan-amendments.md` A26 on `v2`.

### Step 1 — MUST-FIX. A pure deletion can retain ADD storage it never consumes

(P) The local gates themselves match A26. Edits check both the scalar claim and `piece_extra_local` (2457–2468). A snapshot cache miss checks the header slot and any boundary chunk and directory spares (1194–1214, 2379–2389). Checkpoint begin uses that take (1235–1245). Capture needs no slots and checks tail room (2536–2546). The defect is in how an edit selects and prepares its companion.

(G) Open an empty tree on a fresh reserve, without replenish or other threads. Use a page geometry with at least six slots per extent. After open, cap additional mapped storage at exactly the cost of one ADD chunk. Let `x` be a byte. Begin a checkpoint, call `piece_replace(t, 0, 0, &x, 1)`, then `piece_replace(t, 0, 1, &x, 0)`, abort, and repeat the first insert. The second call supplies a valid pointer for an empty insertion, which the public entry point accepts (2504–2511).

(P) Open uses one empty leaf (1095–1118). Begin adds one header. The first insert keeps the checkpoint root alive and copies its leaf, so at most three slots are occupied: original leaf + header + live leaf (1204–1225, 2017–2028, 2089–2108). Its first ADD chunk has no directory parent and consumes the allowed mapping (739–762, 765–789). The deletion's leaf-height scalar claim is `4 × 0 + 3 = 3` slots (1289–1292). At least `6 − 3 = 3` are free, so it takes the fast path.

(P) The deletion chooses `PIECE_EXTRA_ADD` solely because `input.bytes` is non-null (2454). Adding zero bytes passes the tail check by equality (2384–2385). Preparation takes an ADD reference and executes zero append iterations because its first and end chunk counts are equal (765–780, 2465–2468). The deletion-only arm then returns through publication without dropping that reference (2474–2476, 2493–2501). The chunk now has two references: the live view's and the abandoned preparation's.

(P) Abort drops the live view's reference and restores the empty ADD root and length (1265–1275). The chunk goes from two owners to one, so it never enters the retirement queue (441–447). The remaining reference has no owning pointer. The repeated insert needs one new chunk and no additional extent. Draining can reclaim the old header and leaf, but cannot reach the abandoned chunk (474–510). Base consequently refuses and the call returns `PIECE_ERR_NOMEM` (2419–2434). With the preparation reference balanced, abort queues that chunk, the drain returns its mapping, and the same cap funds the insert. Passing `NULL` for the empty insertion also avoids the retain (2467). This is a quiescent false refusal caused by the preceding admission.

### Step 2 — CONFIRMED. A covered first plan skips retirement

(P) `piece_admit` computes the first plan at 2419. Coverage checks the exact slot count against core holes plus whole reserve extents and checks actual companion availability (577–580, 2379–2391, 2407–2411). A covered plan jumps directly to companion preparation at 2433. For a local ADD tail the append loop is empty (765–780). For a local boundary the spare chunk and one spare per directory level cover the copy (649–663, 697–725, 812–832). Neither path enters the drain at 2422. This closes the zero-remainder case from K.

### Step 3 — CONFIRMED. A shortfall drains and replans once

(P) Only a failed first coverage check reaches the full core drain at 2422. It processes queued nodes, ADD objects and released headers, including the core inbox (474–510). The reserve exchange then transfers offered whole extents to the counted free list (2423, 273–281). The call recomputes the plan against the resulting ownership at 2424. Coverage at 2426 skips trim and extent funding when the remainder is zero. There are two plan calls on this path, at 2419 and 2424, with no back edge. A refusal returns directly to the caller. A retry enters with a fresh plan and fresh coverage checks (2438–2442, 2459–2471). A26 permits the concurrent-release window in I.

### Step 4 — MUST-FIX. A single capture issues two base requests

(G) Start with a tree whose original text is exactly `PIECE_CHUNK` bytes. Capture the entire original once, leaving a full ADD chunk. With no other activity, call `piece_capture(t, 0, 1, &out)`. Set the remaining mapped-byte allowance to the cost of one ADD chunk. Use a fresh reserve with no whole free extents to trim.

(P) The setup capture appends at chunk index zero and installs the chunk itself as the level-zero root (697–703, 739–762, 765–789). The next capture needs zero slots (2544). Appending one byte to a full chunk raises the rounded chunk count from one to two (221–224, 2384–2385). Both coverage checks fail on ADD. The drain changes nothing, the extent loop executes zero times, and 2433 calls `piece_add_prepare`.

(P) Request one is `base_arena_init(sizeof(piece_chunk))` through 741 → 661 → 249. It succeeds. The new index is one and the old root level is zero, so `1 >> (0 × PIECE_DIR_BITS) = 1` selects root growth (744). The level-zero child construction only retains the chunk (747, 700–703). Request two is `base_arena_init(sizeof(piece_dir))` for the new parent through 749 → 661 → 249. Base refuses it. Cleanup releases the tentative chunk (756–762, 773–777) and capture returns `PIECE_ERR_NOMEM` (2433, 2544–2545). Thus one public call makes two independently fallible requests to base. The same two calls occur if both allocations succeed. This violates A26's one-request limit even though the objects are both necessary and the failure precedes publication.

(P) The surplus-extent calculation itself closes J. For exact need `S`, core holes `H` and slots per extent `E`, 2427–2428 keep at most `ceil(max(S − H, 0) / E)` whole extents. The trim returns each excess extent through `base_pool_free` (295–303) before any new allocation. The request-count violation occurs after that trim.

### Compound edit plan — CONFIRMED. The claim covers the staged peak

(P) The live compound edit retains the old root before deletion and drops it only after insertion (2477–2480, 2493–2497). The shadow therefore starts deletion with inherited sharing, and both deletions disable joining (2341, 2479). They use the same splice, merge and root-promotion code (1768–1816, 1833–1864, 1914–1948, 1965–1992). Shadow allocation raises the running slot balance and records its peak. Dropping a shared original costs no slot, and dropping a shadow node lowers the balance (1707–1739, 1749–1763).

(P) The insertion frontier is taken at the edit offset on the root returned by that deletion (2341–2344). Surviving originals are still reachable from the retained old root. Shadow copies are private. The frontier counts copies on that basis, and the simulation uses the same cut, extension, batch and split geometry as application (2017–2082, 2115–2160, 2273–2291). If deletion peaks at `D` slots and finishes with balance `B`, insertion begins with those `B` slots still live and adds its simulated `I`. The joint need is therefore `max(D, B + I)`, exactly the value stored at 2345–2347. The planned compound path never funds insertion separately.
