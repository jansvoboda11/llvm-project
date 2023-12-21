//===------ unittests/ExtensibleRTTITest.cpp - Extensible RTTI Tests ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace llvm;

namespace {

class MyBaseType : public RTTIExtends<MyBaseType, RTTIRoot> {
public:
  LLVM_EXTENSIBLE_RTTI_DEFINE_ID(MyBaseType);
};

class MyDerivedType : public RTTIExtends<MyDerivedType, MyBaseType> {
public:
  LLVM_EXTENSIBLE_RTTI_DEFINE_ID(MyDerivedType);
};

class MyOtherDerivedType : public RTTIExtends<MyOtherDerivedType, MyBaseType> {
public:
  LLVM_EXTENSIBLE_RTTI_DEFINE_ID(MyOtherDerivedType);
};

class MyDeeperDerivedType
    : public RTTIExtends<MyDeeperDerivedType, MyDerivedType> {
public:
  LLVM_EXTENSIBLE_RTTI_DEFINE_ID(MyDeeperDerivedType);
};

TEST(ExtensibleRTTI, isa) {
  MyBaseType B;
  MyDerivedType D;
  MyDeeperDerivedType DD;

  EXPECT_TRUE(isa<MyBaseType>(B));
  EXPECT_FALSE(isa<MyDerivedType>(B));
  EXPECT_FALSE(isa<MyOtherDerivedType>(B));
  EXPECT_FALSE(isa<MyDeeperDerivedType>(B));

  EXPECT_TRUE(isa<MyBaseType>(D));
  EXPECT_TRUE(isa<MyDerivedType>(D));
  EXPECT_FALSE(isa<MyOtherDerivedType>(D));
  EXPECT_FALSE(isa<MyDeeperDerivedType>(D));

  EXPECT_TRUE(isa<MyBaseType>(DD));
  EXPECT_TRUE(isa<MyDerivedType>(DD));
  EXPECT_FALSE(isa<MyOtherDerivedType>(DD));
  EXPECT_TRUE(isa<MyDeeperDerivedType>(DD));
}

TEST(ExtensibleRTTI, cast) {
  MyDerivedType D;
  MyBaseType &BD = D;

  (void)cast<MyBaseType>(D);
  (void)cast<MyBaseType>(BD);
  (void)cast<MyDerivedType>(BD);
}

TEST(ExtensibleRTTI, dyn_cast) {
  MyBaseType B;
  MyDerivedType D;
  MyBaseType &BD = D;

  EXPECT_EQ(dyn_cast<MyDerivedType>(&B), nullptr);
  EXPECT_EQ(dyn_cast<MyDerivedType>(&D), &D);
  EXPECT_EQ(dyn_cast<MyBaseType>(&BD), &BD);
  EXPECT_EQ(dyn_cast<MyDerivedType>(&BD), &D);
}

} // namespace
