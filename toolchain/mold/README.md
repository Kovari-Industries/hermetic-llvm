# Linux ELF linker

Linux ELF links use mold 3.0.0, declared as a toolchain action input with an
explicit `--ld-path`. The binary is selected using the LLVM toolchain's fixed
execution OS/CPU, not the target architecture or a separate exec transition.
This applies to Rust's C++ linker driver, ordinary C/C++ links, source-built
compiler stages and stage-zero relocatable ELF object links. There is no PATH
lookup, host-installed linker, or automatic lld fallback for ELF links.

`MODULE.bazel` pins the official release archives:

| Execution CPU | SHA-256 |
| --- | --- |
| aarch64 | `52c759d3689babaea4c42af2f31062a74ab83e17ffb5a8090232534c67b70577` |
| x86_64 | `6c90d4a474c7c0409dfb575be03a5345878ac14fdba18de8b40fa58c60121189` |

Source: https://github.com/rui314/mold/releases/tag/v3.0.0 . Downloaded archive
hashes were checked against the release asset SHA-256 digests. These are glibc
executables, not static/musl tools: the ARM binary requires glibc >= 2.30 and the
x86 binary >= 2.18, plus the execution host's libgcc_s. Ubuntu 24.04 qualifies;
pure-musl execution hosts and other execution OSes cannot use these binaries.
The target glibc baseline remains independent of this execution requirement.

## LTO and non-ELF operations

Normal Rust `-Clto=thin` runs inside rustc and emits native objects for mold.
This does **not** require an LLVMgold plugin, and the repository's Rust release
profile is unchanged.

C/C++ bitcode linking, including Bazel distributed `thin_lto`, uses the declared
`LLVMgold.so` built from **the same `@llvm-project` sources** as the configured
LLVM version. It links the configured backend libraries and LLVM's LTO library;
there is no system plugin lookup or separately versioned LLVM binary dependency.
The argument `-Wl,--plugin=<declared path>` explicitly selects it. Clang also
injects its default adjacent plugin path for `-flto`; mold's final explicit
`--plugin` wins, so the nonexistent minimal-archive path is not loaded.

The plugin is compiled for the **linker's execution CPU**, not the output CPU,
with a glibc 2.28 baseline and static libc++/libc++abi/libunwind. Only `onload` is
exported. Its GNU linker-plugin API header is checksum-pinned to GCC commit
`2bfd402f8569511901ec8fe7628f57471e6d240a`, SHA-256
`0fd1645b409e2c83dc60f5387178a6cb02812bc46cf533655fa595c59dc93028`.
The header is fetched separately to avoid expanding unrelated GCC runtime
archives; its upstream licensing notice is preserved.

`gold.bzl` builds this tool using the downloaded seed compiler with native object
files. The private `building_plugin` configuration omits the plugin dependency
itself from that compiler's toolchain, preventing a cycle. This affects **only
the plugin's construction**: requested application ThinLTO remains enabled.
The plugin inherits Bazel's selected **host compilation mode** (already applied
by its outer execution transition), stripping policy and explicit runtime
optimization setting. It does not force `opt`: `--host_compilation_mode=dbg`
compiles LLVMgold and its LLVM libraries with `-O0 -g`; `opt` retains `-O2`.
Raw compiler/linker options, sanitizers and FDO from the application remain
isolated to prevent self-dependent LTO/toolchain configurations. The root module
supplies private foreign-setting resets through
`llvm.configure(mold_plugin_build_settings = ...)`. Label keys resolve in the
root's repository mapping and values are JSON literals. LLVM has no reverse
module dependency on `rules_rs` and no hardcoded generated Rust repository name.
The root currently resets only Rust's bootstrap setting to false.

Per-crate Rust flags are target-scoped by the root's pinned `rules_rust` patch.
Bazel therefore excludes them from execution configurations, including its
execution baseline used to name outputs. This matches `rules_rust`: per-crate
flags affect target compilations, not execution-tool compilations. Rust's
per-crate trimming can no longer create extra copies of this C++ tool.

Do not replace that scope with a transition that restores an equivalent list:
Bazel 9.3.0rc2 can store CLI lists and transition-produced lists differently,
producing different output suffixes despite identical visible configuration
values. Clearing a nonempty top-level list also changes the dynamic-baseline
difference. Both approaches lose reuse of warm toolchain artifacts.

The root's non-compiling `plugin_configuration_test` fixtures check actual
artifact sharing for bootstrap/non-bootstrap and retained/trimmed caller
settings, while verifying the caller's CGU/bootstrap settings survive and the
real plugin/LLVM compile actions obey the host mode. Run them under each
repository profile, including mixed target/host modes. Application Rust flags,
the native execution CPU/ABI and requested application C++ ThinLTO remain intact.

Runtime prerequisites retain their independently selected optimization policy
(`runtimes_optimization_mode` defaults to `optimized`). In particular, glibc
requires optimization and its overlay explicitly supplies `-O2`. Explicit
source-compiler/FDO bootstrap stages have their own optimized build policy;
they are not the default prebuilt-seed path used by ordinary CI.


Source archive, Git and path overrides all receive the gold
BUILD overlay; path overrides are not modified in place.

The first plugin build compiles LLVM libraries and can be substantial. Ordinary
Bazel action caching then reuses the artifact. Native-object links need not load
LLVMgold, but it remains a declared toolchain input, including for remote
execution. Requalify the actual bitcode path when updating LLVM or mold.

Mach-O, PE/COFF and WebAssembly outputs retain their existing linkers. The
internal sanitizer symbolizer's `--lto-emit-llvm`/`--lto-newpm-passes` operation
also retains LLVM's tool: it produces LLVM IR, not a final ELF binary. Its final
ELF link uses mold. These are format/IR requirements, not fallback ELF links.

The root workspace's `tools/bazel/profiles` contains actual Rust and C++ link
argument/input regressions and small executable fixtures. Qualification results
belong in that area's README; execution on ARM64 does not establish native
x86_64 execution or optional C++ bitcode support.
