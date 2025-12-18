// This provides coverage for file inclusion logic in asm directives.

// RUN: split-file %s %t
//--- foo.h
//--- tu.c
asm(".incbin \"foo.h\"");
// RUN: cd %t
// RUN: %clang -flto=thin -c %t/tu.c -o %t/tu.a
