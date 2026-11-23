/* The flat model as the implementation of src/piece/piece.h: the one TU that
 * defines PIECE_MODEL_IMPL (tests/piece_model.h; docs/decisions/v2-0fv.7.md
 * §7). A suite that links build/<cfg>/tests/piece_model.o ahead of
 * libtext.a never selects the archive's piece.o. */
#define PIECE_MODEL_IMPL
#include "piece_model.h"
