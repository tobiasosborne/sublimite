# vendor/

- `stb_truetype.h`: stb_truetype v1.26, public domain (Sean Barrett / RAD Game Tools).
  Fetched from https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
  sha256 `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab`.
- `DejaVuSansMono.ttf`: DejaVu Sans Mono from fonts-dejavu-core; licence text in
  `DejaVu-LICENSE` (copied from /usr/share/doc/fonts-dejavu-core/copyright).
  Local patch `EDIT PATCH P2.3e` (3 lines in `stbtt__run_charstring`): a step budget
  (`STBTT_CFF_MAX_STEPS`, default 100000 interpreter iterations per charstring run) so a
  hostile CFF font cannot nest subrs into ~20^9 steps. The sha256 above is of the upstream
  file; the patched file differs by exactly those lines. Everything else about CFF safety
  (INDEX/DICT/FDSelect bounds) is validated in `src/font/font.c` before `stbtt_InitFont`.
