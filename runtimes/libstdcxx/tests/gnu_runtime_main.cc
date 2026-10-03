#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <limits>
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
  void *half_symbol = dlvsym(RTLD_DEFAULT, "__extendhfdf2", "GCC_12.0.0");
  if (!loaded_from(half_symbol, library))
    return false;
  auto extend_half = reinterpret_cast<double (*)(_Float16)>(half_symbol);
  // Decode binary16 independently: casting the input could call this same
  // builtin.
  for (unsigned bits = 0; bits < 65536; ++bits) {
    const std::uint16_t representation = bits;
    _Float16 input;
    static_assert(sizeof(input) == sizeof(representation));
    std::memcpy(&input, &representation, sizeof(input));
    const double actual = extend_half(input);
    const unsigned exponent = (bits >> 10) & 31;
    const unsigned fraction = bits & 1023;
    if (exponent == 31 && fraction != 0) {
      if (!std::isnan(actual))
        return false;
      continue;
    }
    double expected =
        exponent == 31
            ? std::numeric_limits<double>::infinity()
            : std::ldexp(exponent ? 1024 + fraction : fraction,
                         exponent ? static_cast<int>(exponent) - 25 : -24);
    if (bits & 0x8000)
      expected = -expected;
    if (actual != expected || std::signbit(actual) != std::signbit(expected))
      return false;
  }
  for (const char *name :
       {"__divdc3", "__divsc3", "__muldc3", "__powidf2", "__powisf2"}) {
    if (!loaded_from(dlvsym(RTLD_DEFAULT, name, "GCC_4.0.0"), library))
      return false;
  }
  if (!loaded_from(dlvsym(RTLD_DEFAULT, "__floatuntidf", "GCC_4.2.0"), library))
    return false;
  for (const char *name :
       {"__addtf3", "__divtf3", "__eqtf2", "__fixtfsi", "__floatditf",
        "__floatsitf", "__floatunditf", "__getf2", "__gttf2", "__letf2",
        "__lttf2", "__multf3", "__netf2", "__subtf3", "__unordtf2"}) {
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

  using DoubleComplexOp =
      __complex__ double (*)(double, double, double, double);
  using FloatComplexOp = __complex__ float (*)(float, float, float, float);
  auto complex_multiply = reinterpret_cast<DoubleComplexOp>(
      dlvsym(RTLD_DEFAULT, "__muldc3", "GCC_4.0.0"));
  auto complex_divide = reinterpret_cast<DoubleComplexOp>(
      dlvsym(RTLD_DEFAULT, "__divdc3", "GCC_4.0.0"));
  auto float_complex_divide = reinterpret_cast<FloatComplexOp>(
      dlvsym(RTLD_DEFAULT, "__divsc3", "GCC_4.0.0"));
  auto product = complex_multiply(1, 2, 3, 4);
  auto quotient = complex_divide(4, 2, 1, 1);
  auto float_quotient = float_complex_divide(4, 2, 1, 1);
  if (__real__ product != -5 || __imag__ product != 10 ||
      __real__ quotient != 3 || __imag__ quotient != -1 ||
      __real__ float_quotient != 3 || __imag__ float_quotient != -1)
    return false;
  auto double_power = reinterpret_cast<double (*)(double, int)>(
      dlvsym(RTLD_DEFAULT, "__powidf2", "GCC_4.0.0"));
  auto float_power = reinterpret_cast<float (*)(float, int)>(
      dlvsym(RTLD_DEFAULT, "__powisf2", "GCC_4.0.0"));
  if (double_power(2, 10) != 1024 || double_power(2, -10) != 0x1p-10 ||
      float_power(-2, 3) != -8 || float_power(2, -10) != 0x1p-10f)
    return false;
  auto quad_to_int = reinterpret_cast<int (*)(__float128)>(
      dlvsym(RTLD_DEFAULT, "__fixtfsi", "GCC_4.3.0"));
  auto unsigned_to_double = reinterpret_cast<double (*)(unsigned __int128)>(
      dlvsym(RTLD_DEFAULT, "__floatuntidf", "GCC_4.2.0"));
  if (quad_to_int(3.75) != 3 || quad_to_int(-3.75) != -3 ||
      unsigned_to_double(static_cast<unsigned __int128>(1) << 100) != 0x1p100 ||
      unsigned_to_double((static_cast<unsigned __int128>(1) << 53) + 1) !=
          0x1p53 ||
      unsigned_to_double((static_cast<unsigned __int128>(1) << 53) + 3) !=
          0x1.0000000000002p53)
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
