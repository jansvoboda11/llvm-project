// Check that with lazily loaded module maps, the relocation check for a module
// that is not loaded yet only parses module maps to find where the module is
// defined, and does not load the module when it is still defined where the PCM
// expects. Without lazy loading, the module map is still loaded.

// RUN: rm -rf %t
// RUN: split-file %s %t

// RUN: %clang_cc1 -fsyntax-only -fmodules -fimplicit-module-maps \
// RUN:   -fmodules-cache-path=%t/cache -I %t/include/Top -I %t/include/Dep \
// RUN:   %t/tu.c

// RUN: %clang_cc1 -fsyntax-only -fmodules -fimplicit-module-maps \
// RUN:   -fmodules-cache-path=%t/cache -I %t/include/Top -I %t/include/Dep \
// RUN:   %t/tu.c -Rmodule-map -Rmodule-validation -Rmodule-build 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not=remark:
// RUN: %clang_cc1 -fsyntax-only -fmodules -fimplicit-module-maps \
// RUN:   -fmodules-cache-path=%t/cache -I %t/include/Top -I %t/include/Dep \
// RUN:   %t/tu.c -Rmodule-map -Rmodule-validation -Rmodule-build \
// RUN:   -fmodules-lazy-load-module-maps 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LAZY --implicit-check-not=remark:

// CHECK: remark: loading modulemap '{{.*}}Top{{/|\\}}module.modulemap'
// CHECK-COUNT-2: remark: checking if module 'Top' from '{{.*}}Top-{{.*}}.pcm' has relocated
// CHECK: remark: checking if module 'Dep' from '{{.*}}Dep-{{.*}}.pcm' has relocated
// CHECK: remark: loading modulemap '{{.*}}Dep{{/|\\}}module.modulemap'
// CHECK: remark: checking if module 'Dep' from '{{.*}}Dep-{{.*}}.pcm' has relocated
// CHECK: remark: validating {{.*}} input files in module 'Dep'
// CHECK: remark: validating {{.*}} input files in module 'Top'

// LAZY: remark: parsing modulemap '{{.*}}Top{{/|\\}}module.modulemap'
// LAZY: remark: loading parsed module 'Top'
// LAZY-COUNT-2: remark: checking if module 'Top' from '{{.*}}Top-{{.*}}.pcm' has relocated
// LAZY: remark: checking if module 'Dep' from '{{.*}}Dep-{{.*}}.pcm' has relocated
// LAZY: remark: parsing modulemap '{{.*}}Dep{{/|\\}}module.modulemap'
// LAZY: remark: checking if module 'Dep' from '{{.*}}Dep-{{.*}}.pcm' has relocated
// LAZY: remark: validating {{.*}} input files in module 'Dep'
// LAZY: remark: validating {{.*}} input files in module 'Top'

// Loading the module is still forced with -fmodules-force-redundant-lookup.
// RUN: %clang_cc1 -fsyntax-only -fmodules -fimplicit-module-maps \
// RUN:   -fmodules-cache-path=%t/cache -I %t/include/Top -I %t/include/Dep \
// RUN:   %t/tu.c -Rmodule-map -fmodules-force-redundant-lookup \
// RUN:   -fmodules-lazy-load-module-maps 2>&1 \
// RUN:   | FileCheck %s --check-prefix=FORCE
// FORCE: remark: loading parsed module 'Dep'

//--- include/Top/module.modulemap
module Top { header "Top.h" export * }
//--- include/Top/Top.h
#include "Dep.h"

//--- include/Dep/module.modulemap
module Dep {
  header "Dep.h"
  header "Dep2.h"
}
//--- include/Dep/Dep.h
int dep(void);
//--- include/Dep/Dep2.h
int dep2(void);

//--- tu.c
#include "Top.h"
int x(void) { return dep(); }
