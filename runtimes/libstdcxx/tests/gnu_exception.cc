#include <stdexcept>

namespace {
struct Cleanup {
  int* value;
  ~Cleanup() { ++*value; }
};
}  // namespace

extern "C" void throw_from_shared_library(int* cleaned_up) {
  Cleanup cleanup{cleaned_up};
  throw std::runtime_error("exception from shared library");
}
