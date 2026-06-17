# Fermi

A compiled, statically-typed systems programming language with an AOT pipeline, written in C11 on top of LLVM.

```
Source → Lexer → Parser → AST → HIR → Type Checker → Sema → FIR (codegen) → Optimizer → LLVM IR → Machine Code
```

> Status: v0.1.0, pre-alpha. The language and compiler are both under active development; expect breaking changes.

## Features

- **AOT compilation** — compiles to a native binary via LLVM + the system `clang`
- **Compile-cache + exec** — re-running an unchanged source file skips compilation and execs the cached binary
- **Syscall-based I/O runtime** — `fe_print_*` / `fe_println_*` write directly via `write(2)`, no `stdio.h` in the hot path
- **Static typing** — explicit type annotations, structs, enums, `match` expressions
- **Regions** — scope-based memory management
- **Closures / lambdas**, **`defer`** statements
- **String interpolation** via `%{expr}`
- **`try` / `Result`** error propagation

## Quick Start

```sh
./build.sh                 # make -j$(nproc); produces build/fermi
sudo make install          # PREFIX=/usr/local by default

fermi hello.fe              # compile (cached) + run
fermi -o hello hello.fe     # compile only, don't run
./hello
```

## Language Sample

```fermi
import <std.io>;

struct Point {
    x: int;
    y: int;

    fn dist_sq(self): int {
        return self.x * self.x + self.y * self.y;
    }
}

fn main(): int {
    let p = Point { x: 3, y: 4 };
    io::print(p.dist_sq());
    return 0;
}
```

## CLI

```
fermi [options] <file.fe> [-- prog_args...]

Options:
  --fir           Dump FIR to stdout
  --ast           Parse only
  --lex           Lex only (dump tokens)
  -o <out>        Output executable (do not run)
  --no-opt        Disable FIR optimizer
  --time          Print per-phase timing
  --target <t>    LLVM target triple
  -O0..3          LLVM optimization level (default: -O2)
  --cache-clear   Clear the compiled binary cache
  --version, -v   Print version
```

**Default mode** (no `-o`, no dump flags): compiles, caches the binary under
`~/.cache/fermi/<hash>` keyed off a stat()-based hash of the source, and execs it.
Subsequent `fermi file.fe` invocations on an unchanged file skip compilation
and exec the cached binary directly.

## Build

Requirements:
- `clang` (compiler for the Fermi toolchain itself, and the C backend driver — any recent version)
- `llvm-config` on `PATH` (LLVM core + native target libs)
- `make`, `ar`

```sh
make            # builds build/fermi and build/libfermi_rt.a, same as `make install`
make install    # also copies build/fermi -> $(PREFIX)/bin/fermi  (PREFIX defaults to empty — set it explicitly)
make clean
```

`PREFIX` must be set for `install` to land somewhere sane, e.g. `make install PREFIX=/usr/local`.

### Nix

```sh
nix build       # produces result/bin/fermi
nix develop     # dev shell with gcc, clang, llvm, hyperfine, valgrind, gdb
```

### Tests

```sh
./tests/run_tests.sh           # runs every tests/t*.fe through the frontend + LLVM emission
./tests/run_tests.sh --clang   # also compiles and executes each test binary
```

A test passes if `fermi` exits 0 with no `[parse error]` / `[error]` lines on stderr (and, with `--clang`, if the resulting binary also runs successfully).

## Runtime

`libfermi_rt.a` is linked into every compiled Fermi program:

- **`rt/fermi_rt.S`** — `fe_print_*` / `fe_println_*`, raw `write(2)` syscalls, x86-64 only
- **`rt/fermi_rt.c`** — math/string helpers, wraps GCC/Clang builtins where possible
- **`rt/fermi_rt.h`** — public C ABI shared between the runtime and generated code

## Standard Library

| Module  | Import                | Namespace |
|---------|------------------------|-----------|
| I/O     | `import <std.io>;`    | `io::`    |
| Math    | `import <std.math>;`  | `math::`  |
| Strings | `import <std.str>;`   | `str::`   |

## Project Layout

```
fermi/
├── src/            compiler source (C11)
│   ├── fermi/      shared version/config header
│   ├── fearena/    arena allocator
│   ├── felexer/    lexer + tokens
│   ├── feparser/   parser + AST
│   ├── fehir/      HIR lowering
│   ├── fetc/       type checker
│   ├── fesema/     semantic analysis
│   ├── fecodegen/  FIR (mid-level IR) codegen
│   ├── feopt/      FIR optimizer
│   └── fellvm/     LLVM IR emitter
├── rt/             runtime library, linked into every compiled program
├── stdlib/         Fermi standard library sources (.fe)
├── tests/          test suite (.fe files) + run_tests.sh
├── docs/           language reference
├── flake.nix       Nix flake (build + dev shell)
├── build.sh        thin wrapper around `make -j`
└── Makefile
```

See [`docs/language.md`](docs/language.md) for the language reference, and
[`CONTRIBUTING.md`](CONTRIBUTING.md) for how to send patches.

## License

Apache License 2.0 — see [LICENSE](LICENSE).
# fermi
