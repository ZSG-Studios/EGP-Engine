def can_build(env, platform):
    env.module_add_dependencies("litenet", ["mono"])
    return platform in ("windows", "linuxbsd", "macos")


def configure(env):
    pass


def get_doc_classes():
    return ["EGPLiteSession"]


def get_doc_path():
    return "doc_classes"


def is_enabled():
    return False
