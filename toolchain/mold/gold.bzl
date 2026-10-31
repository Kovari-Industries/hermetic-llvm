"""Build LLVMgold using the seed compiler, never the plugin being built."""

load("@llvm_version//:version.bzl", "MOLD_PLUGIN_BUILD_SETTINGS")
load("//toolchain/bootstrap:transition_settings.bzl", "SANITIZER_FLAGS", "disable_sanitizers")

def _plugin_transition_impl(settings, attr):
    result = {
        "//runtimes:linkmode": "static",
        "//command_line_option:host_compilation_mode": "opt",
        "//command_line_option:host_features": [],
        "//command_line_option:strip": "always",
        "//config:runtimes_optimization_mode": "optimized",
        "//config:empty_sysroot": True,
        "//command_line_option:collect_code_coverage": False,
        "@llvm-project//third-party:llvm_enable_zstd": True,
        "@llvm-project//third-party:llvm_enable_zlib": True,
        "//toolchain/mold:building_plugin": True,
        "//toolchain:bootstrap_stage": "stage0_prebuilt_seed",
        "//toolchain:runtime_stage": "complete",
        "//command_line_option:platforms": str(attr.platform),
        "//command_line_option:compilation_mode": "opt",
        "//command_line_option:features": ["-thin_lto"],
        "//command_line_option:copt": [],
        "//command_line_option:cxxopt": [],
        "//command_line_option:conlyopt": [],
        "//command_line_option:linkopt": [],
        "//command_line_option:host_copt": [],
        "//command_line_option:host_cxxopt": [],
        "//command_line_option:host_conlyopt": [],
        "//command_line_option:host_linkopt": [],
        "//command_line_option:fdo_profile": None,
    }

    result.update(MOLD_PLUGIN_BUILD_SETTINGS)
    disable_sanitizers(result)
    return result

_plugin_transition = transition(
    implementation = _plugin_transition_impl,
    inputs = [],
    outputs = [
        "//runtimes:linkmode",
        "//command_line_option:host_compilation_mode",
        "//command_line_option:host_features",
        "//command_line_option:strip",
        "//config:runtimes_optimization_mode",
        "//config:empty_sysroot",
        "//command_line_option:collect_code_coverage",
        "@llvm-project//third-party:llvm_enable_zstd",
        "@llvm-project//third-party:llvm_enable_zlib",
        "//toolchain/mold:building_plugin",
        "//toolchain:bootstrap_stage",
        "//toolchain:runtime_stage",
        "//command_line_option:platforms",
        "//command_line_option:compilation_mode",
        "//command_line_option:features",
        "//command_line_option:copt",
        "//command_line_option:cxxopt",
        "//command_line_option:conlyopt",
        "//command_line_option:linkopt",
        "//command_line_option:host_copt",
        "//command_line_option:host_cxxopt",
        "//command_line_option:host_conlyopt",
        "//command_line_option:host_linkopt",
        "//command_line_option:fdo_profile",
    ] + SANITIZER_FLAGS + MOLD_PLUGIN_BUILD_SETTINGS.keys(),
)

def _plugin_impl(ctx):
    return [DefaultInfo(files = ctx.attr.actual[0][DefaultInfo].files)]

_llvmgold = rule(
    implementation = _plugin_impl,
    attrs = {
        "actual": attr.label(default = "@llvm-project//llvm/tools/gold:LLVMgold.so", cfg = _plugin_transition),
        "platform": attr.label(mandatory = True),
        "_allowlist_function_transition": attr.label(default = "@bazel_tools//tools/allowlists/function_transition_allowlist"),
    },
)

def _execution_plugin_impl(ctx):
    return [DefaultInfo(files = ctx.attr.actual[DefaultInfo].files)]

_execution_plugin = rule(
    implementation = _execution_plugin_impl,
    attrs = {"actual": attr.label(mandatory = True, cfg = "exec")},
)

def llvmgold(name, platform):
    """Use execution configuration consistently, then pin the linker's CPU/ABI."""
    _llvmgold(name = name + "_native", platform = platform)
    _execution_plugin(name = name, actual = ":" + name + "_native")
