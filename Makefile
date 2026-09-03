# cerco bootstrap build (builds the cerco CLI; app builds use the CLI itself)
CC ?= clang
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -I include -I runtime/shared -I runtime/server
UVINC = -I vendor/libuv/include -I vendor/llhttp/include
LLVM_DIR ?= $(shell brew --prefix llvm 2>/dev/null || echo /usr/local/opt/llvm)
LLD_DIR ?= $(shell brew --prefix lld 2>/dev/null || echo /usr/local/opt/lld)

BUILD = build
OBJ = $(BUILD)/obj
LIB = $(BUILD)/lib
BIN = $(BUILD)/cerco

SHARED_SRC = runtime/shared/arena.c runtime/shared/sb.c runtime/shared/str.c \
             runtime/shared/wire.c runtime/shared/sha256.c
CLI_SRC = cli/main.c cli/util.c cli/sdk.c cli/doctor.c cli/new.c cli/tailwind.c \
          cli/build.c cli/dev.c cli/bundle.c

all: $(BIN)

$(BUILD)/obj/deps/.stamp: scripts/build_deps.sh
	@mkdir -p $(BUILD)/obj/deps $(LIB)
	CC=$(CC) ./scripts/build_deps.sh $(BUILD)
	@touch $@

$(BUILD)/obj/mkbundle: scripts/mkbundle.c
	@mkdir -p $(BUILD)/obj
	$(CC) -O2 -o $@ $<

cli/bundle.c cli/bundle.h: $(BUILD)/obj/mkbundle $(BUILD)/obj/deps/.stamp FORCE
	$(BUILD)/obj/mkbundle . \
		"$(shell uname -s | tr '[:upper:]' '[:lower:]')" \
		"$(shell uname -m)" \
		$(abspath $(LIB)/libuv.a) $(abspath $(LIB)/libllhttp.a)

FORCE:

$(BIN): cli/bundle.c $(CLI_SRC) $(SHARED_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(CLI_SRC) $(SHARED_SRC)
	@echo "cerco CLI: $@"

install: $(BIN)
	mkdir -p ~/.local/bin
	cp $(BIN) ~/.local/bin/cerco
	@echo "installed to ~/.local/bin/cerco (make sure it is on PATH)"

clean:
	rm -rf $(BUILD) cli/bundle.c cli/bundle.h

test: $(BIN)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $(BUILD)/test_unit \
		tests/unit/unit_main.c tests/unit/unit_shared.c tests/unit/unit_router.c \
		$(SHARED_SRC) runtime/server/router.c runtime/server/log.c \
		runtime/server/util.c runtime/server/config.c runtime/server/req.c \
		runtime/server/render.c runtime/server/sf.c runtime/server/dispatch.c \
		runtime/server/static.c runtime/server/http.c runtime/server/worker.c \
		runtime/server/serve.c \
		$(UVINC) $(BUILD)/lib/libuv.a $(BUILD)/lib/libllhttp.a -lm -lpthread \
		-framework CoreFoundation -framework CoreServices 2>/dev/null
	$(BUILD)/test_unit

test-integration: $(BIN) test
	$(CC) $(CFLAGS) -o $(BUILD)/http_test tests/integration/http_test.c
	cd tests/integration/app && $(abspath $(BIN)) build --dev-assets && \
		$(abspath $(BIN)) build --debug && $(abspath $(BIN)) build
	$(BUILD)/http_test

test-tsan:
	$(CC) $(CFLAGS) -fsanitize=thread -o $(BUILD)/test_unit_tsan \
		tests/unit/unit_main.c tests/unit/unit_shared.c tests/unit/unit_router.c \
		$(SHARED_SRC) runtime/server/router.c runtime/server/log.c \
		runtime/server/util.c runtime/server/config.c runtime/server/req.c \
		runtime/server/render.c runtime/server/sf.c runtime/server/dispatch.c \
		runtime/server/static.c runtime/server/http.c runtime/server/worker.c \
		runtime/server/serve.c \
		$(UVINC) $(BUILD)/lib/libuv.a $(BUILD)/lib/libllhttp.a -lm -lpthread \
		-framework CoreFoundation -framework CoreServices
	TSAN_OPTIONS="halt_on_error=0" $(BUILD)/test_unit_tsan

.PHONY: all install clean test test-integration test-tsan FORCE
