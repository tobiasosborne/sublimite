# Vendored tree-sitter runtime + the 13 grammars: ONE table, no duplication.
#
# Active only when the runtime is vendored (Wave 0 imports vendor/ from main);
# until then every variable below is empty and this file is a clean no-op, so
# smoke builds stay instant. Included by the Makefile after WARN, INC, B and
# CFGS are defined and before mk/layers.mk (which adds TS_SRCS to L4 libui.a,
# next to hl).
#
# Upstream's generated parsers/scanners are not -Wconversion/-Wshadow/
# -Wswitch-enum clean: every vendored object drops those three in every
# configuration and keeps -std=c11 -Wall -Wextra -Werror plus the project
# optimisation/sanitizer flags. A grammar adds only the extra relaxations its
# own parser/scanner trips (carried over from v1), and its own parser ABI
# header directory first on the include path.

TS_RUNTIME     := vendor/tree-sitter/lib/src/lib.c
TS_RELAXED     := -Wconversion -Wshadow -Wswitch-enum
HL_OPTIN       :=
TS_SRCS        :=
TS_GRAMMAR_ALL :=

ifneq ($(wildcard $(TS_RUNTIME)),)
TS_ENABLED := yes

# $(call TS_GRAMMAR,name,source dir,sources,extra -Wno-* flags,opt-in)
# opt-in = yes: measured size keeps the grammar out of any default set;
# `make build/sublimite-v2-<name>` forces its hl_language_<name> wrapper in
# (HL_OPTIN, docs/decisions/edit-v0o.3.md in v1).
define TS_GRAMMAR
TS_GRAMMAR_ALL += $(1)
TS_SRCS += $(addprefix $(2)/,$(3))
$(if $(5),HL_OPTIN += $(1))
$(foreach c,$(CFGS),$(addprefix $(B)/$(c)/$(2)/,$(3:.c=.o))): WARN := $$(filter-out $$(TS_RELAXED),$$(WARN)) $(4)
$(foreach c,$(CFGS),$(addprefix $(B)/$(c)/$(2)/,$(3:.c=.o))): INC := -I$(2) $$(INC)
endef

# Runtime: relaxations only, v1 include path (vendor/tree-sitter/lib/include).
TS_SRCS += $(TS_RUNTIME)
$(foreach c,$(CFGS),$(B)/$(c)/$(TS_RUNTIME:.c=.o)): WARN := $(filter-out $(TS_RELAXED),$(WARN))

# One row per grammar: name,source dir,sources,extra relaxations,opt-in
$(eval $(call TS_GRAMMAR,c,vendor/tree-sitter-c/src,parser.c,,))
$(eval $(call TS_GRAMMAR,rust,vendor/tree-sitter-rust/src,parser.c scanner.c,,))
$(eval $(call TS_GRAMMAR,python,vendor/tree-sitter-python/src,parser.c scanner.c,-Wno-unused-value,))
$(eval $(call TS_GRAMMAR,julia,vendor/tree-sitter-julia/src,parser.c scanner.c,-Wno-unused-parameter,yes))
$(eval $(call TS_GRAMMAR,javascript,vendor/tree-sitter-javascript/src,parser.c scanner.c,-Wno-unused-parameter,))
$(eval $(call TS_GRAMMAR,typescript,vendor/tree-sitter-typescript/typescript/src,parser.c scanner.c,-Wno-unused-parameter,yes))
$(eval $(call TS_GRAMMAR,json,vendor/tree-sitter-json/src,parser.c,,))
$(eval $(call TS_GRAMMAR,toml,vendor/tree-sitter-toml/src,parser.c scanner.c,-Wno-unused-parameter,))
$(eval $(call TS_GRAMMAR,yaml,vendor/tree-sitter-yaml/src,parser.c scanner.c,-Wno-unused-value -Wno-unused-but-set-variable -Wno-implicit-fallthrough,))
$(eval $(call TS_GRAMMAR,markdown,vendor/tree-sitter-markdown/tree-sitter-markdown/src,parser.c scanner.c,,yes))
$(eval $(call TS_GRAMMAR,markdown_inline,vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src,parser.c scanner.c,,yes))
$(eval $(call TS_GRAMMAR,latex,vendor/tree-sitter-latex/src,parser.c scanner.c,-Wno-unused-parameter -Wno-unused-label -Wno-unused-but-set-variable,yes))
$(eval $(call TS_GRAMMAR,bash,vendor/tree-sitter-bash/src,parser.c scanner.c,-Wno-unused-value,yes))

ifneq ($(words $(TS_GRAMMAR_ALL)),13)
$(error mk/grammars.mk: expected 13 grammars, table has $(words $(TS_GRAMMAR_ALL)): $(TS_GRAMMAR_ALL))
endif
else
TS_ENABLED :=
endif
