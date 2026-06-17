LLVM_CONFIG := llvm-config

LLVM_CFLAGS := $(shell $(LLVM_CONFIG) --cflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs core native)

CC := clang

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic \
          -Wno-unused-parameter \
          -Wno-unused-function \
          -Wno-sign-compare \
          -Wno-missing-field-initializers \
          -Isrc \
          $(LLVM_CFLAGS) \
          -O2

LDFLAGS := $(LLVM_LDFLAGS) $(LLVM_LIBS) -lm

BUILD := build
RT_DIR := rt

PREFIX ?= $(PREFIX)
BINDIR := $(PREFIX)/bin

SRCS := \
	src/main.c \
	src/fearena/arena.c \
	src/felexer/lexer.c \
	src/felexer/token.c \
	src/feparser/parser.c \
	src/fehir/hir.c \
	src/fetc/tc.c \
	src/fesema/sema.c \
	src/fecodegen/fir.c \
	src/fecodegen/codegen.c \
	src/feopt/opt.c \
	src/fellvm/llvm_emit.c

OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

RT_SRC := $(RT_DIR)/fermi_rt.c
RT_OBJ := $(BUILD)/fermi_rt.o
RT_LIB := $(BUILD)/libfermi_rt.a

all: install

install: $(BUILD)/fermi $(RT_LIB)
	@mkdir -p $(BINDIR)
	cp $(BUILD)/fermi $(BINDIR)/fermi
	chmod +x $(BINDIR)/fermi
	@echo "Installed fermi -> $(BINDIR)/fermi"

$(BUILD)/fermi: $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(RT_OBJ): $(RT_SRC)
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Wno-unused-parameter \
	      -O2 -Irt -c -o $@ $<

$(RT_LIB): $(RT_OBJ)
	ar rcs $@ $^

clean:
	rm -rf $(BUILD)

.PHONY: all install clean