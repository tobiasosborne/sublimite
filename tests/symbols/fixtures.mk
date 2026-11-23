# Use the normal release object rule and flags from Makefile.
SYMBOL_FIXTURE_OBJS := $(patsubst %.c,$(B)/rel/%.o,$(wildcard tests/symbols/*/src/*/*.c))

.PHONY: check-symbol-fixtures
check-symbol-fixtures: $(SYMBOL_FIXTURE_OBJS)
	python3 tests/symbols/run.py --build $(B) --ar '$(AR)' --nm '$(or $(NM),nm)' \
	    --layer-libs $(symbol_layer_libs) --module-map $(symbol_modules)
