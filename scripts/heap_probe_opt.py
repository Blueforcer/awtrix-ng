Import("env")


def _outside_lto(node):
    return env.Object(
        node,
        CCFLAGS=env["CCFLAGS"] + ["-fno-lto"],
        CPPPATH=env["CPPPATH"] + ["$PROJECT_SRC_DIR", "$PROJECT_INCLUDE_DIR"],
    )


env.AddBuildMiddleware(_outside_lto, "*HeapProbe*")
