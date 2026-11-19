// Check that header file info for a file is found in a module file loaded after
// earlier lookups of that file found no information in any loaded module file.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -I %t/include -fsyntax-only -verify %t/tu.m

//--- include/module.modulemap
module A { header "A.h" }
module B { header "B.h" }

//--- include/A.h
void a(void);

//--- include/B.h
#include "A.h"
#include "Once.h"
#import "Imported.h"

//--- include/Once.h
#pragma once
#ifdef ONCE_H
#error Once.h entered twice
#endif
#define ONCE_H

//--- include/Imported.h
#ifdef IMPORTED_H
#error Imported.h entered twice
#endif
#define IMPORTED_H

//--- tu.m
// expected-no-diagnostics

// Look up the textual headers before any module file is loaded.
#if !__has_include("Once.h") || !__has_include("Imported.h")
#error missing headers
#endif

#include "A.h"

// Look them up again with only A loaded (neither is part of A).
#if !__has_include("Once.h") || !__has_include("Imported.h")
#error missing headers
#endif

#include "B.h"

// B's module file says that both headers were already included.
#include "Once.h"
#import "Imported.h"

#if !defined(ONCE_H) || !defined(IMPORTED_H)
#error B's macros not visible
#endif
