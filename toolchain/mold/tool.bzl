"""Bind mold to the same execution architecture as the selected LLVM tools."""

load("@rules_cc//cc/toolchains:args.bzl", "cc_args")
load("@rules_cc//cc/toolchains/impl:documented_api.bzl", "cc_args_list")

def _unsupported_exec_impl(ctx):
    fail("Linux ELF linking requires a Linux aarch64 or x86_64 execution platform with glibc; no host linker or lld fallback is permitted (selected " + ctx.attr.platform + ")")

_unsupported_exec = rule(
    implementation = _unsupported_exec_impl,
    attrs = {"platform": attr.string()},
)

def mold_linker_args(name, exec_os, exec_cpu):
    """Declare target-Linux linker arguments for one fixed execution toolchain."""
    if exec_os == "linux" and exec_cpu in ["aarch64", "x86_64"]:
        binary = Label("@mold_linux_" + exec_cpu + "//:mold")
    else:
        binary = name + "_unsupported_exec"
        _unsupported_exec(name = binary, platform = exec_os + "_" + exec_cpu)

    cc_args(
        name = name + "_linux",
        actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
        args = ["--ld-path={mold}"],
        data = [binary],
        format = {"mold": binary},
    )
    plugin_args = []
    if exec_os == "linux" and exec_cpu in ["aarch64", "x86_64"]:
        plugin = Label("//toolchain/mold:llvmgold_" + exec_cpu)
        cc_args(
            name = name + "_plugin",
            actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
            args = ["-Wl,--plugin={plugin}"],
            data = [plugin],
            format = {"plugin": plugin},
        )

        # Omit the dependency, not merely its flags, while building the plugin.
        # Its seed-compiler/native-object link cannot depend on itself.
        plugin_args = select({
            "//toolchain/mold:plugin_build": [],
            "//conditions:default": [name + "_plugin"],
        })

    cc_args_list(name = name + "_elf", args = [name + "_linux"] + plugin_args)
    cc_args_list(
        name = name,
        args = select({
            "@platforms//os:linux": [name + "_elf"],
            "//conditions:default": [],
        }),
    )

def mold_from_toolchain(cc_toolchain):
    """Return the already selected linker input, without another exec transition."""
    binaries = [file for file in cc_toolchain.all_files.to_list() if file.basename == "mold"]
    if len(binaries) != 1:
        fail("Linux ELF linking requires exactly one declared mold input; found " + str(len(binaries)))
    return binaries[0]
