// Files whose directives contain tokens that are too long for the scanner are
// lexed normally instead.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.template > %t/cdb.json
// RUN: %python -c "print('#define LONG \"' + 'x' * 70000 + '\"')" > %t/long.h
// RUN: echo '#include "after.h"' >> %t/long.h

// RUN: clang-scan-deps -compilation-database %t/cdb.json 2>&1 | FileCheck %s
// CHECK-NOT: error
// CHECK:        t.c
// CHECK-NEXT:   long.h
// CHECK-NEXT:   after.h
// CHECK-NEXT:   defined.h

//--- cdb.json.template
[
  {
    "directory": "DIR",
    "command": "clang -fsyntax-only DIR/t.c -I DIR",
    "file": "DIR/t.c"
  }
]

//--- t.c
#include "long.h"
#ifdef LONG
#include "defined.h"
#endif

//--- after.h

//--- defined.h
