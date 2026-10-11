#ifndef EDIT_HL_FIXTURE_H
#define EDIT_HL_FIXTURE_H
/* Hand-written test-only LR grammar:
 * document -> empty | list; list -> unit | unit list.
 * unit is one Unicode scalar, including newlines / UTF-8 replacement.
 * Four symbols and six states derive from these four productions. Keeping
 * the nullable document out of list avoids nullable leaves inside reused
 * subtrees. The list is visible so cursor differential checks visit each
 * node once, without repeatedly flattening a nested hidden rule. This fixture is never linked into libedit or the editor. */
#include "../vendor/tree-sitter/lib/src/parser.h"
static bool hl_fixture_lex(TSLexer *lexer, TSStateId state)
{
    (void)state;
    if (lexer->eof(lexer)) { lexer->result_symbol = 0; return true; }
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    lexer->result_symbol = 1;
    return true;
}
static const TSLanguage *hl_fixture_language(void)
{
    static const uint16_t table[6][4] = {
        {1, 1, 0, 0}, {3, 5, 2, 4}, {7, 0, 0, 0},
        {9, 5, 0, 5}, {11, 0, 0, 0}, {15, 0, 0, 0}
    };
    static const TSParseActionEntry actions[] = {
        [0] = {.entry = {0, false}},
        [1] = {.entry = {1, false}}, [2] = RECOVER(),
        [3] = {.entry = {1, true}}, [4] = REDUCE(2, 0, 0, 0),
        [5] = {.entry = {1, true}}, [6] = SHIFT(3),
        [7] = {.entry = {1, true}}, [8] = ACCEPT_INPUT(),
        [9] = {.entry = {1, true}}, [10] = REDUCE(3, 1, 0, 0),
        [11] = {.entry = {1, true}}, [12] = REDUCE(2, 1, 0, 0),
        [13] = {.entry = {1, true}}, [14] = SHIFT(5),
        [15] = {.entry = {1, true}}, [16] = REDUCE(3, 2, 0, 0)
    };
    static const char *const names[] = {"end", "unit", "document", "list"};
    static const TSSymbolMetadata metadata[] = {
        {false, false, false}, {true, true, false},
        {true, true, false}, {true, true, false}
    };
    static const TSSymbol public_map[] = {0, 1, 2, 3};
    static const TSLexerMode modes[6] = {{0, 0, 0}};
    static const TSStateId primary[] = {0, 1, 2, 3, 4, 5};
    static const TSSymbol aliases[] = {0};
    static const TSLanguage language = {
        .abi_version = TREE_SITTER_LANGUAGE_VERSION,
        .symbol_count = 4, .token_count = 2,
        .state_count = 6, .large_state_count = 6,
        .production_id_count = 1,
        .parse_table = &table[0][0], .parse_actions = actions,
        .symbol_names = names, .symbol_metadata = metadata,
        .public_symbol_map = public_map, .alias_sequences = aliases,
        .lex_modes = modes, .lex_fn = hl_fixture_lex,
        .primary_state_ids = primary, .name = "hl_fixture"
    };
    return &language;
}
#endif
