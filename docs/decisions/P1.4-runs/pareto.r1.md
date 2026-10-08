| row | col | bptree | rope | flat | hybrid |
|---|---|---|---|---|---|
| build | text_size | 21102 / 21102 B | 17533 / 17533 B | 14094 / 14094 B | 18103 / 18103 B |
| code_1m | open | 0.146 / 0.731 ms | 0.185 / 0.997 ms | 0.108 / 0.743 ms | 0.195 / 0.815 ms |
| code_1m | first_op | 0.146 / 0.735 ms | 0.185 / 1.002 ms | 0.108 / 0.747 ms | 0.195 / 0.816 ms |
| code_1m | mem_open | 1.05642e+06 / 1.05642e+06 B | 1.06527e+06 / 1.06527e+06 B | 1.05151e+06 / 1.05151e+06 B | 1.05225e+06 / 1.05225e+06 B |
| code_1m | insert | 0.276 / 1.581 us | 0.655 / 5.091 us | 0.764 / 6.917 us | 0.887 / 4.24 us |
| code_1m | delete | 0.504 / 1.801 us | 0.907 / 6.829 us | 0.807 / 3.689 us | 0.495 / 2.818 us |
| code_1m | peak_mem | 1.27453e+06 / 1.27453e+06 B | 1.32754e+06 / 1.32754e+06 B | 1.21698e+06 / 1.21698e+06 B | 1.35047e+06 / 1.35047e+06 B |
| code_1m | pieces | 2981 / 2981 count | 2966 / 2966 count | 2966 / 2966 count | 2720 / 2720 count |
| code_1m | read_viewport | 4.528 / 7.714 us | 10.371 / 14.433 us | 7.758 / 10.565 us | 8.391 / 11.535 us |
| code_1m | byte_to_line | 0.118 / 0.185 us | 0.225 / 0.398 us | 0.279 / 0.456 us | 0.246 / 0.465 us |
| code_1m | line_to_byte | 0.174 / 0.47 us | 0.282 / 0.693 us | 0.313 / 0.797 us | 0.334 / 0.943 us |
| log_1g | open | 0.059 / 0.262 ms | 0.001 / 0.029 ms | 0.002 / 0.009 ms | 0.712 / 1.017 ms |
| log_1g | first_op | 0.06 / 0.265 ms | 0.002 / 0.042 ms | 0.003 / 0.022 ms | 0.714 / 1.02 ms |
| log_1g | mem_open | 561376 / 561376 B | 82168 / 82168 B | 19704 / 19704 B | 403664 / 403664 B |
| log_1g | insert | 0.297 / 0.601 us | 0.294 / 11.299 us | 0.746 / 13.896 us | 3.667 / 6.426 us |
| log_1g | delete | 4.378 / 11.748 us | 4.233 / 11.641 us | 4.743 / 12.909 us | 0.178 / 1.516 us |
| log_1g | peak_mem | 1.07709e+06 / 1.07709e+06 B | 344440 / 344440 B | 168408 / 168408 B | 1.01395e+06 / 1.01395e+06 B |
| log_1g | pieces | 19408 / 19408 count | 3025 / 3025 count | 4048 / 4048 count | 19407 / 19407 count |
| log_1g | read_viewport | 3.133 / 8.266 us | 2.756 / 3.329 us | 3.078 / 6.073 us | 2.702 / 3.234 us |
| log_1g | byte_to_line | 1.51 / 3.041 us | 4.67 / 8.604 us | 15.363 / 62.95 us | 2.287 / 4.557 us |
| log_1g | line_to_byte | 2.647 / 5.319 us | 4.371 / 7.88 us | 14.104 / 53.588 us | 2.245 / 4.122 us |
| oneline_1g | open | 0.059 / 0.27 ms | 0.002 / 0.031 ms | 0.002 / 0.012 ms | 0.727 / 0.909 ms |
| oneline_1g | first_op | 0.059 / 0.273 ms | 0.002 / 0.043 ms | 0.003 / 0.024 ms | 0.729 / 0.911 ms |
| oneline_1g | mem_open | 561376 / 561376 B | 82168 / 82168 B | 19704 / 19704 B | 403664 / 403664 B |
| oneline_1g | insert | 0.297 / 0.608 us | 0.303 / 10.88 us | 0.732 / 11.479 us | 3.492 / 6.309 us |
| oneline_1g | delete | 4.142 / 9.258 us | 4.123 / 11.956 us | 4.532 / 11.801 us | 0.177 / 1.678 us |
| oneline_1g | peak_mem | 1.07709e+06 / 1.07709e+06 B | 344440 / 344440 B | 168408 / 168408 B | 1.01395e+06 / 1.01395e+06 B |
| oneline_1g | pieces | 19408 / 19408 count | 3025 / 3025 count | 4048 / 4048 count | 19407 / 19407 count |
| oneline_1g | read_viewport | 3.914 / 13.366 us | 2.508 / 4.291 us | 4.079 / 6.29 us | 2.946 / 4.285 us |
| oneline_1g | byte_to_line | 1.529 / 3.225 us | 4.609 / 8.446 us | 15.703 / 68.318 us | 2.32 / 4.71 us |
| oneline_1g | line_to_byte | 0.123 / 0.421 us | 0.099 / 0.28 us | 0.104 / 0.255 us | 0.156 / 0.431 us |
| oneline_10g | open | SKIP | SKIP | SKIP | SKIP |
| dense_short | open | 26.999 / 29.218 ms | 27.121 / 28.976 ms | 22.928 / 25.009 ms | 28.484 / 30.361 ms |
| dense_short | first_op | 27 / 29.233 ms | 27.122 / 28.98 ms | 22.929 / 25.014 ms | 28.485 / 30.364 ms |
| dense_short | mem_open | 6.72043e+07 / 6.72043e+07 B | 6.71296e+07 / 6.71296e+07 B | 6.71118e+07 / 6.71118e+07 B | 6.71341e+07 / 6.71341e+07 B |
| dense_short | insert | 1.011 / 2.701 us | 2.072 / 7.614 us | 1.653 / 7.715 us | 1.388 / 4.003 us |
| dense_short | delete | 1.25 / 3.092 us | 2.251 / 8.299 us | 1.711 / 7.669 us | 1.175 / 3.159 us |
| dense_short | peak_mem | 6.74003e+07 / 6.74003e+07 B | 6.73919e+07 / 6.73919e+07 B | 6.72773e+07 / 6.72773e+07 B | 6.74652e+07 / 6.74652e+07 B |
| dense_short | pieces | 4048 / 4048 count | 3025 / 3025 count | 3088 / 3088 count | 4044 / 4044 count |
| dense_short | read_viewport | 1.703 / 1.836 us | 1.711 / 1.981 us | 1.731 / 2 us | 1.742 / 2.142 us |
| dense_short | byte_to_line | 0.68 / 2.319 us | 1.749 / 7.305 us | 1.695 / 9.607 us | 1.304 / 4.847 us |
| dense_short | line_to_byte | 2.501 / 5.269 us | 3.173 / 8.169 us | 3.015 / 8.929 us | 2.707 / 5.811 us |
| random_edits_1e5 | open | 0.049 / 0.242 ms | 0.002 / 0.03 ms | 0.002 / 0.008 ms | 0.798 / 0.98 ms |
| random_edits_1e5 | first_op | 0.05 / 0.245 ms | 0.002 / 0.042 ms | 0.003 / 0.023 ms | 0.8 / 0.983 ms |
| random_edits_1e5 | mem_open | 561376 / 561376 B | 82168 / 82168 B | 19704 / 19704 B | 403664 / 403664 B |
| random_edits_1e5 | batch_total | 51.284 / 102.442 ms | 317.626 / 372.617 ms | 2257.33 / 2327.66 ms | 51.303 / 106.273 ms |
| random_edits_1e5 | insert | 0.23 / 0.554 us | 2.743 / 7.535 us | 17.248 / 80.855 us **MISS** | 0.561 / 1.572 us |
| random_edits_1e5 | delete | 0.581 / 1.455 us | 2.963 / 8.489 us | 17.394 / 82.545 us **MISS** | 0.262 / 0.705 us |
| random_edits_1e5 | peak_mem | 1.064e+07 / 1.064e+07 B | 1.29933e+07 / 1.29933e+07 B | 9.27638e+06 / 9.27638e+06 B | 1.6567e+07 / 1.6567e+07 B |
| random_edits_1e5 | pieces | 166197 / 166197 count | 149839 / 149839 count | 150862 / 150862 count | 165427 / 165427 count |
| random_edits_1e5 | read_viewport | 5.569 / 11.743 us | 3.57 / 5.206 us | 3.149 / 4.05 us | 3.42 / 4.584 us |
| random_edits_1e5 | byte_to_line | 1.035 / 2.93 us | 1.311 / 4.131 us | 0.913 / 3.463 us | 1.452 / 3.592 us |
| random_edits_1e5 | line_to_byte | 1.451 / 3.974 us | 1.481 / 3.8 us | 1.097 / 3.235 us | 1.465 / 3.169 us |
| typing_1e4 | open | 0.833 / 0.923 ms | 0.995 / 1.026 ms | 0.464 / 0.666 ms | 0.43 / 0.531 ms |
| typing_1e4 | first_op | 0.837 / 0.928 ms | 1.001 / 1.034 ms | 0.468 / 0.672 ms | 0.43 / 0.532 ms |
| typing_1e4 | mem_open | 1.05642e+06 / 1.05642e+06 B | 1.06527e+06 / 1.06527e+06 B | 1.05151e+06 / 1.05151e+06 B | 1.05225e+06 / 1.05225e+06 B |
| typing_1e4 | batch_total | 1.327 / 1.451 ms | 1.503 / 2.637 ms | 0.62 / 0.78 ms | 0.632 / 0.859 ms |
| typing_1e4 | insert | 0.042 / 0.122 us | 0.087 / 0.164 us | 0.023 / 0.029 us | 0.031 / 0.051 us |
| typing_1e4 | delete | 0.229 / 0.299 us | 0.221 / 0.342 us | 0.032 / 0.045 us | 0.038 / 0.069 us |
| typing_1e4 | peak_mem | 1.20906e+06 / 1.20906e+06 B | 1.1637e+06 / 1.1637e+06 B | 1.12962e+06 / 1.12962e+06 B | 1.1283e+06 / 1.1283e+06 B |
| typing_1e4 | pieces | 476 / 476 count | 462 / 462 count | 462 / 462 count | 21 / 21 count |
| typing_1e4 | read_viewport | 2.485 / 8.101 us | 3.126 / 16.689 us | 2.953 / 6.594 us | 2.464 / 3 us |
| typing_1e4 | byte_to_line | 0.722 / 1.32 us | 1.669 / 5.213 us | 10.188 / 20.181 us | 1.295 / 2.639 us |
| typing_1e4 | line_to_byte | 1.07 / 1.963 us | 1.441 / 2.743 us | 6.241 / 11.853 us | 1.064 / 1.939 us |
| undo_1e4 | open | 0.05 / 0.233 ms | 0.002 / 0.03 ms | 0.002 / 0.008 ms | 0.803 / 1.163 ms |
| undo_1e4 | first_op | 0.05 / 0.237 ms | 0.002 / 0.041 ms | 0.003 / 0.022 ms | 0.806 / 1.166 ms |
| undo_1e4 | mem_open | 561376 / 561376 B | 82168 / 82168 B | 19704 / 19704 B | 403664 / 403664 B |
| undo_1e4 | batch_total | 6.816 / 32.479 ms | 20.176 / 44.728 ms | 55.636 / 79.22 ms | 5.374 / 30.423 ms |
| undo_1e4 | delete | 0.403 / 1.196 us | 0.424 / 7.515 us | 2.017 / 9.107 us | 0.241 / 1.329 us |
| undo_1e4 | insert_ref | 0.176 / 0.256 us | 0.196 / 0.267 us | 1.097 / 4.601 us | 0.174 / 0.255 us |
| undo_1e4 | peak_mem | 1.99395e+06 / 1.99395e+06 B | 1.70431e+06 / 1.70431e+06 B | 1.0639e+06 / 1.0639e+06 B | 1.794e+06 / 1.794e+06 B |
| undo_1e4 | pieces | 36383 / 36383 count | 20005 / 20005 count | 21028 / 21028 count | 36382 / 36382 count |
| undo_1e4 | read_viewport | 5.046 / 8.389 us | 2.823 / 3.758 us | 3.08 / 3.895 us | 2.768 / 3.35 us |
| undo_1e4 | byte_to_line | 1.638 / 3.698 us | 4.261 / 8.936 us | 5.205 / 31.467 us | 2 / 4.44 us |
| undo_1e4 | line_to_byte | 2.465 / 5.823 us | 4.081 / 8.399 us | 4.382 / 23.225 us | 1.959 / 4.179 us |
| undo_1e5 | open | 0.085 / 0.394 ms | 0.001 / 0.029 ms | 0.002 / 0.009 ms | 0.751 / 1.078 ms |
| undo_1e5 | first_op | 0.086 / 0.399 ms | 0.002 / 0.04 ms | 0.003 / 0.024 ms | 0.753 / 1.08 ms |
| undo_1e5 | mem_open | 561376 / 561376 B | 82168 / 82168 B | 19704 / 19704 B | 403664 / 403664 B |
| undo_1e5 | batch_total | 87.041 / 143.731 ms | 357.111 / 412.224 ms | 4792.04 / 4838.77 ms | 109.381 / 160.236 ms |
| undo_1e5 | delete | 0.428 / 0.814 us | 2.485 / 7.886 us | 12.407 / 68.872 us **MISS** | 0.446 / 1.868 us |
| undo_1e5 | insert_ref | 0.19 / 0.294 us | 0.251 / 0.344 us | 9.708 / 75.182 us | 0.272 / 0.727 us |
| undo_1e5 | peak_mem | 1.28661e+07 / 1.28661e+07 B | 1.62209e+07 / 1.62209e+07 B | 8.55548e+06 / 8.55548e+06 B | 1.02873e+07 / 1.02873e+07 B |
| undo_1e5 | pieces | 216360 / 216360 count | 200028 / 200028 count | 201047 / 201047 count | 216209 / 216209 count |
| undo_1e5 | read_viewport | 3.634 / 6.202 us | 3.82 / 5.601 us | 3.216 / 4.568 us | 3.471 / 4.756 us |
| undo_1e5 | byte_to_line | 0.916 / 2.068 us | 1.367 / 4.091 us | 0.906 / 3.426 us | 1.529 / 3.786 us |
| undo_1e5 | line_to_byte | 1.317 / 3.536 us | 1.54 / 4.018 us | 1.096 / 3.272 us | 1.543 / 3.322 us |
| paste_1m | paste_1m | 0.076 / 0.952 ms | 0.317 / 0.798 ms | 0.309 / 0.389 ms | 0.074 / 0.396 ms |
| paste_1m | peak_mem | 2.19222e+06 / 2.19222e+06 B | 2.13036e+06 / 2.13036e+06 B | 2.10214e+06 / 2.10214e+06 B | 2.10095e+06 / 2.10095e+06 B |
| paste_1m | pieces | 33 / 33 count | 18 / 18 count | 18 / 18 count | 33 / 33 count |
| snapshots_100 | insert | 0.306 / 1.461 us | 0.38 / 1.708 us | 1.434 / 7.402 us | 0.88 / 5.219 us |
| snapshots_100 | delete | 0.553 / 19.263 us | 0.566 / 5.213 us | 1.304 / 7.268 us | 0.627 / 4.997 us |
| snapshots_100 | snapshot_take | 0.034 / 0.11 us | 0.032 / 0.053 us | 36.147 / 119.147 us | 1.702 / 4.212 us |
| snapshots_100 | read_viewport | 5.501 / 9.966 us | 6.091 / 16.61 us | 4.096 / 8.38 us | 7.602 / 17.479 us |
| snapshots_100 | peak_mem | 8.08387e+06 / 8.08387e+06 B | 6.11167e+06 / 6.11167e+06 B | 1.31064e+07 / 1.31064e+07 B | 1.6587e+07 / 1.6587e+07 B |
| snapshots_100 | pathcopy_ref | 1.92e+07 / 1.92e+07 B | 1.92e+07 / 1.92e+07 B | 1.92e+07 / 1.92e+07 B | 1.92e+07 / 1.92e+07 B |
| snapshots_100 | pieces | 13882 / 13882 count | 13868 / 13868 count | 13868 / 13868 count | 9439 / 9439 count |
| snapshots_100 | snapshot_release_all | 0.577 / 0.577 ms | 1.328 / 1.328 ms | 0.308 / 0.308 ms | 2.586 / 2.586 ms |
| line_jump_1e7 | open | 0.054 / 0.265 ms | 0.017 / 0.029 ms | 0.003 / 0.008 ms | 0.632 / 1.164 ms |
| line_jump_1e7 | line_to_byte_first | 32.54 / 60.285 ms **MISS** | 64.393 / 73.145 ms **MISS** | 23.678 / 28.307 ms | 85.254 / 136.448 ms **MISS** |
| line_jump_1e7 | line_to_byte | 0 / 0 ms | 0 / 0 ms | 0 / 0 ms | 0 / 0 ms |
| line_jump_1e7 | byte_to_line | 0.002 / 0.003 ms | 0.023 / 0.047 ms | 0.037 / 0.084 ms | 0.002 / 0.005 ms |

