// Test that visibility propagates through exports and conflicts of submodules
// that haven't been deserialized yet in the same way as through deserialized
// ones, and that it doesn't deserialize them unnecessarily.

// RUN: rm -rf %t
// RUN: split-file %s %t

// DEFINE: %{extra} =
// DEFINE: %{out} = %t/default
// DEFINE: %{cc1} = %clang_cc1 -fmodules -fno-implicit-modules -x objective-c \
// DEFINE:   %{extra}
// DEFINE: %{pcms} = -fmodule-file=Top=%{out}/Top.pcm \
// DEFINE:   -fmodule-file=Lazy=%{out}/Lazy.pcm \
// DEFINE:   -fmodule-file=Other=%{out}/Other.pcm \
// DEFINE:   -fmodule-file=Other2=%{out}/Other2.pcm
// DEFINE: %{build} = mkdir %{out} \
// DEFINE:   && %{cc1} -I%t -emit-module -fmodule-name=Other \
// DEFINE:        %t/module.modulemap -o %{out}/Other.pcm \
// DEFINE:   && %{cc1} -I%t -emit-module -fmodule-name=Other2 \
// DEFINE:        %t/module.modulemap -o %{out}/Other2.pcm \
// DEFINE:   && %{cc1} -I%t -emit-module -fmodule-name=Lazy \
// DEFINE:        -fmodule-file=Other=%{out}/Other.pcm \
// DEFINE:        -fmodule-file=Other2=%{out}/Other2.pcm \
// DEFINE:        %t/module.modulemap -o %{out}/Lazy.pcm \
// DEFINE:   && %{cc1} -I%t -emit-module -fmodule-name=Top \
// DEFINE:        -fmodule-file=Lazy=%{out}/Lazy.pcm \
// DEFINE:        %t/module.modulemap -o %{out}/Top.pcm

// RUN: %{build}
// Without the module map, the submodules of Lazy are deserialized lazily.
// RUN: %{cc1} %{pcms} -fsyntax-only %t/tu.m -verify
// With the module map, the submodules of Lazy are deserialized eagerly.
// RUN: %{cc1} %{pcms} -fmodule-map-file=%t/module.modulemap \
// RUN:   -fsyntax-only %t/tu.m -verify
// Making modules visible doesn't deserialize the modules it propagates to.
// RUN: %{cc1} %{pcms} -fsyntax-only %t/tu-stats.m -print-stats 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LAZY
// RUN: %{cc1} %{pcms} -fmodule-map-file=%t/module.modulemap \
// RUN:   -fsyntax-only %t/tu-stats.m -print-stats 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EAGER

// REDEFINE: %{extra} = -fmodules-local-submodule-visibility
// REDEFINE: %{out} = %t/lsv
// RUN: %{build}
// Without the module map, the submodules of Lazy are deserialized lazily.
// RUN: %{cc1} %{pcms} -fsyntax-only %t/tu.m -verify
// With the module map, the submodules of Lazy are deserialized eagerly.
// RUN: %{cc1} %{pcms} -fmodule-map-file=%t/module.modulemap \
// RUN:   -fsyntax-only %t/tu.m -verify
// Making modules visible doesn't deserialize the modules it propagates to.
// RUN: %{cc1} %{pcms} -fsyntax-only %t/tu-stats.m -print-stats 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LAZY
// RUN: %{cc1} %{pcms} -fmodule-map-file=%t/module.modulemap \
// RUN:   -fsyntax-only %t/tu-stats.m -print-stats 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EAGER

// Lazily, we only read the top-level modules, the imported submodules and
// Lazy.Req, since it has requirements that need to be checked.
// LAZY: 10/22 submodules read
// EAGER: 22/22 submodules read

//--- module.modulemap
module Other { header "Other.h" }
module Other2 { header "Other2.h" }
module Unrelated { header "Unrelated.h" }
// The requirement of the top-level module is checked once, its submodules don't
// need to be deserialized to check it.
module Lazy {
  requires objc
  // Non-explicit submodules are exported by their parent.
  module A { header "A.h" }
  // Export of a sibling.
  explicit module B { header "B.h" export C }
  explicit module C { header "C.h" }
  // Export of a module from another module file.
  explicit module D { header "D.h" export Other }
  // Wildcard export of a module from another module file.
  explicit module E { header "E.h" export * }
  // Restricted wildcard export of a module from another module file.
  explicit module V { header "V.h" export Other.* }
  // Restricted wildcard export of a module that's not imported.
  explicit module W { header "W.h" export Unrelated.* }
  // Export of an unimportable module.
  explicit module F { header "F.h" export Req }
  explicit module Req { requires cplusplus header "Req.h" }
  // Conflicts.
  explicit module X { header "X.h" conflict Lazy.Y, "X vs Y" }
  explicit module Y { header "Y.h" }
  explicit module Z { header "Z.h" export X }
  explicit module K { header "K.h" conflict Other, "K vs Other" }
  explicit module KK { header "KK.h" export K }
  // Transitive exports.
  explicit module G { header "G.h" export B }
  // Non-exported submodules.
  explicit module H { header "H.h" export Unused1 }
  explicit module Unused1 { header "Unused1.h" }
  explicit module Unused2 { header "Unused2.h" }
}
module Top { header "Top.h" export * }

