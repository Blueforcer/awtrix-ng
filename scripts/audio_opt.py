Import("env")


def _audio_o2(node):
    return env.Object(
        node,
        # Decodes whatever a stream delivers, so it counts as input code. See stack_protector.py.
        CCFLAGS=env.InputCodeFlags(env["CCFLAGS"]) + ["-O2"],
        CPPPATH=env["CPPPATH"] + ["$PROJECT_SRC_DIR", "$PROJECT_INCLUDE_DIR"],
    )


env.AddBuildMiddleware(_audio_o2, "*core*audio*")
