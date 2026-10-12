/* Verbatim query bytes encoded as C strings; regenerate with tools/hl_embed.py. */
static const char highlights_latex[] =
"; Local query for the coordinator-vendored LaTeX grammar.\n"
"; Node names derive from the generated grammar; compilation is tested in hl_test.\n"
"[(line_comment) (block_comment)] @comment\n"
"(command_name) @function\n"
"[(begin) (end)] @keyword\n"
"(math_delimiter) @punctuation.delimiter\n"
"[(inline_formula) (displayed_equation)] @embedded\n"
"(section (curly_group) @text.title)\n"
"(verbatim_environment) @text.literal\n"
"(label) @text.reference\n"
"(curly_group_uri) @text.uri\n"
;
