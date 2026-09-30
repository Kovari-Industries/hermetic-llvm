# GCC 13.3 filesystem ABI separation

`libstdcxx-filesystem-inline-abi.patch` preserves the namespace of the C++11 ABI's
private directory helper when the pinned Clang 23 bootstrap builds libstdc++.
It does not change the compiler, headers, configure policy, public APIs, or the
existing dual-ABI selection.

## Failure

This reduced example produces different symbols with the pinned Clang and GCC:

```cpp
namespace outer {
inline namespace abi { struct Dir; }
}
namespace fs = outer;
template <typename T> struct Ptr {};
template class Ptr<fs::Dir>;
struct fs::Dir { void advance(); };
void fs::Dir::advance() {}
```

GCC emits `outer::abi::Dir::advance()`. The pinned Clang emits
`outer::Dir::advance()`. Explicitly spelling `struct fs::abi::Dir` restores the
expected namespace. Selecting Clang ABI compatibility versions 22 or 17 does
not correct the reduced case.

In GCC 13.3's `fs_dir.cc`, this loses `__cxx11` from `_Dir` helper symbols. The
COW and C++11 object files then define the same weak `advance` and destructor
symbols despite their different `path` layouts. The linked runtime can call the
COW helper with a C++11 object. Recursive directory removal segfaults; this was
reproduced without ROS, JSON, or GoogleTest.

## Fix and verification scope

For `_GLIBCXX_USE_CXX11_ABI=1`, qualify the definition as
`fs::__cxx11::_Dir`. Keep the existing `fs::_Dir` definition for ABI 0. The
existing GCC patch mechanism applies this change only to version 13.3.0.

The runtime test package contains `filesystem_runtime_abi0_test` and
`filesystem_runtime_abi1_test`. Both create and remove a nonempty nested tree
using the selected runtime. Run them with a Linux GNU platform that selects
`libstdcxx.13.3.0`, alongside `gnu_runtime_link_test` and `shared_elf_test`.

Qualification through Evo's Linux x86_64 development container passed nine
selected Bazel test targets with cached test results disabled: both ABI tests,
GNU runtime linking, shared ELF checks, the standalone filesystem regression,
the unchanged five-test sensor suite, Damiao protocol, command watchdog, and
the declared GLib/GStreamer runtime check. The platform selected GCC 13.3.0
libstdc++ and glibc 2.39. The log is `/tmp/ros-runtime-fixed-tests.log` in the
`evo-bazel-crates` container.

This patch does not establish qualification for other GCC versions, compiler
uses, or architectures. It is a runtime source adjustment, not a general fix to
Clang's handling of qualified inline-namespace definitions.
