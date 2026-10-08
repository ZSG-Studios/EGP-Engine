// Evaluate the production Gradle native route without downloading Android plugins or invoking compilers.
class GradleException extends RuntimeException {
    GradleException(String message) { super(message) }
}
class Exec {}
class NativeTask {
    String name
    File directory
    List<String> command
    List<Closure> actions = []
    List<String> dependencies = []
    void workingDir(File value) { directory = value }
    void doFirst(Closure action) { actions.add(action) }
    void commandLine(List<String> value) { command = value }
    void dependsOn(String value) { dependencies.add(value) }
    void execute() {
        actions.each { action ->
            action.delegate = this
            action.resolveStrategy = Closure.DELEGATE_FIRST
            action.call()
        }
    }
}
class FixtureTasks {
    Map<String, NativeTask> entries = [:]
    NativeTask create(Map options, Closure configure) {
        assert options.type == Exec
        assert !entries.containsKey(options.name)
        NativeTask task = new NativeTask(name: options.name)
        configure.delegate = task
        configure.resolveStrategy = Closure.DELEGATE_FIRST
        configure.call()
        entries[options.name] = task
        return task
    }
    NativeTask getAt(String name) {
        if (!entries.containsKey(name)) { entries[name] = new NativeTask(name: name) }
        return entries[name]
    }
}

File engine = new File(args[0]).canonicalFile
File androidRoot = new File(engine, "platform/android/java")
assert new File(".").canonicalFile == androidRoot
int checks = 0
Map extensions = [:]
Binding helperBinding = new Binding(ext: extensions)
new GroovyShell(this.class.classLoader, helperBinding).evaluate(new File(androidRoot, "scripts/native-build.gradle"))
def rootProject = new Expando()
extensions.each { key, value -> rootProject.setProperty(key, value) }
FixtureTasks tasks = new FixtureTasks()
List variants = []
for (String flavor in ["editor", "template"]) {
    for (String buildType in ["debug", "release"]) {
        String fixtureFlavor = flavor
        Map output = [:]
        variants.add(new Expando(getFlavorName: { -> fixtureFlavor }, buildType: [name: buildType],
            outputs: new Expando(all: { Closure action -> action.call(output) }), artifact: output))
    }
}
Binding binding = new Binding(
    rootProject: rootProject, project: new Expando(findProperty: { String key -> key == "xmakeExecutable" ? "fixture-xmake" : null }),
    libraryVariants: new Expando(all: { Closure action -> variants.each { action.call(it) } }),
    selectedAbis: ["arm32", "arm64", "x86_32", "x86_64"], supportedAbis: ["arm32", "arm64", "x86_32", "x86_64"],
    supportedFlavorsBuildTypes: [editor: ["debug", "release"], template: ["debug", "release"]],
    pathToRootDir: "../../../../", tasks: tasks, Exec: Exec,
    file: { String relative -> new File(new File(androidRoot, "lib"), relative).canonicalFile })
String source = new File(androidRoot, "lib/build.gradle").text
String registration = source.substring(source.indexOf("libraryVariants.all"), source.indexOf("    publishing {"))
new GroovyShell(this.class.classLoader, binding).evaluate(registration)
List<NativeTask> nativeTasks = tasks.entries.values().findAll { !it.actions.empty }
assert nativeTasks.size() == 16
assert nativeTasks.every { it.command == null && it.directory == engine }
assert variants*.artifact*.outputFileName == ["godot-lib.editor.aar", "godot-lib.editor.aar", "godot-lib.template_debug.aar", "godot-lib.template_release.aar"]
assert tasks.entries.values().findAll { it.name.startsWith("merge") }.every { it.dependencies.size() == 4 }
assert !source.contains("findInPath") && !source.toLowerCase().contains("scons")
checks += 5
nativeTasks.each { task ->
    task.execute()
    List<String> command = task.command
    assert command[0] == "fixture-xmake" && command[1] == "lua"
    assert new File(command[2]) == new File(engine, "misc/scripts/build_egp.lua") && command[3] == "android"
    assert command[5].toInteger() >= 1 && new File(command[6]) == new File(engine, ".build/xmake-cache")
    Map options = command[7].split(" ").collectEntries { String option -> option.split("=", 2).toList() }
    boolean editor = task.name.contains("Editor")
    boolean debug = task.name.contains("Debug")
    assert command[4] == (editor ? "editor" : "template_" + (debug ? "debug" : "release"))
    assert options.dev_build == debug.toString() && options.dev_mode == debug.toString() && options.debug_symbols == debug.toString()
    assert options.tests == (debug && editor).toString() && options.production == (!debug).toString() && options.store_release == (!debug).toString()
    assert options.generate_android_binaries == "no" && options.disable_physics_2d == "yes" && options.disable_physics_3d == "yes"
    assert task.name.endsWith(options.arch.capitalize())
    checks += 8
}
for (List<String> invalid in [["invalid", "debug", "arm64"], ["template", "invalid", "arm64"], ["template", "debug", "invalid"]]) {
    try {
        rootProject.getGodotNativeBuildCommand(*invalid, engine, "fixture-xmake")
        assert false : "Invalid native variant accepted"
    } catch (GradleException expected) { checks++ }
}
String config = new File(androidRoot, "app/config.gradle").text
String versionFunctions = config.substring(config.indexOf("ext.generateGodotLibraryVersion"), config.indexOf("final String VALUE_SEPARATOR_REGEX"))
Map versions = [:]
Binding versionBinding = new Binding(ext: versions)
new GroovyShell(this.class.classLoader, versionBinding).evaluate(versionFunctions)
versions.each { key, value -> versionBinding.setVariable(key, value) }
assert versions.getGodotLibraryVersion() == ["4.8.0.dev", 408001]
assert versions.getGodotPublishVersion() == "4.8.0.dev-SNAPSHOT"
assert versions.getGodotLibraryVersion() == ["4.8.0.dev", 408001]
assert !new File(androidRoot, "nativeSrcsConfigs/build.gradle").text.contains("externalNativeBuild")
assert !new File(engine, "editor/export/android_sdk_manager.cpp").text.contains('"cmake/')
checks += 5
println "ANDROID_XMAKE_GRADLE_FIXTURE_PASS $checks"
