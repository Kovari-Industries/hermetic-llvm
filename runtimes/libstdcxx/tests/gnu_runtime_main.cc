#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unwind.h>

extern "C" void throw_from_shared_library(int* cleaned_up);
extern "C" void call_through_c_cleanup(void (*callback)(int*), int* c_cleanup,
                                      int* cpp_cleanup);
int project_value();

// Check actual loaded paths, not just DT_NEEDED. A system runtime can otherwise
// hide a missing runfile or an incompatible adapter.
bool loaded_from(const void* symbol, const char* expected) {
  Dl_info info{};
  if (!expected || !dladdr(symbol, &info) || !info.dli_fname) return false;
  char* actual_path = realpath(info.dli_fname, nullptr);
  char* expected_path = realpath(expected, nullptr);
  const bool matches = actual_path && expected_path &&
                       std::strcmp(actual_path, expected_path) == 0;
  if (!matches) std::cerr << "Unexpected runtime: " << info.dli_fname << '\n';
  std::free(actual_path);
  std::free(expected_path);
  return matches;
}

int main() {
  if (project_value() != 42) return 1;
  if (!loaded_from(dlsym(RTLD_DEFAULT, "_Unwind_RaiseException"),
                   std::getenv("LIBGCC_S"))) return 2;
  if (!loaded_from(dlsym(RTLD_DEFAULT, "__cxa_throw"),
                   std::getenv("LIBSTDCXX"))) return 3;
  if (!loaded_from(dlvsym(RTLD_DEFAULT, "__gcc_personality_v0", "GCC_3.3.1"),
                   std::getenv("LIBGCC_S"))) return 6;
  int cleaned_up = 0;
  int c_cleaned_up = 0;
  try {
    call_through_c_cleanup(throw_from_shared_library, &c_cleaned_up, &cleaned_up);
  } catch (const std::runtime_error& error) {
    if (cleaned_up != 1 || c_cleaned_up != 1 ||
        std::string(error.what()) != "exception from shared library") return 4;
    std::cout << "GNU runtime exception boundary passed\n";
    return 0;
  }
  return 5;
}
