#ifndef LLVM_ADT_ARRAYREFOFSTRINGREF_H
#define LLVM_ADT_ARRAYREFOFSTRINGREF_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"

#include <string>

namespace llvm {
class ArrayRefOfStringRef {
  enum class Kind {
    CharPtr,
    StringRef,
    StdString,
  };

  PointerIntPair<const void *, 2, Kind> DataAndKind = {nullptr, Kind::CharPtr};

  size_t Length = 0;

  template <class FnTy>
  auto visit(FnTy &&Fn) const {
    const void *Ptr = DataAndKind.getPointer();
    switch (DataAndKind.getInt()) {
    case Kind::CharPtr:
      return Fn(ArrayRef(static_cast<const char *const *>(Ptr), Length));
    case Kind::StringRef:
      return Fn(ArrayRef(static_cast<const StringRef *>(Ptr), Length));
    case Kind::StdString:
      return Fn(ArrayRef(static_cast<const std::string *>(Ptr), Length));
    }
  }

public:
  ArrayRefOfStringRef() = default;

  ArrayRefOfStringRef(int Argc, char *Argv[])
      : DataAndKind(Argv, Kind::CharPtr), Length(Argc) {}

  ArrayRefOfStringRef(int Argc, const char *Argv[])
      : DataAndKind(Argv, Kind::CharPtr), Length(Argc) {}

  ArrayRefOfStringRef(int Argc, const char *const *Argv)
      : DataAndKind(Argv, Kind::CharPtr), Length(Argc) {}

  ArrayRefOfStringRef(ArrayRef<const char *> Args)
      : DataAndKind(Args.data(), Kind::CharPtr), Length(Args.size()) {}

  ArrayRefOfStringRef(ArrayRef<StringRef> Args)
      : DataAndKind(Args.data(), Kind::StringRef), Length(Args.size()) {}

  ArrayRefOfStringRef(ArrayRef<std::string> Args)
      : DataAndKind(Args.data(), Kind::StdString), Length(Args.size()) {}

  ArrayRefOfStringRef(const std::vector<std::string> &Args)
      : DataAndKind(Args.data(), Kind::StdString), Length(Args.size()) {}

  ArrayRefOfStringRef(const SmallVectorImpl<const char *> &Args)
      : DataAndKind(Args.data(), Kind::CharPtr), Length(Args.size()) {}

  ArrayRefOfStringRef(const SmallVectorImpl<StringRef> &Args)
      : DataAndKind(Args.data(), Kind::StringRef), Length(Args.size()) {}

  size_t size() const { return Length; }

  bool empty() const { return size() == 0; }

  StringRef front() const {
    assert(!empty());
    return operator[](0);
  }

  StringRef back() const {
    assert(!empty());
    return operator[](size() - 1);
  }

  StringRef operator[](size_t Index) const {
    return visit([Index](auto Arr) { return StringRef(Arr[Index]); });
  }

  /// slice(n, m) - Chop off the first N elements of the array, and keep M
  /// elements in the array.
  ArrayRefOfStringRef slice(size_t N, size_t M) const {
    assert(N+M <= size() && "Invalid specifier");
    ArrayRefOfStringRef Slice = *this;
    const void *PointerPlusN = visit([N](auto Arr) -> const void * { return Arr.data() + N; });
    Slice.DataAndKind.setPointer(PointerPlusN);
    Slice.Length = M;
    return Slice;
  }

  /// slice(n) - Chop off the first N elements of the array.
  ArrayRefOfStringRef slice(size_t N) const { return drop_front(N); }

  /// Drop the first \p N elements of the array.
  ArrayRefOfStringRef drop_front(size_t N = 1) const {
    assert(size() >= N && "Dropping more elements than exist");
    return slice(N, size() - N);
  }

  /// Drop the last \p N elements of the array.
  ArrayRefOfStringRef drop_back(size_t N = 1) const {
    assert(size() >= N && "Dropping more elements than exist");
    return slice(0, size() - N);
  }

  class iterator
      : public iterator_facade_base<iterator, std::random_access_iterator_tag,
                                    StringRef, std::ptrdiff_t, StringRef,
                                    StringRef> {
    const ArrayRefOfStringRef *Array;
    size_t Index;

    friend ArrayRefOfStringRef;
    iterator(const ArrayRefOfStringRef *Array, size_t Index)
        : Array(Array), Index(Index) {}

  public:
    iterator(const iterator &RHS) : Array(RHS.Array), Index(RHS.Index) {}

    iterator &operator=(const iterator &RHS) = default;

    bool operator==(const iterator &RHS) const {
      assert(Array == RHS.Array);
      return Index == RHS.Index;
    }

    StringRef operator*() const { return (*Array)[Index]; }

    bool operator<(const iterator &RHS) const {
      assert(Array == RHS.Array);
      return Index < RHS.Index;
    }

    std::ptrdiff_t operator-(const iterator &RHS) const {
      assert(Array == RHS.Array);
      return Index - RHS.Index;
    }

    iterator &operator+=(std::ptrdiff_t N) {
      Index += N;
      return *this;
    }

    iterator &operator-=(std::ptrdiff_t N) {
      Index -= N;
      return *this;
    }
  };

  iterator begin() const { return {this, 0}; }
  iterator end() const { return {this, size()}; }

  using reverse_iterator = std::reverse_iterator<iterator>;

  reverse_iterator rbegin() const { return reverse_iterator(end()); }
  reverse_iterator rend() const { return reverse_iterator(begin()); }
};
} // namespace llvm

#endif
