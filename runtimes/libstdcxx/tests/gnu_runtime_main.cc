#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unwind.h>

extern "C" void throw_from_shared_library(int *cleaned_up);
extern "C" void call_through_c_cleanup(void (*callback)(int *), int *c_cleanup,
                                       int *cpp_cleanup);
int project_value();

// Check actual loaded paths, not just DT_NEEDED. A system runtime can otherwise
// hide a missing runfile or an incompatible adapter.
bool loaded_from(const void *symbol, const char *expected) {
  Dl_info info{};
  if (!expected || !dladdr(symbol, &info) || !info.dli_fname)
    return false;
  char *actual_path = realpath(info.dli_fname, nullptr);
  char *expected_path = realpath(expected, nullptr);
  const bool matches = actual_path && expected_path &&
                       std::strcmp(actual_path, expected_path) == 0;
  if (!matches)
    std::cerr << "Unexpected runtime: " << info.dli_fname << '\n';
  std::free(actual_path);
  std::free(expected_path);
  return matches;
}

#if defined(__x86_64__)
bool check_numeric_runtime() {
  const char *library = std::getenv("LIBGCC_S");
  for (const char *name :
       {"__addtf3", "__divtf3", "__eqtf2", "__floatditf", "__floatsitf",
        "__floatunditf", "__getf2", "__gttf2", "__letf2", "__lttf2", "__multf3",
        "__netf2", "__subtf3", "__unordtf2"}) {
    if (!loaded_from(dlvsym(RTLD_DEFAULT, name, "GCC_4.3.0"), library))
      return false;
  }
  for (const char *name : {"__divti3", "__modti3", "__udivti3", "__umodti3"}) {
    if (!loaded_from(dlvsym(RTLD_DEFAULT, name, "GCC_3.0"), library))
      return false;
  }
  void *model = dlvsym(RTLD_DEFAULT, "__cpu_model", "GCC_4.8.0");
  void *init = dlvsym(RTLD_DEFAULT, "__cpu_indicator_init", "GCC_4.8.0");
  if (!loaded_from(model, library) || !loaded_from(init, library))
    return false;
  if (reinterpret_cast<int (*)()>(init)() != 0 ||
      *static_cast<unsigned *>(model) == 0)
    return false;

  using SignedOp = __int128 (*)(__int128, __int128);
  using UnsignedOp =
      unsigned __int128 (*)(unsigned __int128, unsigned __int128);
  auto divide =
      reinterpret_cast<SignedOp>(dlvsym(RTLD_DEFAULT, "__divti3", "GCC_3.0"));
  auto remainder =
      reinterpret_cast<SignedOp>(dlvsym(RTLD_DEFAULT, "__modti3", "GCC_3.0"));
  auto unsigned_remainder = reinterpret_cast<UnsignedOp>(
      dlvsym(RTLD_DEFAULT, "__umodti3", "GCC_3.0"));
  constexpr __int128 magnitude = (static_cast<__int128>(1) << 100) + 21;
  if (divide(-magnitude, 7) != -magnitude / 7 ||
      remainder(-magnitude, 7) != -magnitude % 7 ||
      unsigned_remainder(magnitude, 7) != magnitude % 7)
    return false;

  using QuadOp = __float128 (*)(__float128, __float128);
  auto add =
      reinterpret_cast<QuadOp>(dlvsym(RTLD_DEFAULT, "__addtf3", "GCC_4.3.0"));
  auto multiply =
      reinterpret_cast<QuadOp>(dlvsym(RTLD_DEFAULT, "__multf3", "GCC_4.3.0"));
  return add(2, 3) == 5 && multiply(2, 3) == 6;
}
#endif

int main() {
#if defined(__x86_64__)
  if (!check_numeric_runtime())
    return 7;
#endif
  if (project_value() != 42)
    return 1;
  if (!loaded_from(dlsym(RTLD_DEFAULT, "_Unwind_RaiseException"),
                   std::getenv("LIBGCC_S")))
    return 2;
  if (!loaded_from(dlsym(RTLD_DEFAULT, "__cxa_throw"),
                   std::getenv("LIBSTDCXX")))
    return 3;
  if (!loaded_from(dlvsym(RTLD_DEFAULT, "__gcc_personality_v0", "GCC_3.3.1"),
                   std::getenv("LIBGCC_S")))
    return 6;
  int cleaned_up = 0;
  int c_cleaned_up = 0;
  try {
    call_through_c_cleanup(throw_from_shared_library, &c_cleaned_up,
                           &cleaned_up);
  } catch (const std::runtime_error &error) {
    if (cleaned_up != 1 || c_cleaned_up != 1 ||
        std::string(error.what()) != "exception from shared library")
      return 4;
    std::cout << "GNU runtime exception boundary passed\n";
    return 0;
  }
  return 5;
}
