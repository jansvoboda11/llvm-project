// RUN: rm -rf %t
// RUN: split-file %s %t

//--- cdb.json.in
[
{
  "directory": "DIR",
  "command": "clang -E DIR/vfsoverlay_input.cpp -IInputs -ivfsoverlay DIR/vfsoverlay.yaml",
  "file": "DIR/vfsoverlay_input.cpp"
},
{
  "directory": "DIR",
  "command": "clang-cl /E /IInputs -Xclang -ivfsoverlay -Xclang DIR/vfsoverlay.yaml -- DIR/vfsoverlay_input_clangcl.cpp",
  "file": "DIR/vfsoverlay_input_clangcl.cpp"
}
]

//--- vfsoverlay.yaml.in
{
  'version': 0,
  'roots': [
    { 'name': 'DIR', 'type': 'directory',
      'contents': [
        { 'name': 'not_real.h', 'type': 'file',
          'external-contents': 'DIR/header.h'
        }
      ]
    }
  ]
}

//--- header.h
// empty
//--- vfsoverlay_input.cpp
#include "not_real.h"
//--- vfsoverlay_input_clangcl.cpp
#include "not_real.h"

// RUN: sed -e "s|DIR|%/t|g" %t/vfsoverlay.yaml.in > %t/vfsoverlay.yaml
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/cdb.json
//
// RUN: clang-scan-deps -compilation-database %t/cdb.json -mode preprocess-dependency-directives -j 1 | FileCheck %s
// RUN: clang-scan-deps -compilation-database %t/cdb.json -mode preprocess                       -j 1 | FileCheck %s

// CHECK: vfsoverlay_input.o
// CHECK-NEXT: vfsoverlay_input.cpp
// CHECK-NEXT: header.h

// CHECK: vfsoverlay_input_clangcl.o
// CHECK-NEXT: vfsoverlay_input_clangcl.cpp
// CHECK-NEXT: header.h