//--- Other.h
#define OTHER_MACRO
extern int other_val;
//--- Other2.h
#define OTHER2_MACRO
extern int other2_val;
//--- A.h
#define A_MACRO
extern int a_val;
//--- B.h
#define B_MACRO
extern int b_val;
//--- C.h
#define C_MACRO
extern int c_val;
//--- D.h
#include "Other.h"
#define D_MACRO
extern int d_val;
//--- E.h
#include "Other2.h"
#define E_MACRO
extern int e_val;
//--- Unrelated.h
#define UNRELATED_MACRO
//--- V.h
#include "Other.h"
#define V_MACRO
//--- W.h
#include "Other2.h"
#define W_MACRO
//--- K.h
#define K_MACRO
//--- KK.h
#define KK_MACRO
//--- F.h
#define F_MACRO
extern int f_val;
//--- Req.h
#define REQ_MACRO
extern int req_val;
//--- X.h
#define X_MACRO
extern int x_val;
//--- Y.h
#define Y_MACRO
extern int y_val;
//--- Z.h
#define Z_MACRO
extern int z_val;
//--- G.h
#define G_MACRO
extern int g_val;
//--- H.h
#define H_MACRO
extern int h_val;
//--- Unused1.h
#define UNUSED1_MACRO
extern int unused1_val;
//--- Unused2.h
#define UNUSED2_MACRO
extern int unused2_val;
//--- Top.h
#include "A.h"
#define TOP_MACRO
extern int top_val;

//--- tu.m
@import Top;
#if !defined(TOP_MACRO) || !defined(A_MACRO)
#error TOP_MACRO and A_MACRO should be visible
#endif
int *top[] = {&top_val, &a_val};

@import Lazy.G;
#if !defined(G_MACRO) || !defined(B_MACRO) || !defined(C_MACRO)
#error G_MACRO, B_MACRO and C_MACRO should be visible
#endif
int *g[] = {&g_val, &b_val, &c_val};

#ifdef D_MACRO
#error D_MACRO should not be visible yet
#endif
@import Lazy.W;
#if !defined(W_MACRO) || defined(OTHER2_MACRO)
#error W_MACRO should be visible, OTHER2_MACRO should not
#endif
@import Lazy.V;
#if !defined(V_MACRO) || !defined(OTHER_MACRO)
#error V_MACRO and OTHER_MACRO should be visible
#endif
@import Lazy.D;
#if !defined(D_MACRO) || !defined(OTHER_MACRO)
#error D_MACRO and OTHER_MACRO should be visible
#endif
int *d[] = {&d_val, &other_val};

@import Lazy.E;
#if !defined(E_MACRO) || !defined(OTHER2_MACRO)
#error E_MACRO and OTHER2_MACRO should be visible
#endif
int *e[] = {&e_val, &other2_val};

@import Lazy.F;
#if !defined(F_MACRO) || defined(REQ_MACRO)
#error F_MACRO should be visible, REQ_MACRO should not
#endif
int *f[] = {&f_val};

@import Lazy.Y;
@import Lazy.Z; // expected-warning{{module 'Lazy.X' conflicts with already-imported module 'Lazy.Y': X vs Y}}
#if !defined(X_MACRO) || !defined(Y_MACRO) || !defined(Z_MACRO)
#error X_MACRO, Y_MACRO and Z_MACRO should be visible
#endif
int *xyz[] = {&x_val, &y_val, &z_val};

@import Lazy.KK; // expected-warning{{module 'Lazy.K' conflicts with already-imported module 'Other': K vs Other}}
#if !defined(K_MACRO) || !defined(KK_MACRO)
#error K_MACRO and KK_MACRO should be visible
#endif

@import Lazy.H;
#if !defined(H_MACRO) || !defined(UNUSED1_MACRO) || defined(UNUSED2_MACRO)
#error H_MACRO and UNUSED1_MACRO should be visible, UNUSED2_MACRO should not
#endif
int *h[] = {&h_val, &unused1_val};
int *unused2[] = {&unused2_val}; // expected-error{{declaration of 'unused2_val' must be imported from module 'Lazy.Unused2' before it is required}}
                                 // expected-note@Unused2.h:2{{declaration here is not visible}}

//--- tu-stats.m
@import Top;
@import Lazy.G;
@import Lazy.D;
@import Lazy.E;
@import Lazy.F;
@import Lazy.H;
