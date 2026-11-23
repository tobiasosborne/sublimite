# The acyclic module DAG (docs/PLAN_V2.md §2). Make expands L<n>_MODS
# for both the archive source lists and tools/check_dag.py.
#
# A layer links only lower layers. Archives per configuration live at
# build/<cfg>/<archive>; objects at build/<cfg>/src/<module>/<file>.o.
# Included by the Makefile after WARN/INC/B/CFGS and mk/grammars.mk.

L0_MODS := os base trace work ipc
L1_MODS := scan utf8 indent piece undo find
L2_MODS := file journal savectl edit
L3_MODS := plat x11 wayland render raster gl font
L4_MODS := viewport layout tabs minimap findui fif hl cmd config dnd
L5_MODS := app

LAYERS      := L0 L1 L2 L3 L4 L5
LAYERS_DESC := L5 L4 L3 L2 L1 L0

L0_LIB := libcore.a
L1_LIB := libtext.a
L2_LIB := libpersist.a
L3_LIB := libgfx_plat.a
L4_LIB := libui.a
L5_LIB := libapp.a

# Layers strictly below each layer, nearest first (link order).
LOWER_L0 :=
LOWER_L1 := L0
LOWER_L2 := L1 L0
LOWER_L3 := L2 L1 L0
LOWER_L4 := L3 L2 L1 L0
LOWER_L5 := L4 L3 L2 L1 L0

# Sources per layer. src/main.c is outside every archive (it is linked as an
# object ahead of L5). Vendored tree-sitter objects ride in libui.a next to hl.
LAYER_MODULES := $(foreach l,$(LAYERS),$(addprefix $(l):,$($(l)_MODS)))
layer_modules = $(patsubst $(1):%,%,$(filter $(1):%,$(LAYER_MODULES)))
$(foreach l,$(LAYERS),$(eval $(l)_SRCS := $$(sort $$(foreach m,$$(call layer_modules,$(l)),$$(wildcard src/$$(m)/*.c)))))
L4_SRCS += $(TS_SRCS)
MAIN_SRC := $(wildcard src/main.c)

.PHONY: print-layer-map
print-layer-map:
	@printf '%s\n' $(LAYER_MODULES)

# Per-layer object lists in every configuration tree: L<n>_OBJ_<cfg>.
$(foreach c,$(CFGS),$(foreach l,$(LAYERS),$(eval $(l)_OBJ_$(c) := $$(patsubst %.c,$$(B)/$(c)/%.o,$$($(l)_SRCS)))))

# Archive paths per configuration, topological link order (L5 first, L0 last).
libs_desc = $(foreach l,$(LAYERS_DESC),$(B)/$(1)/$($(l)_LIB))
lib_of    = $(B)/$(1)/$($(2)_LIB)
libs_of   = $(foreach l,$(2),$(B)/$(1)/$($(l)_LIB))

# One archive rule per (configuration, layer). The archive is rebuilt from
# scratch whenever its member list changes, not only when a member is newer:
# a removed module source must leave the archive (else its stale object links
# into every binary). <archive>.members holds the list and is replaced only
# when the list differs (tests/lanes/run.py archive-members fixture).
define LAYER_ARCHIVE
$(B)/$(1)/$($(2)_LIB).members: FORCE
	@mkdir -p $$(@D); printf '%s\n' $($(2)_OBJ_$(1)) > $$@.next; \
	cmp -s $$@.next $$@ || mv -f $$@.next $$@
$(B)/$(1)/$($(2)_LIB): $($(2)_OBJ_$(1)) $(B)/$(1)/$($(2)_LIB).members
	@mkdir -p $$(@D)
	rm -f $$@; $$(AR) rcs $$@ $$(filter %.o,$$^)
endef
.PHONY: FORCE
FORCE:
$(foreach c,$(CFGS),$(foreach l,$(LAYERS),$(eval $(call LAYER_ARCHIVE,$(c),$(l)))))

# check-layers: for layers 0-4 an isolated executable pulls EVERY object of the
# layer (--whole-archive) and resolves it against ONLY lower archives, so an
# upward reference cannot hide behind an unselected object: the link fails.
# Layer 5 is checked the same way with src/main.o as the entry point.
LAYER_TEST_IDS  := 0 1 2 3 4
LAYER_TEST_BINS := $(patsubst tests/%.c,$(B)/rel/tests/%,$(LAYER_SUITES))
LAYER_L5_BIN    := $(B)/rel/tests/layer_L5_link
LAYER_TESTS_SRC := $(foreach n,$(LAYER_TEST_IDS),tests/layer_L$(n)_test.c)

define LAYER_LINK_CHECK
$(2): $(3) $(call lib_of,rel,L$(1)) $(call libs_of,rel,$(LOWER_L$(1)))
	@mkdir -p $$(@D)
	$$(CC_RELEASE) $$(REL_F) -pthread $(3) -Wl,--whole-archive $(call lib_of,rel,L$(1)) -Wl,--no-whole-archive $(call libs_of,rel,$(LOWER_L$(1))) $$(LDLIBS) -o $$@
endef
$(foreach n,$(LAYER_TEST_IDS),$(eval $(call LAYER_LINK_CHECK,$(n),$(B)/rel/tests/layer_L$(n)_test,$(B)/rel/tests/layer_L$(n)_test.o)))
$(eval $(call LAYER_LINK_CHECK,5,$(LAYER_L5_BIN),$(B)/rel/src/main.o))
