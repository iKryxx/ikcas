CC = gcc
AR = ar

# Three build flavors: release (build/rel), debug (build/dbg), asan (build/asan).
WARN = -Wall -Wextra -Wpedantic
INC = -Iinc -Iplugins
CFLAGS = $(WARN) -O2 $(INC)
DBG_CFLAGS = $(WARN) -O0 -g3 $(INC)
ASAN_CFLAGS = $(WARN) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(INC)
DEPFLAGS = -MMD -MP

# The executable exports its symbols so plugin shared objects can call the
# registry and the numeric core.
EXE_LDFLAGS = -rdynamic
LDLIBS = -ldl

BUILD = build
GEN = $(BUILD)/gen/builtin_plugins.c

# plugins.conf lists the plugins compiled into the binary. It is created
# with the defaults on first use and never overwritten afterwards.
$(if $(wildcard plugins.conf),,$(shell sh scripts/init_plugins_conf.sh > plugins.conf))
PLUGINS := $(shell sh scripts/plugin_list.sh plugins.conf)
$(foreach p,$(PLUGINS),$(if $(wildcard plugins/$(p)/*.c),,$(error plugins.conf lists '$(p)' but plugins/$(p)/ has no .c files)))

# Source lists. The library is everything except the app, which is the only
# part that has a main().
LIB_SRCS = $(shell find src -name '*.c' -not -path 'src/app/*')
APP_SRCS = $(wildcard src/app/*.c)
CORE_SRCS = $(wildcard src/core/*.c)
PLUGIN_SRCS = $(foreach p,$(PLUGINS),$(wildcard plugins/$(p)/*.c))
SHIPPED_PLUGIN_SRCS = $(wildcard plugins/arith/*.c plugins/extra_ops/*.c plugins/constants/*.c)
TEST_CORE_SRCS = tests/test_core.c
TEST_FRONT_SRCS = $(filter-out $(TEST_CORE_SRCS),$(wildcard tests/test_*.c))

EXTRA_OPS_SO = $(BUILD)/plugins/extra_ops.so

# objs: object files in a flavor's build dir for the given sources.
# whole: link the whole library, so shared-object plugins can reach any host symbol.
objs = $(patsubst %.c,$(BUILD)/$(1)/%.o,$(2))
whole = -Wl,--whole-archive $(BUILD)/$(1)/libikcas.a -Wl,--no-whole-archive

.PHONY: all test cli-test bench debug asan compdb clean
.SECONDEXPANSION:

all: ikcas

# Rules for one flavor: objects, libikcas.a, the executable and both test
# binaries. $(1) build dir, $(2) compiler flags, $(3) binary suffix.
define mode_rules
$(BUILD)/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$(CC) $(2) $(DEPFLAGS) -c $$< -o $$@

$(BUILD)/$(1)/libikcas.a: $(call objs,$(1),$(LIB_SRCS))
	@rm -f $$@
	$(AR) rcs $$@ $$^

ikcas$(3): $(call objs,$(1),$(APP_SRCS) $(PLUGIN_SRCS) $(GEN)) $(BUILD)/$(1)/libikcas.a
	$(CC) $(2) $(EXE_LDFLAGS) -o $$@ $$(filter %.o,$$^) $(call whole,$(1)) $(LDLIBS)

ikcas_test$(3): $(call objs,$(1),$(CORE_SRCS) $(TEST_CORE_SRCS))
	$(CC) $(2) -o $$@ $$^

ikcas_test_frontend$(3): $(call objs,$(1),$(TEST_FRONT_SRCS) $(SHIPPED_PLUGIN_SRCS)) $(BUILD)/$(1)/libikcas.a $(EXTRA_OPS_SO)
	$(CC) $(2) $(EXE_LDFLAGS) -o $$@ $$(filter %.o,$$^) $(call whole,$(1)) $(LDLIBS)
endef

$(eval $(call mode_rules,rel,$(CFLAGS),))
$(eval $(call mode_rules,dbg,$(DBG_CFLAGS),_debug))
$(eval $(call mode_rules,asan,$(ASAN_CFLAGS),_asan))

# builtin_plugins.c is generated from plugins.conf, see scripts/gen_builtin_plugins.sh.
$(GEN): plugins.conf scripts/gen_builtin_plugins.sh scripts/plugin_list.sh
	@mkdir -p $(dir $@)
	sh scripts/gen_builtin_plugins.sh plugins.conf > $@

plugins.conf:
	sh scripts/init_plugins_conf.sh > $@

# Any plugin directory can also be built as a shared object for -p.
$(BUILD)/plugins/%.so: $$(wildcard plugins/$$*/*.c)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -shared -DIKCAS_PLUGIN_SHARED -MMD -MP -MF $@.d -o $@ $(filter %.c,$^)

plugin-%: $(BUILD)/plugins/%.so
	@:

# Everything: core tests, frontend tests, CLI golden tests.
test: ikcas ikcas_test ikcas_test_frontend cli-test
	./ikcas_test
	./ikcas_test_frontend

# The golden files assume plugins.conf still lists arith (the default);
# extra_ops is loaded with -p where a test needs it.
cli-test: ikcas $(EXTRA_OPS_SO)
	sh tests/run_cli_tests.sh ./ikcas

bench: ikcas_test
	./ikcas_test bench

debug: ikcas_debug ikcas_test_debug ikcas_test_frontend_debug

# Same suites under AddressSanitizer and UBSan.
asan: ikcas_asan ikcas_test_asan ikcas_test_frontend_asan $(EXTRA_OPS_SO)
	./ikcas_test_asan
	./ikcas_test_frontend_asan
	sh tests/run_cli_tests.sh ./ikcas_asan

# Regenerates compile_commands.json for clangd (needs bear).
compdb:
	bear -- $(MAKE) -B ikcas ikcas_test ikcas_test_frontend

# Leaves plugins.conf alone, it is user configuration.
clean:
	rm -rf $(BUILD) ikcas ikcas_test ikcas_test_frontend ikcas_debug ikcas_test_debug \
		ikcas_test_frontend_debug ikcas_asan ikcas_test_asan ikcas_test_frontend_asan

# Header dependencies written by -MMD.
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
