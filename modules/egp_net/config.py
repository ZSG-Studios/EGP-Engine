def can_build(env, platform):
    return platform in ("windows", "linuxbsd", "macos")


def configure(env):
    pass


def get_doc_classes():
    return [
        "EGPNetSession",
        "EGPNetSnapshotInterpolator",
        "Superposition",
        "SuperpositionConfig",
        "SuperpositionProperty",
    ]


def get_doc_path():
    return "doc_classes"


def is_enabled():
    return True
