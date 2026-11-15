// Check that the scanner compresses the embedded source buffers of the PCMs it
// builds (e.g. the predefines buffer) with a faster compression level than
// regular implicit module builds of the same module.

// REQUIRES: zlib || zstd

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json

// RUN: %clang -fsyntax-only %t/tu.c -I %t/include -fmodules -fimplicit-module-maps \
// RUN:   -fmodules-cache-path=%t/cache-regular
// RUN: llvm-bcanalyzer --dump --disable-histogram %t/cache-regular/*/Mod-*.pcm > %t/regular.dump

// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 -format experimental-full > %t/deps.json
// RUN: llvm-bcanalyzer --dump --disable-histogram %t/cache/*/Mod-*.pcm > %t/scan.dump

// RUN: FileCheck %s --check-prefix=SCAN --input-file %t/scan.dump
// RUN: cat %t/regular.dump %t/scan.dump | FileCheck %s --check-prefix=CMP

// The scanner compresses the predefines buffer.
// SCAN:      <SOURCE_MANAGER_BLOCK
// SCAN-NOT:  </SOURCE_MANAGER_BLOCK>
// SCAN:      <SM_SLOC_BUFFER_BLOB_COMPRESSED abbrevid=[[#]] op0={{[0-9][0-9][0-9][0-9]+}}/>

// The predefines buffer has the same contents in both PCMs, but its compressed
// size differs.
// CMP:       <SOURCE_MANAGER_BLOCK
// CMP-NOT:   </SOURCE_MANAGER_BLOCK>
// CMP:       <SM_SLOC_BUFFER_BLOB_COMPRESSED abbrevid=[[#]] op0=[[PREDEFINES_SIZE:[0-9]{4,}]]/> blob data = unprintable, [[#REGULAR_SIZE:]] bytes.
// CMP:       </SOURCE_MANAGER_BLOCK>
// CMP:       <SOURCE_MANAGER_BLOCK
// CMP-NOT:   op0=[[PREDEFINES_SIZE]]/> blob data = unprintable, [[#REGULAR_SIZE]] bytes.
// CMP:       </SOURCE_MANAGER_BLOCK>

//--- cdb.json.template
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu.c -I DIR/include -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache",
  "file": "DIR/tu.c"
}]

//--- include/module.modulemap
module Mod { header "mod.h" }

//--- include/mod.h
#define MOD 1

//--- tu.c
#include "mod.h"
