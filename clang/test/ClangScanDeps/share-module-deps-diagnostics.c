// Check that each translation unit reports the diagnostics of parsing its
// command line exactly once, as a regular scan does.

// RUN: rm -rf %t && split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json

// RUN: not clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full 2>&1 | FileCheck %s
// RUN: not clang-scan-deps -compilation-database %t/cdb.json -j 1 \
// RUN:   -format experimental-full -share-module-deps 2>&1 | FileCheck %s

// CHECK:     error: invalid value '99' in '-stack-protector 99'
// CHECK:     error: invalid value '99' in '-stack-protector 99'
// CHECK-NOT: error: invalid value

//--- cdb.json.template
[{
  "directory": "DIR",
  "file": "DIR/a.c",
  "command": "clang -Xclang -stack-protector -Xclang 99 -c DIR/a.c -o DIR/a.o"
},{
  "directory": "DIR",
  "file": "DIR/b.c",
  "command": "clang -Xclang -stack-protector -Xclang 99 -c DIR/b.c -o DIR/b.o"
}]

//--- a.c
//--- b.c
