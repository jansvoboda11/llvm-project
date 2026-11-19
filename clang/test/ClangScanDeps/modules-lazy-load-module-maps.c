// Check that -lazy-load-module-maps only creates the modules the scan needs,
// but otherwise produces the same results: the same modules, dependencies,
// link libraries, command lines and context hashes.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json

// RUN: clang-scan-deps -compilation-database %t/cdb.json -format experimental-full \
// RUN:   -o %t/eager.json 2> %t/eager.err
// RUN: clang-scan-deps -compilation-database %t/cdb.json -format experimental-full \
// RUN:   -lazy-load-module-maps -o %t/lazy.json 2> %t/lazy.err
// RUN: diff %t/eager.json %t/lazy.json
// RUN: cat %t/lazy.json | sed 's:\\\\\?:/:g' | FileCheck %s -DPREFIX=%/t

// The scanning PCMs of both modes are kept apart.
// RUN: find %t/cache -name 'Used-*.pcm' | count 2

// Only the lazy scan loads modules on demand, and it doesn't load the unused
// ones.
// RUN: FileCheck %s --check-prefix=EAGER-REMARKS < %t/eager.err
// RUN: FileCheck %s --check-prefix=LAZY-REMARKS --implicit-check-not=Unused < %t/lazy.err
// EAGER-REMARKS-NOT: loading parsed module
// LAZY-REMARKS-DAG: remark: loading parsed module 'Used'
// LAZY-REMARKS-DAG: remark: loading parsed module 'Y'

// CHECK-NOT: lazy-load-module-maps
// CHECK:      "modules": [
// CHECK:          "clang-modulemap-file": "[[PREFIX]]/inc/module.modulemap",
// CHECK:          "link-libraries": [],
// CHECK-NEXT:     "name": "Used"
// CHECK:          "clang-modulemap-file": "[[PREFIX]]/inc/module.modulemap",
// CHECK:          "link-libraries": [],
// CHECK-NEXT:     "name": "Y"
// CHECK:      "translation-units": [
// CHECK:            "clang-module-deps": [
// CHECK-NEXT:         {
// CHECK-NEXT:           "context-hash": "{{.*}}",
// CHECK-NEXT:           "module-name": "Used"
// CHECK-NEXT:         },
// CHECK-NEXT:         {
// CHECK-NEXT:           "context-hash": "{{.*}}",
// CHECK-NEXT:           "module-name": "Y"
// CHECK-NEXT:         }
// CHECK-NEXT:       ],
// CHECK:            "-fmodule-map-file=[[PREFIX]]/inc/module.modulemap"

//--- cdb.json.template
[{
  "directory": "DIR",
  "command": "clang -fsyntax-only DIR/tu.m -I DIR/inc -fmodules -fimplicit-module-maps -fmodules-cache-path=DIR/cache -Rmodule-map",
  "file": "DIR/tu.m"
}]

//--- inc/module.modulemap
module Unused1 { header "unused1.h" }
module Unused2 { header "unused2.h" export * }
module Used { header "used.h" export * }
module Unused3 { umbrella "umb" }
module Unused4 { textual header "textual.h" }
// The 'export_as' module is only parsed, but must still replace the link name.
module X { header "x.h" link "X" }
module Y { header "y.h" export_as X link "Y" }

//--- inc/unused1.h
//--- inc/unused2.h
#include "unused1.h"
//--- inc/used.h
#include "textual.h"
//--- inc/textual.h
//--- inc/umb/u.h
//--- inc/x.h
//--- inc/y.h

//--- tu.m
#include "used.h"
#include "y.h"
