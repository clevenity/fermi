# Fermi Language Reference

## Types

Type      Size     Notes
bool      1 byte   true / false
byte      1 byte   unsigned 8-bit
short     2 bytes  signed 16-bit
int       4 bytes  signed 32-bit
uint      4 bytes  unsigned 32-bit
long      8 bytes  signed 64-bit
ulong     8 bytes  unsigned 64-bit
float     4 bytes  IEEE 754 single precision
double    8 bytes  IEEE 754 double precision
char      1 byte   UTF-8 code unit
str       pointer  null-terminated UTF-8 string

## Variables

```fermi
let x = 42;           // immutable, type inferred
let y: int = 10;      // immutable, explicit type
mut z = 0;// mutable
mut w: float = 1.5;
```

## Functions

```fermi
fn add(a: int, b: int): int {
    return a + b;
}
```

### Extern functions

```fermi
fn printf(fmt: str, ...): int;
```

## Control Flow

```fermi
if (x > 0) { ... } else { ... }

while (x < 10) { x = x + 1; }

for (mut i = 0; i < 10; i = i + 1) { ... }

for item in collection { ... }
```

## Structs

```fermi
struct Point { x: int; y: int; }

let p = Point { x: 3, y: 4 };
let dx = p.x;
```

## Enums

```fermi
enum Color { Red; Green; Blue; }
enum Option { Some(int); None; }

let c = Color::Red;
match (c) {
    Color::Red => { io::print("red"); }
    Color::Green => { io::print("green"); }
    _ => { io::print("other"); }
}
```

## Match

```fermi
match (n) {
    0 => { io::print("zero"); }
    1..10 => { io::print("small"); }
    _ => { io::print("large"); }
}
```

## Classes

```fermi
class Counter {
    private { mut count: int = 0; }
    public {
        fn inc() { this.count = this.count + 1; }
        fn get(): int { return this.count; }
    }
}
```

## Defer

```fermi
fn process(): int {
    let f = open_file("data.txt");
    defer close_file(f);
    // f is closed when function returns
    return 0;
}
```

## Regions

```fermi
region {
    let buf = alloc(1024);
    // buf freed automatically at end of region
}
```

## Lambdas

```fermi
let double = fn(x: int): int {
    return x * 2;
};

let result = double(21);
```

## String Interpolation

```fermi
let name = "world";
io::print("Hello, %{name}!");
```

## Try / Result

```fermi
fn parse(s: str): Result<int> { ... }

fn main(): int {
    let n = try parse("42");
    return n;
}
```

## Import
this is how to import on this lang

```fermi
import <std.io>;       // io::print, io::input, io::flush
import <std.math>;     // math::sqrt, math::pow, math::sin, ...
import <std.str>;      // str::len, str::concat, str::trim, ...
import "local_module"; // user file, resolved relative to source file
```