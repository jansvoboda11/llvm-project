//===--- DataBufferLLVM.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_DATABUFFERLLVM_H
#define LLDB_UTILITY_DATABUFFERLLVM_H

#include "lldb/Utility/DataBuffer.h"
#include "lldb/lldb-types.h"

#include <cstdint>
#include <llvm/Support/ExtensibleRTTI.h>
#include <memory>

namespace llvm {
class WritableMemoryBuffer;
class MemoryBuffer;
class Twine;
} // namespace llvm

namespace lldb_private {
class FileSystem;

class DataBufferLLVM : public llvm::RTTIExtends<DataBufferLLVM, DataBuffer> {
public:
  ~DataBufferLLVM() override;

  const uint8_t *GetBytesImpl() const override;
  lldb::offset_t GetByteSize() const override;

  LLVM_EXTENSIBLE_RTTI_DEFINE_ID(lldb_private::DataBufferLLVM);

  /// Construct a DataBufferLLVM from \p Buffer.  \p Buffer must be a valid
  /// pointer.
  explicit DataBufferLLVM(std::unique_ptr<llvm::MemoryBuffer> Buffer);

protected:
  std::unique_ptr<llvm::MemoryBuffer> Buffer;
};

class WritableDataBufferLLVM
    : public llvm::RTTIExtends<WritableDataBufferLLVM, WritableDataBuffer> {
public:
  ~WritableDataBufferLLVM() override;

  const uint8_t *GetBytesImpl() const override;
  lldb::offset_t GetByteSize() const override;

  LLVM_EXTENSIBLE_RTTI_DEFINE_ID(lldb_private::WritableDataBufferLLVM);

  /// Construct a DataBufferLLVM from \p Buffer.  \p Buffer must be a valid
  /// pointer.
  explicit WritableDataBufferLLVM(
      std::unique_ptr<llvm::WritableMemoryBuffer> Buffer);

protected:
  std::unique_ptr<llvm::WritableMemoryBuffer> Buffer;
};
} // namespace lldb_private

#endif
