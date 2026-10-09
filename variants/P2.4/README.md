P2.4 GL renderer par variants (edit-e6x.4), stored here (force-added; variants/ is gitignored) so the code survives until the pick.
Build/bench from the branches wt/edit-e6x.4-egl and wt/edit-e6x.4-glx (each has src/gl, bench/gl_bench.c, tests/gl_test.c in place).
Pick: real-display bench on Tobias's GPU (present rows SKIP under Xvfb); the winner is cherry-picked into src/gl, then this directory is deleted.
Decision records: docs/decisions/P2.4-egl.md, docs/decisions/P2.4-glx.md.
