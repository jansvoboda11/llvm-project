// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: mkdir %t/foodir

//--- cdb.json.in
[
    {
      "directory": "DIR",
      "command": "clang -c -IDIR -IDIR/foodir -IInputs DIR/headerwithdirname_input.cpp",
      "file": "DIR/headerwithdirname_input.cpp"
    },
    {
      "directory": "DIR",
      "command": "clang-cl /c /IDIR /IDIR/foodir -IInputs -- DIR/headerwithdirname_input_clangcl.cpp",
      "file": "DIR/headerwithdirname_input_clangcl.cpp"
    }
]
//--- Inputs/foodir
// A C++ header with same name as that of a directory in the include path.
//--- headerwithdirname_input.cpp
#include <foodir>
//--- headerwithdirname_input_clangcl.cpp
#include <foodir>

// RUN: mkdir %t/foodir
// RUN: mkdir %t.dir/Inputs
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json
//
// RUN: clang-scan-deps -compilation-database %t/cdb.json -j 1 | FileCheck %s

#include <foodir>

// CHECK: headerwithdirname_input{{\.o|.*\.s}}
// CHECK-NEXT: headerwithdirname_input.cpp
// CHECK-NEXT: Inputs{{/|\\}}foodir

// CHECK: headerwithdirname_input_clangcl.o
// CHECK-NEXT: headerwithdirname_input_clangcl.cpp
// CHECK-NEXT: Inputs{{/|\\}}foodir
