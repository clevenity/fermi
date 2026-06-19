# Fermi Language Reference

## Table of Contents

1. [Type System](#type-system)
2. [Variables](#variables)
3. [Functions](#functions)
4. [Control Flow](#control-flow)
5. [Composite Types](#composite-types)
6. [Pattern Matching](#pattern-matching)
7. [Object-Oriented Programming](#object-oriented-programming)
8. [Memory Management](#memory-management)
9. [Advanced Features](#advanced-features)
10. [Modules and Imports](#modules-and-imports)

---

## Type System

Fermi is a statically-typed language with type inference capabilities. The following primitive types are supported:

| Type   | Size    | Range / Characteristics |
|--------|---------|------------------------|
| `bool` | 1 byte  | `true` or `false` |
| `byte` | 1 byte  | Unsigned 8-bit integer (0–255) |
| `short` | 2 bytes | Signed 16-bit integer (−32,768 to 32,767) |
| `int` | 4 bytes | Signed 32-bit integer (−2,147,483,648 to 2,147,483,647) |
| `uint` | 4 bytes | Unsigned 32-bit integer (0 to 4,294,967,295) |
| `long` | 8 bytes | Signed 64-bit integer (−9,223,372,036,854,775,808 to 9,223,372,036,854,775,807) |
| `ulong` | 8 bytes | Unsigned 64-bit integer (0 to 18,446,744,073,709,551,615) |
| `float` | 4 bytes | IEEE 754 single-precision floating-point |
| `double` | 8 bytes | IEEE 754 double-precision floating-point |
| `char` | 1 byte | Single UTF-8 code unit |
| `str` | pointer | Null-terminated UTF-8 string reference |

---

## Variables

Variables are declared using `let` (immutable) or `mut` (mutable) keywords. The type can be explicitly specified or inferred from the assigned value.

### Declaration Syntax

```fermi
let x = 42;                    // Immutable, type inferred as int
let y: int = 10;               // Immutable, explicit type annotation
let name: str = "Fermi";       // Immutable string

mut z = 0;                     // Mutable, type inferred as int
mut w: float = 1.5;            // Mutable, explicit type annotation
mut counter: uint = 0;         // Mutable unsigned integer
```

### Mutability

- **`let` bindings**: Create immutable variables. The bound value cannot be reassigned.
- **`mut` bindings**: Create mutable variables that can be reassigned after declaration.

```fermi
let immutable = 5;
// immutable = 10;           // Compile error: cannot reassign immutable binding

mut mutable = 5;
mutable = 10;                  // OK
```

---

## Functions

Functions are defined with the `fn` keyword and must specify parameter types and return type using the `->` syntax.

### Basic Functions

```fermi
fn add(a: int, b: int) -> int {
    return a + b;
}

fn greet(name: str) -> str {
    return "Hello, " + name;
}
```

### Implicit Return

The last expression in a function is implicitly returned if no explicit `return` statement is present:

```fermi
fn multiply(a: int, b: int) -> int {
    a * b               // Implicitly returned
}
```

### Variadic Functions

Functions can accept a variable number of arguments using the `...` syntax. This is commonly used with C interop functions:

```fermi
fn printf(fmt: str, ...) -> int;    // Extern C function
fn custom_log(level: str, ...) -> void;
```

### Function Pointers and First-Class Functions

Functions are first-class values and can be stored in variables or passed as arguments:

```fermi
let operation: fn(int, int) -> int = add;
let result = operation(3, 4);       // result = 7
```

---

## Control Flow

### Conditional Statements

#### If / Else

```fermi
if (x > 0) {
    io::print("positive");
} else if (x < 0) {
    io::print("negative");
} else {
    io::print("zero");
}
```

#### If Expressions

Conditionals can be used as expressions:

```fermi
let sign: str = if (x > 0) "positive" else "negative";
```

### Loops

#### While Loop

```fermi
mut counter = 0;
while (counter < 10) {
    io::print(counter);
    counter = counter + 1;
}
```

#### For Loop (C-style)

```fermi
for (mut i = 0; i < 10; i = i + 1) {
    io::print(i);
}
```

#### For-In Loop (Iterator)

```fermi
for item in collection {
    io::print(item);
}
```

### Loop Control

- **`break`**: Exit the current loop immediately
- **`continue`**: Skip to the next iteration

```fermi
for (mut i = 0; i < 10; i = i + 1) {
    if (i == 5) {
        break;                  // Exit loop
    }
    if (i == 2) {
        continue;               // Skip to next iteration
    }
    io::print(i);
}
```

---

## Composite Types

### Structs

Structs are used to group related data together. All fields are public by default.

#### Definition and Instantiation

```fermi
struct Point {
    x: int;
    y: int;
}

struct Person {
    name: str;
    age: int;
    email: str;
}

// Instantiation
let origin = Point { x: 0, y: 0 };
let person = Person { name: "Alice", age: 30, email: "alice@example.com" };
```

#### Field Access

```fermi
let p = Point { x: 3, y: 4 };
let dx = p.x;                   // dx = 3
let dy = p.y;                   // dy = 4
```

#### Struct Methods

Structs can have associated functions (static methods) and instance methods:

```fermi
struct Rectangle {
    width: int;
    height: int;
}

// Associated function (static)
fn Rectangle::new(w: int, h: int) -> Rectangle {
    Rectangle { width: w, height: h }
}

// Instance method
fn Rectangle::area(this) -> int {
    this.width * this.height
}
```

### Enums

Enums define a set of named variants. Each variant can optionally carry associated data.

#### Simple Enums

```fermi
enum Color {
    Red;
    Green;
    Blue;
}

let primary = Color::Red;
```

#### Enums with Data

```fermi
enum Result {
    Ok(int);
    Error(str);
}

enum Option {
    Some(int);
    None;
}

let success = Result::Ok(42);
let failure = Result::Error("Not found");
```

---

## Pattern Matching

### Match Expressions

Match expressions provide a powerful way to destructure and pattern match values:

```fermi
match (status_code) {
    200 => {
        io::print("OK");
    }
    404 => {
        io::print("Not Found");
    }
    500..599 => {
        io::print("Server Error");
    }
    _ => {
        io::print("Other");
    }
}
```

### Enum Pattern Matching

```fermi
enum Status { Active; Inactive; Pending; }

let state = Status::Active;

match (state) {
    Status::Active => {
        io::print("System is running");
    }
    Status::Inactive => {
        io::print("System is stopped");
    }
    Status::Pending => {
        io::print("System is initializing");
    }
}
```

### Destructuring with Match

```fermi
enum Result {
    Ok(int);
    Error(str);
}

let result = Result::Ok(42);

match (result) {
    Result::Ok(value) => {
        io::print("Success: ");
        io::print(value);
    }
    Result::Error(msg) => {
        io::print("Error: ");
        io::print(msg);
    }
}
```

---

## Object-Oriented Programming

### Classes

Classes encapsulate data and behavior with support for access modifiers and instance methods.

#### Class Definition

```fermi
class Counter {
    private {
        mut count: int = 0;
    }

    public {
        fn inc() {
            this.count = this.count + 1;
        }

        fn dec() {
            this.count = this.count - 1;
        }

        fn get() -> int {
            return this.count;
        }

        fn reset() {
            this.count = 0;
        }
    }
}
```

#### Access Modifiers

- **`public`**: Members are accessible from outside the class
- **`private`**: Members are only accessible within the class

#### Constructor and Instantiation

```fermi
class Stack {
    private {
        mut items: array;
        mut size: int = 0;
    }

    public {
        fn init() {
            this.items = alloc(100);
            this.size = 0;
        }

        fn push(value: int) {
            this.items[this.size] = value;
            this.size = this.size + 1;
        }

        fn pop() -> int {
            this.size = this.size - 1;
            return this.items[this.size];
        }
    }
}
```

---

## Memory Management

### Defer Statement

The `defer` statement schedules a function call to be executed when the current scope exits. This is useful for resource cleanup (RAII pattern).

#### Usage

```fermi
fn read_file() -> str {
    let file = open_file("data.txt");
    defer close_file(file);           // Guaranteed to execute before return

    let content = read_all(file);
    return content;
}
```

#### Multiple Defers

When multiple defer statements are present, they execute in LIFO (Last In, First Out) order:

```fermi
fn process() -> int {
    defer io::print("Third");
    defer io::print("Second");
    defer io::print("First");
    
    return 0;
    // Output: First, Second, Third
}
```

### Regions

Regions provide lexically-scoped memory management. All allocations within a region are automatically freed at the end of the region block.

#### Usage

```fermi
region {
    let buf = alloc(1024);           // Allocate 1024 bytes
    // Use buf...
}                                     // buf automatically freed here

// buf is no longer accessible
```

#### Nested Regions

```fermi
region outer {
    let data1 = alloc(512);
    
    region inner {
        let data2 = alloc(256);
        // data2 is freed here
    }
    
    // data1 still available here
}
// data1 is freed here
```

---

## Advanced Features

### Lambda Expressions

Anonymous functions (lambdas) can be defined inline and assigned to variables or passed to other functions:

```fermi
let double = fn(x: int) -> int {
    return x * 2;
};

let result = double(21);            // result = 42

// Lambda as argument
fn apply(operation: fn(int) -> int, value: int) -> int {
    return operation(value);
}

let squared = apply(fn(x: int) -> int { x * x }, 5);  // squared = 25
```

### String Interpolation

Fermi supports string interpolation using the `%{...}` syntax:

```fermi
let name = "World";
let age = 25;

io::print("Hello, %{name}!");
io::print("Age: %{age} years");

// With expressions
io::print("Result: %{10 + 5}");
```

### Error Handling with Try/Result

The `Result` type and `try` operator provide a mechanism for error handling without exceptions:

#### Result Type

```fermi
enum Result {
    Ok(int);
    Error(str);
}
```

#### Try Operator

The `try` operator unwraps a `Result` value or returns early with an error:

```fermi
fn parse(s: str) -> Result {
    // Implementation
}

fn main() -> Result {
    let number = try parse("42");      // Unwrap or return error
    io::print(number);
    return Result::Ok(0);
}
```

#### Error Propagation

```fermi
fn chain_operations() -> Result {
    let value1 = try operation1();     // Returns early if error
    let value2 = try operation2(value1);
    return Result::Ok(value2);
}
```

---

## Modules and Imports

Fermi uses a module system for organizing code and managing dependencies.

### Import Statements

#### Standard Library Imports

```fermi
import <std.io>;                // I/O operations: print, input, flush
import <std.math>;              // Mathematical functions: sqrt, pow, sin, cos, etc.
import <std.str>;               // String utilities: len, concat, trim, split, etc.
import <std.array>;             // Array operations: map, filter, reduce, etc.
import <std.file>;              // File operations: open, read, write, close
import <std.time>;              // Time functions: now, sleep, format
```

#### Local Module Imports

```fermi
import "local_module";           // Relative path to user-defined module
import "./utils/helpers";        // Subdirectory reference
import "../common/constants";    // Parent directory reference
```

### Module Usage

After importing, identifiers are accessed with the `::` namespace operator:

```fermi
import <std.io>;
import <std.math>;
import "config";

io::print("Starting...");
let root = math::sqrt(16.0);     // root = 4.0
let version = config::VERSION;
```

### Creating Modules

Modules are defined in separate files with a `.fermi` extension:

**math_utils.fermi:**
```fermi
fn square(x: int) -> int {
    return x * x;
}

fn cube(x: int) -> int {
    return x * x * x;
}
```

**main.fermi:**
```fermi
import "math_utils";

fn main() {
    let result = math_utils::square(5);     // result = 25
}
```

---

## Type Conversion

### Explicit Casting

```fermi
let x: int = 42;
let y: long = x as long;         // Cast int to long
let z: float = x as float;       // Cast int to float
```

### Implicit Conversions

Fermi allows certain implicit conversions:

```fermi
let small: byte = 10;
let medium: int = small;         // Implicit: byte → int
let large: long = medium;        // Implicit: int → long
```

---

## Naming Conventions

- **Variables and functions**: Use `snake_case` (e.g., `calculate_area`, `user_name`)
- **Types (structs, enums, classes)**: Use `PascalCase` (e.g., `Point`, `UserProfile`, `HttpResponse`)
- **Constants**: Use `UPPER_CASE` (e.g., `MAX_SIZE`, `API_VERSION`)

---

## Comments

```fermi
// Single-line comment

/* Multi-line comment
   spanning multiple lines */
```

---

## Standard Library Overview

| Module | Purpose | Key Functions |
|--------|---------|---|
| `std.io` | Input/Output operations | `print()`, `input()`, `flush()` |
| `std.math` | Mathematical operations | `sqrt()`, `pow()`, `sin()`, `cos()`, `floor()`, `ceil()` |
| `std.str` | String manipulation | `len()`, `concat()`, `trim()`, `split()`, `substr()` |
| `std.array` | Array/Collection utilities | `map()`, `filter()`, `reduce()`, `sort()` |
| `std.file` | File I/O operations | `open()`, `read()`, `write()`, `close()` |
| `std.time` | Time/Date functions | `now()`, `sleep()`, `format()` |

---

## Best Practices

1. **Use type annotations** for function parameters and return types to improve code clarity
2. **Prefer `let` over `mut`** unless reassignment is necessary
3. **Use pattern matching** instead of nested if-else statements for better readability
4. **Leverage `defer`** for deterministic resource cleanup
5. **Use regions** for bulk memory management in performance-critical sections
6. **Document public APIs** with clear comments explaining behavior and constraints
7. **Follow naming conventions** to maintain consistency across your codebase
