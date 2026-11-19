// Check that relocation and module map mismatch diagnostics are unchanged when
// the relocation check finds where a module is defined without loading it, and
// that it does skip loading the module when nothing changed. This only
// applies with lazily loaded module maps.

// RUN: rm -rf %t
// RUN: split-file %s %t
// RUN: cd %t

// A module found in a normal search directory.
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -emit-pch -x objective-c-header -I a/Top -I a/Dep a.h -o a.pch
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I a/Top -I a/Dep -include-pch a.pch use.m -Rmodule-map 2>&1 \
// RUN:   | FileCheck %s --check-prefix=A-SAME --implicit-check-not="remark: loading"
// A-SAME: remark: parsing modulemap 'a/Top/module.modulemap'
// A-SAME: remark: parsing modulemap 'a/Dep/module.modulemap'

// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I a/Top -I a/Other -I a/Dep -include-pch a.pch use.m 2>&1 \
// RUN:   | FileCheck %s --check-prefix=A-RELOCATED
// A-RELOCATED: fatal error: module 'Dep' was built in directory '{{.*}}a{{/|\\}}Dep' but now resides in directory 'a/Other'

// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I a/Top -I a/Other -I a/Dep -include-pch a.pch use.m \
// RUN:   -fno-validate-pch

// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I a/Top -include-pch a.pch use.m 2>&1 \
// RUN:   | FileCheck %s --check-prefix=A-MISSING
// A-MISSING: fatal error: module 'Dep' in precompiled file '{{.*}}Dep-{{.*}}.pcm' (imported by precompiled file '{{.*}}Top-{{.*}}.pcm') is not defined in any loaded module map file; maybe you need to load '{{.*}}a{{/|\\}}Dep{{/|\\}}module.modulemap'?

// The definition of a framework module moves to the private module map in the
// same directory.
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -emit-pch -x objective-c-header -F b b.h -o b.pch
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -F b -include-pch b.pch use.m -Rmodule-map 2>&1 \
// RUN:   | FileCheck %s --check-prefix=B-SAME --implicit-check-not="remark: loading"
// B-SAME: remark: parsing modulemap 'b/Top.framework/Modules/module.modulemap'
// B-SAME: remark: parsing modulemap 'b/Dep.framework/Modules/module.modulemap'

// RUN: mv b/Dep.framework/Modules/module.modulemap b/Dep.framework/Modules/module.private.modulemap
// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -F b -include-pch b.pch use.m 2>&1 \
// RUN:   | FileCheck %s --check-prefix=B-CHANGED
// B-CHANGED: fatal error: module 'Dep' in precompiled file '{{.*}}Top-{{.*}}.pcm' found in a different module map file (b/Dep.framework/Modules/module.private.modulemap) than when the importing precompiled file was built (b/Dep.framework/Modules/module.modulemap)

// A module with an additional module map is loaded.
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -emit-pch -x objective-c-header -F c c.h -o c.pch -Wno-private-module
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -F c -include-pch c.pch use.m -Rmodule-map -Wno-private-module 2>&1 \
// RUN:   | FileCheck %s --check-prefix=C-SAME --implicit-check-not="remark: loading"
// C-SAME: remark: parsing modulemap 'c/Top.framework/Modules/module.modulemap'
// C-SAME: remark: parsing modulemap 'c/Dep.framework/Modules/module.modulemap'
// C-SAME: remark: parsing modulemap 'c/Dep.framework/Modules/module.private.modulemap'
// C-SAME: remark: loading parsed module 'Dep'

// RUN: mv c/Dep.framework/Modules/module.private.modulemap c/Dep.framework/Modules/moved.modulemap
// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -F c -include-pch c.pch use.m 2>&1 \
// RUN:   | FileCheck %s --check-prefix=C-CHANGED
// C-CHANGED: fatal error: malformed or corrupted precompiled file: 'could not find file 'c/Dep.framework/Modules/module.private.modulemap' referenced by AST file'

// An inferred framework module is loaded.
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -emit-pch -x objective-c-header -I d/Top -F d/F d.h -o d.pch
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I d/Top -F d/F -include-pch d.pch use.m -Rmodule-map 2>&1 \
// RUN:   | FileCheck %s --check-prefix=D-SAME --implicit-check-not="remark: loading"
// D-SAME: remark: parsing modulemap 'd/Top/module.modulemap'
// D-SAME: remark: loading modulemap '{{.*}}d{{/|\\}}F{{/|\\}}module.modulemap'

// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I d/Top -F d/F2 -F d/F -include-pch d.pch use.m 2>&1 \
// RUN:   | FileCheck %s --check-prefix=D-RELOCATED
// D-RELOCATED: fatal error: module 'Dep' was built in directory '{{.*}}d{{/|\\}}F{{/|\\}}Dep.framework' but now resides in directory 'd/F2/Dep.framework'

// A private framework module found through the fallback search for Foo_Private.
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -emit-pch -x objective-c-header -I e/Top -F e/F e.h -o e.pch
// RUN: %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I e/Top -F e/F -include-pch e.pch use.m -Rmodule-map 2>&1 \
// RUN:   | FileCheck %s --check-prefix=E-SAME --implicit-check-not="remark: loading"
// E-SAME: remark: parsing modulemap 'e/Top/module.modulemap'
// E-SAME: remark: parsing modulemap 'e/F/Dep.framework/Modules/module.modulemap'
// E-SAME: remark: parsing modulemap 'e/F/Dep.framework/Modules/module.private.modulemap'

// RUN: cp -R e/F e/F2
// RUN: not %clang_cc1 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t/cache \
// RUN:   -fsyntax-only -fmodules-lazy-load-module-maps -I e/Top -F e/F2 -F e/F -include-pch e.pch use.m 2>&1 \
// RUN:   | FileCheck %s --check-prefix=E-RELOCATED
// E-RELOCATED: fatal error: module 'Dep_Private' was built in directory '{{.*}}e{{/|\\}}F{{/|\\}}Dep.framework' but now resides in directory 'e/F2/Dep.framework'

//--- use.m
int use(void);

//--- a.h
#include "Top.h"
//--- a/Top/module.modulemap
module Top { header "Top.h" export * }
//--- a/Top/Top.h
#include "Dep.h"
//--- a/Dep/module.modulemap
module Dep { header "Dep.h" }
//--- a/Dep/Dep.h
int dep(void);
//--- a/Other/module.modulemap
module Dep { header "Dep.h" }
//--- a/Other/Dep.h
int dep(void);

//--- b.h
#include <Top/Top.h>
//--- b/Top.framework/Modules/module.modulemap
framework module Top { header "Top.h" export * }
//--- b/Top.framework/Headers/Top.h
#include <Dep/Dep.h>
//--- b/Dep.framework/Modules/module.modulemap
framework module Dep { header "Dep.h" }
//--- b/Dep.framework/Headers/Dep.h
int dep(void);

//--- c.h
#include <Top/Top.h>
//--- c/Top.framework/Modules/module.modulemap
framework module Top { header "Top.h" export * }
//--- c/Top.framework/Headers/Top.h
#include <Dep/Dep.h>
//--- c/Dep.framework/Modules/module.modulemap
framework module Dep { header "Dep.h" }
//--- c/Dep.framework/Modules/module.private.modulemap
explicit module Dep.Private { header "DepPrivate.h" }
//--- c/Dep.framework/Headers/Dep.h
int dep(void);
//--- c/Dep.framework/PrivateHeaders/DepPrivate.h
int dep_private(void);

//--- d.h
#include "Top.h"
//--- d/Top/module.modulemap
module Top { header "Top.h" export * }
//--- d/Top/Top.h
#include <Dep/Dep.h>
//--- d/F/module.modulemap
framework module * {}
//--- d/F/Dep.framework/Headers/Dep.h
int dep(void);
//--- d/F2/module.modulemap
framework module * {}
//--- d/F2/Dep.framework/Headers/Dep.h
int dep(void);

//--- e.h
#include "Top.h"
//--- e/Top/module.modulemap
module Top { header "Top.h" export * }
//--- e/Top/Top.h
#include <Dep/DepPrivate.h>
//--- e/F/Dep.framework/Modules/module.modulemap
framework module Dep { header "Dep.h" }
//--- e/F/Dep.framework/Modules/module.private.modulemap
framework module Dep_Private { header "DepPrivate.h" }
//--- e/F/Dep.framework/Headers/Dep.h
int dep(void);
//--- e/F/Dep.framework/PrivateHeaders/DepPrivate.h
int dep_private(void);
