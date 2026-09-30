#include <cstdlib>
#include <filesystem>
#include <fstream>

int main() {
  const char* temporary = std::getenv("TEST_TMPDIR");
  if (!temporary) return 1;
  const auto root = std::filesystem::path(temporary) / "filesystem_runtime";
  std::filesystem::create_directories(root / "nested");
  {
    std::ofstream output(root / "nested" / "file");
    output << "test";
    output.close();
    if (!output) return 2;
  }
  const auto removed = std::filesystem::remove_all(root);
  return removed == 3 && !std::filesystem::exists(root) ? 0 : 3;
}
