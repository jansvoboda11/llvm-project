#include "llvm/Support/raw_ostream.h"

#include "llvm/ADT/ArrayRefOfStringRef.h"

#include "gtest/gtest.h"

using namespace llvm;

namespace {
TEST(ArrayRefOfStringRef, ConstructionC) {
  const char *Argv[]{"one", "two", "three"};

  ArrayRefOfStringRef Array(Argv);

  EXPECT_EQ(Array.size(), 3u);
  EXPECT_EQ(Array[0], "one");
  EXPECT_EQ(Array[1], "two");
  EXPECT_EQ(Array[2], "three");
}

TEST(ArrayRefOfStringRef, ConstructionLLVM) {
  SmallVector<StringRef> Args{"one", "two", "three"};

  ArrayRefOfStringRef Array(Args);

  EXPECT_EQ(Array.size(), 3u);
  EXPECT_EQ(Array[0], "one");
  EXPECT_EQ(Array[1], "two");
  EXPECT_EQ(Array[2], "three");
}

TEST(ArrayRefOfStringRef, ConstructionStd) {
  std::vector<std::string> Args{"one", "two", "three"};

  ArrayRefOfStringRef Array(Args);

  EXPECT_FALSE(Array.empty());
  EXPECT_EQ(Array.size(), 3u);
  EXPECT_EQ(Array[0], "one");
  EXPECT_EQ(Array[1], "two");
  EXPECT_EQ(Array[2], "three");
  EXPECT_EQ(Array.front(), "one");
  EXPECT_EQ(Array.back(), "three");

  EXPECT_EQ(std::distance(Array.begin(), Array.end()), 3);

  auto It = Array.begin();
  EXPECT_EQ(*It++, "one");
  EXPECT_EQ(*It++, "two");
  EXPECT_EQ(*It++, "three");
  EXPECT_EQ(It, Array.end());

  auto RIt = Array.rbegin();
  EXPECT_EQ(*RIt++, "three");
  EXPECT_EQ(*RIt++, "two");
  EXPECT_EQ(*RIt++, "one");
  EXPECT_EQ(RIt, Array.rend());
}
} // namespace
