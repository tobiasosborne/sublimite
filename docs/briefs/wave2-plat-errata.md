Errata pass (advisor, 2026-11-25): Wave 2 plat spec (branch t3/wave2-plat-spec, c81c4ce). SIGNED IN SUBSTANCE; the
advisor reads only the diff. Astra, document only, after her viewport re-review. With this pass all four Wave 2 designs
are signed.

ACCEPTED as written:
- Q1: the atomic key, one sequencer, producer backpressure at the listener boundary, nine prepaid terminal cells;
- Q2's ownership moves, and the single absolute deadline;
- Q3: only adjacent, unused surface snapshots coalesce, and a scale-only change gets a serial;
- Q6: one off-UI owner per native connection, and UI submit touches prepaid memory only;
- the transfer axes;
- the surface and scale arithmetic;
- plat_native's lease;
- the witness table.
The dependency question: plat delivers the owned URI-list bytes, and the app parses them at intake with the landed dnd
parser. dnd does not move, and no L3 backend imports a parser.
The numbers (256 events, 64 MiB and 128 MiB, 256 KiB, 5 s, 1 s, and the 3 s GL watchdog) are accepted as admission
constants carried from the pin, each already cited to its donor constant. Add one sentence where 256 is derived: under
producer backpressure it is a tuning constant, not a correctness bound, because no admitted transition is ever dropped
at any value.

PL-1. READ AND CLAIM RESULTS ARE COMPLETIONS, NOT ORDERED EVENTS.
      The text sends a read result through the ordered FIFO, then adds an exception so the waiting app head can take it
      early: plat_take_reply, a reply fd bound to a head id, an `already_taken` receipt left in sequence, and a race
      between the two paths ("if ordinary poll won ... the direct lookup is stale"). But the position of a paste in the
      edit order is fixed by the Ctrl+V key that issued the read, not by when the bytes arrive; the same holds for a
      claim result.
      The simplest alternative, to adopt or refute with a counter-example in which a read or claim result's place among
      later input changes an outcome:
      - READ and CLAIM results leave the ordered FIFO entirely, and arrive only on the completion channel: one reply fd,
        and one call returning the next completed transfer by id, routed by the app into the destination it reserved
        when it issued the request;
      - DROP and OWNER_LOST stay ordered events, because they are things that happen to the user.
      That deletes `already_taken`, the head-id binding of the fd, the dual path, and two of the nine terminal cells'
      special handling.
PL-2. THE ALLOCATION GUARD MUST NOT SHAPE PRODUCTION THREADING.
      The text requires native-library work on other threads to complete or yield "before the guarded step starts", by
      a phase handoff on every key, because the landed guard counts process-wide. But Law 2 is about the typing path,
      and in v2 that path (ingest of prepaid records, mutation, layout, submit) runs on the UI thread.
      The ruling, as plan amendment A31 (proposed in docs/decisions/wave0-plan-amendments.md, for the advisor's
      signature):
      - the guard attributes allocations by thread;
      - the enforced count is the UI thread's, between ingest and submit;
      - other threads' counts in the same window are recorded and reported, not enforced;
      - a worker job that a key's frame waits for before submit is not permitted by design, and if one is ever
        introduced, it joins the enforced set.
      Base bead: v2-ze1 (P2).
      DELETE the phase-handoff requirement from the plat text. As an erratum to the signed font text
      (docs/decisions/wave2-font-spec.md), delete its matching "no thread exemption" sentence. W0.9's finding 8 was an
      allocation on the UI thread, and it is still caught.
PL-3. Add to the implementation evidence list: the cost of the producer-thread hop on a key (native ingress to the UI's
      wake), measured separately inside G1, both ordinary and after idle.
After the errata diff: sign-off. plat joins Sol's order after font, and the backends follow plat.
