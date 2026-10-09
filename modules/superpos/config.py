def can_build(env, platform):
    # Mobile/web are capability gates, not silently advertised supported targets.
    return platform in ("windows", "linuxbsd", "macos")

def get_opts(platform):
    from SCons.Variables import BoolVariable
    return [BoolVariable("superpos_dtls", "Compile Superpos DTLS backend against EGP's crypto profile", False)]

def configure(env):
    env.module_add_dependencies("superpos", ["mbedtls"])

def get_doc_classes():
    return ["SuperposUInt64", "SuperposField", "SuperposSchema", "SuperposSession", "SuperposWorld",
            "SuperposSpawnEntry", "SuperposSpawnCatalog", "SuperposReplicaView", "SuperposSpawner",
            "SuperposLockstepClient", "SuperposLockstepServer"]

def get_doc_path():
    return "doc_classes"

def is_enabled():
    return False
