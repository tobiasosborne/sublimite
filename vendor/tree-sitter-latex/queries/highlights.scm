; Local query for the coordinator-vendored LaTeX grammar.
; Node names derive from the generated grammar; compilation is tested in hl_test.
[(line_comment) (block_comment)] @comment
(command_name) @function
[(begin) (end)] @keyword
(math_delimiter) @punctuation.delimiter
[(inline_formula) (displayed_equation)] @embedded
(section (curly_group) @text.title)
(verbatim_environment) @text.literal
(label) @text.reference
(curly_group_uri) @text.uri
