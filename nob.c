#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "./thirdparty/nob.h"

#define SRC_FOLDER   "./src/"
#define BUILD_FOLDER "./build/"

const char *thirdparty_objects[] = {
    "arena",
    "flag",
    "glob",
    "jim",
    "jimp",
    "libc",
    "nob",
    "shlex",
    "time",
};

// TODO: Support MacOS and Android.
typedef enum {
    TARGET_POSIX,
    TARGET_MINGW32,
    TARGET_MSVC,
} Build_Target;

#if defined(_WIN32) && defined(_MSC_VER)
    Build_Target default_target = TARGET_MSVC;
#elif defined(_WIN32)
    Build_Target default_target = TARGET_MINGW32;
#else
    Build_Target default_target = TARGET_POSIX;
#endif

typedef struct {
    size_t max_procs;
    Cmd cmd;
    Procs procs;
    String_Builder sb;
} Build_Context;

bool run_cmd(Build_Context *build) {
    Proc proc = cmd_run_async_and_reset(&build->cmd);
    if (build->max_procs) {
        return procs_append_with_flush(&build->procs, proc, build->max_procs);
    } else {
        da_append(&build->procs, proc);
        return true;
    }
}

bool build_thirdparty_objects(Build_Context *build, Build_Target target, File_Paths *object_paths) {
    const char *cc;
    switch (target) {
        case TARGET_POSIX:   cc = getenv("CC");             break;
        case TARGET_MINGW32: cc = "x86_64-w64-mingw32-gcc"; break;
        case TARGET_MSVC:    cc = "cl";                     break;
    }
    if (!cc) cc = "cc";

    const char *object_ext;
    switch (target) {
        case TARGET_POSIX:
        case TARGET_MINGW32: object_ext = ".o";   break;
        case TARGET_MSVC:    object_ext = ".obj"; break;
    }

    for (int i = 0; i < ARRAY_LEN(thirdparty_objects); i++) {
        const char *object_name = thirdparty_objects[i];
        const char *output_path = temp_sprintf(BUILD_FOLDER"%s%s", object_name, object_ext);
        const char *input_path = temp_sprintf("./thirdparty/%s.c", object_name);
        switch (target) {
            case TARGET_POSIX:
            case TARGET_MINGW32: {
                cmd_append(
                    &build->cmd,
                    cc,
                    "-fPIC",
                    "-g",
                    "-c", input_path,
                    "-o", output_path,
                );
                cmd_append(&build->cmd, "-lc", "-lgcc");
            } break;

            case TARGET_MSVC: {
                cmd_append(
                    &build->cmd,
                    cc,
                    "/nologo",
                    "/Zi",
                    "/MD",
                    "/c", input_path,
                    temp_sprintf("/Fo:%s", output_path),
                    temp_sprintf("/Fd:%s%s.pdb", BUILD_FOLDER, object_name),
                );
            } break;
        }
        da_append(object_paths, output_path);
        if (!run_cmd(build)) return false;
    }
    return true;
}

bool build_crust_executable(Build_Context *build, Build_Target target, File_Paths object_paths, const char *program_name, const char **executable_path) {
    const char *executable_ext;
    switch (target) {
        case TARGET_POSIX:   executable_ext = "";     break;
        case TARGET_MINGW32:
        case TARGET_MSVC:    executable_ext = ".exe"; break;
    }

    const char *input_path  = temp_sprintf(SRC_FOLDER"%s.rs", program_name);
    const char *output_path = temp_sprintf(BUILD_FOLDER"%s%s", program_name, executable_ext);

    if (executable_path) {
        *executable_path = output_path;
    }

    // This is pretty weird, but since we need linker arguments in a space-separated string to pass to rustc,
    // it's actually quite convenient to use the Cmd to collect the arguments and render that to the string.
    da_append_many(&build->cmd, object_paths.items, object_paths.count);
    switch (target) {
        case TARGET_POSIX: {
            cmd_append(&build->cmd, "-lc", "-lgcc");
        } break;
        case TARGET_MINGW32: {
            cmd_append(&build->cmd, "-lmingwex", "-lmsvcrt", "-lkernel32");
        } break;
        case TARGET_MSVC: {
            cmd_append(&build->cmd, "msvcrt.lib", "legacy_stdio_definitions.lib");
        } break;
    }

    // Must not add quotes here because Cmd will quote this entire argument when rendered.
    build->sb.count = 0;
    sb_append_cstr(&build->sb, "link-args=");
    cmd_render(build->cmd, &build->sb);
    sb_append_null(&build->sb);
    const char *link_args = build->sb.items;

    build->cmd.count = 0;
    cmd_append(
        &build->cmd,
        "rustc",
        "-g",
        "--edition", "2021",
        "-C", "opt-level=0",
        "-C", "panic=abort",
        "-C", link_args,
        input_path,
        "-o", output_path,
    );

    switch (target) {
        case TARGET_POSIX:   break;
        case TARGET_MINGW32: cmd_append(&build->cmd, "--target", "x86_64-pc-windows-gnu");  break;
        case TARGET_MSVC:    cmd_append(&build->cmd, "--target", "x86_64-pc-windows-msvc"); break;
    }

    return run_cmd(build);
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "./thirdparty/nob.h");

    Build_Context build = {0};
    Build_Target target = default_target;

    // TODO: Accept target as a command line argument.
    // TODO: Accept max_procs as a command line argument.

    if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;

    File_Paths object_paths = {0};
    if (!build_thirdparty_objects(&build, target, &object_paths)) return 1;
    if (!procs_wait_and_reset(&build.procs)) return 1;

    const char *bgen_path;
    if (!build_crust_executable(&build, target, object_paths, "bgen", &bgen_path)) return 1;
    if (!procs_wait_and_reset(&build.procs)) return 1;

    cmd_append(&build.cmd, bgen_path);
    if (!cmd_run_sync_and_reset(&build.cmd)) return 1;

    if (!build_crust_executable(&build, target, object_paths, "b", NULL)) return 1;
    if (!build_crust_executable(&build, target, object_paths, "btest", NULL)) return 1;
    if (!procs_wait_and_reset(&build.procs)) return 1;

    return 0;
}
