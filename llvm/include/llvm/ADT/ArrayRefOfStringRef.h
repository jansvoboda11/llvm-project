#ifndef LLVM_ADT_ARRAYREFOFSTRINGREF_H
#define LLVM_ADT_ARRAYREFOFSTRINGREF_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"

#include <initializer_list>
#include <string>

namespace llvm {
// namespace detail {
// template <class T, class U> struct Like : std::false_type {};
// template <> struct Like<StringRef, const char *> : std::true_type {};
// template <> struct Like<StringRef, std::string> : std::true_type {};
// template <> struct Like<StringRef, std::string_view> : std::true_type {};
//
// template <class T, class U> struct Like<ArrayRef<T>, U> {
//   static constexpr bool value = Like<T, decltype(std::declval<const U &>().data());
// };
// } // namespace detail

class ArrayRefOfStringRef {
  enum class Kind {
    CharPtr,
    StringRef,
    StdString,
  };

  PointerIntPair<const void *, 2, Kind> DataAndKind = {nullptr, Kind::CharPtr};

  size_t Length = 0;

  template <class FnTy> auto visit(FnTy &&Fn) const {
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

  // template <class T>
  // static constexpr bool StringRefLike =
  //     std::is_same_v<T, const char *> || std::is_same_v<T, std::string> ||
  //     std::is_same_v<T, std::string_view>;
  //
  // template <class C, class T>
  // static constexpr bool ArrayRefLike =
  //   std::is_invocable_v<decltype(std::declval<const C &>().data())>

public:
  ArrayRefOfStringRef() = default;

  template <size_t Argc>
  ArrayRefOfStringRef(const char *const (&Argv)[Argc])
      : DataAndKind(Argv, Kind::CharPtr), Length(Argc) {}

  ArrayRefOfStringRef(int Argc, const char *const Argv[])
      : DataAndKind(Argv, Kind::CharPtr), Length(Argc) {}

  ArrayRefOfStringRef(ArrayRef<const char *> Args)
      : DataAndKind(Args.data(), Kind::CharPtr), Length(Args.size()) {}

  ArrayRefOfStringRef(ArrayRef<StringRef> Args)
      : DataAndKind(Args.data(), Kind::StringRef), Length(Args.size()) {}

  ArrayRefOfStringRef(ArrayRef<std::string> Args)
      : DataAndKind(Args.data(), Kind::StdString), Length(Args.size()) {}

  ArrayRefOfStringRef(const std::vector<const char *> &Args)
      : DataAndKind(Args.data(), Kind::CharPtr), Length(Args.size()) {}

  ArrayRefOfStringRef(const std::vector<std::string> &Args)
      : DataAndKind(Args.data(), Kind::StdString), Length(Args.size()) {}

  ArrayRefOfStringRef(const SmallVectorImpl<const char *> &Args)
      : DataAndKind(Args.data(), Kind::CharPtr), Length(Args.size()) {}

  ArrayRefOfStringRef(const SmallVectorImpl<StringRef> &Args)
      : DataAndKind(Args.data(), Kind::StringRef), Length(Args.size()) {}

  ArrayRefOfStringRef(std::initializer_list<const char *> Args)
      : DataAndKind(Args.begin(), Kind::CharPtr), Length(Args.size()) {}

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
    assert(N + M <= size() && "Invalid specifier");
    const void *NewPtr =
        visit([N](auto Arr) -> const void * { return Arr.data() + N; });
    ArrayRefOfStringRef Slice;
    Slice.DataAndKind = {NewPtr, DataAndKind.getInt()};
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
    PointerIntPair<const void *, 2, Kind> DataAndKind = {nullptr,
                                                         Kind::CharPtr};

    template <class FnTy>
    static auto visit(FnTy &&Fn, const iterator &LHS, const iterator &RHS) {
      assert(LHS.DataAndKind.getInt() == RHS.DataAndKind.getInt());
      const void *LHSPtr = LHS.DataAndKind.getPointer();
      const void *RHSPtr = RHS.DataAndKind.getPointer();
      switch (LHS.DataAndKind.getInt()) {
      case Kind::CharPtr:
        return Fn(static_cast<const char *const *>(LHSPtr),
                  static_cast<const char *const *>(RHSPtr));
      case Kind::StringRef:
        return Fn(static_cast<const StringRef *>(LHSPtr),
                  static_cast<const StringRef *>(RHSPtr));
      case Kind::StdString:
        return Fn(static_cast<const std::string *>(LHSPtr),
                  static_cast<const std::string *>(RHSPtr));
      }
    }

    template <class FnTy> auto visit(FnTy &&Fn) const {
      return visit([&Fn](const auto *Ptr, const auto *) { return Fn(Ptr); },
                   *this, *this);
    }

    friend ArrayRefOfStringRef;
    iterator(const ArrayRefOfStringRef *Array, size_t N)
        : DataAndKind(Array->DataAndKind) {
      const void *NewPtr =
          visit([N](const auto *Ptr) -> const void * { return Ptr + N; });
      DataAndKind.setPointer(NewPtr);
    }

  public:
    iterator() = default;

    iterator(const iterator &) = default;

    iterator &operator=(const iterator &) = default;

    bool operator==(const iterator &RHS) const {
      assert(DataAndKind.getInt() == RHS.DataAndKind.getInt());
      return DataAndKind.getPointer() == RHS.DataAndKind.getPointer();
    }

    bool operator<(const iterator &RHS) const {
      assert(DataAndKind.getInt() == RHS.DataAndKind.getInt());
      return DataAndKind.getPointer() < RHS.DataAndKind.getPointer();
    }

    StringRef operator*() const {
      return visit([](const auto *Ptr) { return StringRef(*Ptr); });
    }

    std::ptrdiff_t operator-(const iterator &RHS) const {
      return visit(
          [](const auto *Ptr, const auto *RHSPtr) { return Ptr - RHSPtr; },
          *this, RHS);
    }

    iterator &operator+=(std::ptrdiff_t N) {
      const void *NewPtr =
          visit([N](const auto *Ptr) -> const void * { return Ptr + N; });
      DataAndKind.setPointer(NewPtr);
      return *this;
    }

    iterator &operator-=(std::ptrdiff_t N) {
      const void *NewPtr =
          visit([N](const auto *Ptr) -> const void * { return Ptr - N; });
      DataAndKind.setPointer(NewPtr);
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
