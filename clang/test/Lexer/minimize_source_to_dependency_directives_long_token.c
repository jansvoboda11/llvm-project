// Tokens in directives that are longer than the scanner can store make the
// scan fail.

// RUN: rm -rf %t && mkdir %t
// RUN: %python -c "print('#define LONG ' + 'x' * 70000)" > %t/long.c
// RUN: not %clang_cc1 -print-dependency-directives-minimized-source %t/long.c 2>&1 \
// RUN:   | FileCheck %s
// CHECK: long.c:1:14: error: token is too long for dependency directive scanning

// Tokens of the maximum length are fine.
// RUN: %python -c "print('#define LONG ' + 'x' * 65535)" > %t/max.c
// RUN: %clang_cc1 -print-dependency-directives-minimized-source %t/max.c \
// RUN:   | FileCheck %s --check-prefix=MAX
// MAX: {{^}}#define LONG xxxxxxxxxx{{x+$}}
