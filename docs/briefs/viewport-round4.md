Viewport ROUND 4 (bead v2-1sg; advisor rulings, 2026-11-25). Sol, on its viewport thread, after the render bead
(v2-jj1) is in and before layout. Limited to five items, each red first from Astra's input in
docs/reviews/wave2-viewport-astra-r3.md, plus the header gaps that review lists on interior-byte column evidence.
Then Astra re-checks only these items and reruns the mutants. The errata are E9 and E10, and the item-4 rule, at the
end of docs/decisions/wave2-viewport-layout-spec.md.

1. E9, the stopping rule. A cluster's class is its first scalar's class. Decide at a cluster's START, and never step
   into a cluster that is rejected. Word motion costs O(accepted bytes + one scalar). Red test: Astra's input (a clip
   row holding `a.`, 4,000 x U+0301, `z`; WORD_RIGHT and SELECT_WORD from 0), with the decoder-byte counter ASSERTED
   (it was 8,002).
2. Test: LEFT from EOF in S ASCII letters followed by U+0301, in clip and in 80-cell wrap. It must kill the mutant that
   changes the backward row-start proof on synthetic rows to BOF (that mutant returned 32,767 instead of 32,768).
   Assert both the result and the decoder-byte counter.
3. Test: LEFT from EOF in 4,000 x U+00E9 at wrap width 1. It must kill the mutant that replaces the active row start
   with its display-line start (that mutant decoded 8,000 bytes for a two-byte move). Assert the counter.
4. The rank evidence rule, in the header. The motion cursor holds offsets, never a snapshot; its evidence may come
   from ANY snapshot of the intent's text serial. After VP_WAIT the driver calls the step again only when it holds a
   snapshot of that same text serial with newer line knowledge, and answers the pending request from it. Nothing in
   the cursor refers to the earlier snapshot, and no window borrow outlives the step that consumed it. Say in the
   header that this is Q10's late-rank rule applied to the port. Test it.
5. E10, endpoint normalisation; it is NOT an ASSERT.
   - The precondition is any byte offset in [0, length], and the walker normalises.
   - If the byte at an endpoint is a continuation byte, look back at most 3 bytes with the strict decoder. If a valid
     scalar spans the endpoint, the forward walks start from its end and the backward walks from its start.
   - An endpoint on a scalar boundary inside a cluster needs nothing.
   - The header says this, in place of "treated as a cluster boundary".
   Red tests:
   - Astra's U+1F600 case (caret = mark = 1);
   - the paste case: stray bytes 82 AC in a file, E2 pasted in front of them, and the caret after the paste.
Also close the header gaps on interior-byte column evidence that the review lists.
Rules as for every round: no git, no bd; red first; decision file and report BEFORE the final lanes; nice make -j2;
nothing outside the checkout. The three standing Wave 2 brief lines apply.
