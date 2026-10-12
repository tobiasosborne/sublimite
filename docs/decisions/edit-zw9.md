# edit-zw9: wrapped wheel progress across blank lines

Keep the existing wrapped wheel displacement and continuation. The fault is
in `scan_row`, the bounded fallback used by adjacent visual-row queries:
an empty logical line advances past LF without scanning a text cluster.
Using an unchanged text scan counter as an end-of-progress condition returns
that blank row prematurely. Forward wheel continuation then stops short;
reverse queries can return an earlier blank row while searching for the
requested predecessor.

Certify progress with the row's `next` boundary advancing beyond `start`,
and retain the explicit end-of-file sentinel. Charge every newline row's LF
to the existing scan counter (P: LF occupies a byte) so blank-line traversal
also observes `LAYOUT_BYTE_BUDGET`. No new allocation, state, capacity, timing
bound, input mapping or scroll controller is needed. The fix stays in layout's
wrapped query; input, find and scroll controller code are unchanged.

The null-backend regression uses wrapped paragraphs separated by empty lines.
Its independent oracle is a taller viewport with identical text width and
document geometry: normal layout publishes all travelled row boundaries,
without using the adjacent-query code under test. Test displacement comes
from the supplied Astra reproduction (M)[AC]: near travel is three notches,
far travel is thirty notches; `SCROLL_CORE_ROWS` supplies three visual rows
per notch. (P) Near travel is therefore three times three = nine rows; total
forward travel is (three plus thirty) times three = ninety-nine rows.
Geometry reuses `PROBE_COLS` and `PROBE_ROWS` from the existing editor scroll
fixture (E: test controls, not a performance budget). Paragraph byte width is
twice `PROBE_COLS`; paragraph count and oracle height are total forward rows
plus `PROBE_ROWS`. The paragraph separator is its LF and an empty line's LF
(P). Array indices, boolean values, zero initializers and successful return
codes follow their C/API contracts (P).

Check separately delivered downward and upward notches, batched far movement
and reversal, and batched near reversal. Compare the actual layout top and
submitted top against the oracle, preserve selection, and compare restored
text cells with both the initial view and the intermediate near view.
Disable focus in this fixture so blinking cannot alter the cell comparison.
Null cell equality is headless evidence; native pixel equality still belongs
to the coordinator's display verification.
