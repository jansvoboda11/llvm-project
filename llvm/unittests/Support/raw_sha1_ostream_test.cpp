//===- llvm/unittest/Support/raw_ostream_test.cpp - raw_ostream tests -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_sha1_ostream.h"
#include "gtest/gtest.h"

#include <string>

using namespace llvm;

TEST(raw_sha1_ostreamTest, Basic) {
  llvm::raw_sha1_ostream Sha1Stream;
  Sha1Stream << "Hello World!";
  auto Hash = toHex(Sha1Stream.sha1());

  ASSERT_EQ("2EF7BDE608CE5404E97D5F042F95F89F1C232871", Hash);
}

TEST(sha1_hash_test, Basic) {
  ArrayRef<uint8_t> Input((const uint8_t *)"Hello World!", 12);
  std::array<uint8_t, 20> Vec = SHA1::hash(Input);
  std::string Hash = toHex(Vec);
  ASSERT_EQ("2EF7BDE608CE5404E97D5F042F95F89F1C232871", Hash);
}

TEST(sha1_hash_test, Update) {
  SHA1 sha1;
  std::string Input = "123456789012345678901234567890";
  ASSERT_EQ(Input.size(), 30UL);
  // 3 short updates.
  sha1.update(Input);
  sha1.update(Input);
  sha1.update(Input);
  // Long update that gets into the optimized loop with prefix/suffix.
  sha1.update(Input + Input + Input + Input);
  // 18 bytes buffered now.

  std::string Hash = toHex(sha1.final());
  ASSERT_EQ("3E4A614101AD84985AB0FE54DC12A6D71551E5AE", Hash);
}

// FIPS 180-2 test vectors.
TEST(sha1_hash_test, KnownAnswers) {
  EXPECT_EQ("DA39A3EE5E6B4B0D3255BFEF95601890AFD80709",
            toHex(SHA1::hash(arrayRefFromStringRef(""))));
  EXPECT_EQ("A9993E364706816ABA3E25717850C26C9CD0D89D",
            toHex(SHA1::hash(arrayRefFromStringRef("abc"))));
  EXPECT_EQ("84983E441C3BD26EBAAE4AA1F95129E5E54670F1",
            toHex(SHA1::hash(arrayRefFromStringRef(
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))));
  std::string MillionA(1000000, 'a');
  EXPECT_EQ("34AA973CD4C4DAA4F61EEB2BDBAD27316534016F",
            toHex(SHA1::hash(arrayRefFromStringRef(MillionA))));
}

// Check that the hash doesn't depend on how the input is split into updates,
// covering both the buffered path and the multi-block path of update().
TEST(sha1_hash_test, SplitUpdates) {
  std::string Input;
  for (unsigned I = 0; I < 1000; ++I)
    Input.push_back(static_cast<char>(I * 7 + I / 13));
  for (size_t Size : {0, 1, 55, 56, 63, 64, 65, 127, 128, 129, 640, 1000}) {
    StringRef Data = StringRef(Input).take_front(Size);
    std::array<uint8_t, 20> Expected = SHA1::hash(arrayRefFromStringRef(Data));
    for (size_t Chunk : {1, 3, 64, 100, 200}) {
      SHA1 Hasher;
      for (size_t Pos = 0; Pos < Data.size(); Pos += Chunk)
        Hasher.update(Data.substr(Pos, Chunk));
      EXPECT_EQ(Expected, Hasher.final()) << Size << " " << Chunk;
    }
  }
}

#if defined(EXPENSIVE_CHECKS)
TEST(sha1_hash_test, LargeInput) {
  SHA1 sha1;
  std::string Input(1 << 20, '\0');
  for (int I = 0; I < 4096; ++I)
    sha1.update(Input);

  std::string Hash = toHex(sha1.final());
  ASSERT_EQ("1BF99EE9F374E58E201E4DDA4F474E570EB77229", Hash);
}
#endif

// Check that getting the intermediate hash in the middle of the stream does
// not invalidate the final result.
TEST(raw_sha1_ostreamTest, Intermediate) {
  llvm::raw_sha1_ostream Sha1Stream;
  Sha1Stream << "Hello";
  auto Hash = toHex(Sha1Stream.sha1());

  ASSERT_EQ("F7FF9E8B7BB2E09B70935A5D785E0CC5D9D0ABF0", Hash);
  Sha1Stream << " World!";
  Hash = toHex(Sha1Stream.sha1());

  // Compute the non-split hash separately as a reference.
  llvm::raw_sha1_ostream NonSplitSha1Stream;
  NonSplitSha1Stream << "Hello World!";
  auto NonSplitHash = toHex(NonSplitSha1Stream.sha1());

  ASSERT_EQ(NonSplitHash, Hash);
}

TEST(raw_sha1_ostreamTest, Reset) {
  llvm::raw_sha1_ostream Sha1Stream;
  Sha1Stream << "Hello";
  auto Hash = toHex(Sha1Stream.sha1());

  ASSERT_EQ("F7FF9E8B7BB2E09B70935A5D785E0CC5D9D0ABF0", Hash);

  Sha1Stream.resetHash();
  Sha1Stream << " World!";
  Hash = toHex(Sha1Stream.sha1());

  ASSERT_EQ("7447F2A5A42185C8CF91E632789C431830B59067", Hash);
}
