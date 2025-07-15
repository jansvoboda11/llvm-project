#include "clang/Basic/DebugOptions.h"

namespace clang {
DebugOptions::DebugOptions() {
#define DEBUGOPT(Name, Bits, Default, Compatibility) Name = Default;
#define ENUM_DEBUGOPT(Name, Type, Bits, Default, Compatibility)                \
  set##Name(Default);
#include "clang/Basic/DebugOptions.def"
}

void DebugOptions::resetNonModularOptions(StringRef ModuleFormat) {
  // FIXME: Replace with C++20 `using enum CodeGenOptions::CompatibilityKind`.
  using CK = CompatibilityKind;

  // First reset benign options.
#define DEBUGOPT(Name, Bits, Default, Compatibility)                           \
  if constexpr (CK::Compatibility == CK::Benign)                               \
    Name = Default;
#define ENUM_DEBUGOPT(Name, Type, Bits, Default, Compatibility)                \
  if constexpr (CK::Compatibility == CK::Benign)                               \
    set##Name(Default);
#include "clang/Basic/DebugOptions.def"

  // Conditionally reset options that only matter when the debug info is emitted
  // into the PCM (-gmodules).
  if (ModuleFormat == "raw" && !DebugTypeExtRefs) {
#define DEBUGOPT(Name, Bits, Default, Compatibility)                           \
  if constexpr (CK::Compatibility != CK::Benign)                               \
    Name = Default;
#define ENUM_DEBUGOPT(Name, Type, Bits, Default, Compatibility)                \
  if constexpr (CK::Compatibility == CK::Benign)                               \
    set##Name(Default);
#include "clang/Basic/DebugOptions.def"
  }
}

} // namespace clang
