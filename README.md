# blang

`blang`, a language inspired by `lox`

Implemented using VM and bytecode interpreter written in C.

## Usage

- `git clone https://github.com/joelbeedle/blang.git`
- `cd blang`
- `make`
- `./build/blang <optional: file>`

## Examples

```go
func fib(n) {
  if (n < 2) return n;
  return fib(n - 2) + fib(n - 1);
}
```

```go
func makeCounter() {
  let count = 0;
  return fun() {
    count = count + 1;
    return count;
  };
}
```

<sub>yes, blang stands for beedlelanguage</sub>