Non-dominated: bptree, rope, flat, hybrid
Dominated: none

bptree: wins 28 [code_1m/open, code_1m/first_op, code_1m/insert, code_1m/delete, code_1m/read_viewport, code_1m/byte_to_line, code_1m/line_to_byte, log_1g/insert, log_1g/byte_to_line, oneline_1g/insert, oneline_1g/byte_to_line, dense_short/insert, dense_short/delete, dense_short/read_viewport, dense_short/byte_to_line, dense_short/line_to_byte, random_edits_1e5/batch_total, random_edits_1e5/insert, random_edits_1e5/byte_to_line, typing_1e4/byte_to_line, undo_1e4/delete, undo_1e4/byte_to_line, undo_1e5/batch_total, undo_1e5/delete, undo_1e5/insert_ref, undo_1e5/byte_to_line, snapshots_100/insert, line_jump_1e7/byte_to_line]; MISS/TIMEOUT cells: 1
rope: wins 8 [log_1g/pieces, oneline_1g/pieces, dense_short/pieces, random_edits_1e5/pieces, undo_1e4/pieces, undo_1e5/pieces, snapshots_100/snapshot_take, snapshots_100/peak_mem]; MISS/TIMEOUT cells: 1
flat: wins 40 [build/text_size, code_1m/mem_open, code_1m/peak_mem, log_1g/open, log_1g/first_op, log_1g/mem_open, log_1g/peak_mem, oneline_1g/open, oneline_1g/first_op, oneline_1g/mem_open, oneline_1g/peak_mem, oneline_1g/line_to_byte, dense_short/open, dense_short/first_op, dense_short/mem_open, dense_short/peak_mem, random_edits_1e5/open, random_edits_1e5/first_op, random_edits_1e5/mem_open, random_edits_1e5/peak_mem, random_edits_1e5/read_viewport, typing_1e4/mem_open, typing_1e4/batch_total, typing_1e4/insert, typing_1e4/delete, undo_1e4/open, undo_1e4/first_op, undo_1e4/mem_open, undo_1e4/peak_mem, undo_1e5/open, undo_1e5/first_op, undo_1e5/mem_open, undo_1e5/peak_mem, undo_1e5/read_viewport, undo_1e5/line_to_byte, paste_1m/paste_1m, snapshots_100/read_viewport, snapshots_100/snapshot_release_all, line_jump_1e7/open, line_jump_1e7/line_to_byte_first]; MISS/TIMEOUT cells: 3
hybrid: wins 21 [code_1m/pieces, log_1g/delete, log_1g/read_viewport, log_1g/line_to_byte, oneline_1g/delete, oneline_1g/read_viewport, random_edits_1e5/delete, random_edits_1e5/line_to_byte, typing_1e4/open, typing_1e4/first_op, typing_1e4/peak_mem, typing_1e4/pieces, typing_1e4/read_viewport, typing_1e4/line_to_byte, undo_1e4/batch_total, undo_1e4/insert_ref, undo_1e4/read_viewport, undo_1e4/line_to_byte, paste_1m/peak_mem, snapshots_100/delete, snapshots_100/pieces]; MISS/TIMEOUT cells: 1
