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
class NamedTasks {
    NativeTask task
    void configureEach(Closure configure) {
        configure.delegate = task
        configure.resolveStrategy = Closure.DELEGATE_FIRST
        configure.call(task)
    }
}
class FixtureTasks {
    Map<String, NativeTask> entries = [:]
    // Names of the AGP tasks the production script may wire into (created later by AGP).
    List<String> agpTasks = []
    NativeTask register(String name, Class type, Closure configure) {
        assert type == Exec
        assert !entries.containsKey(name)
        NativeTask task = new NativeTask(name: name)
        configure.delegate = task
        configure.resolveStrategy = Closure.DELEGATE_FIRST
        configure.call()
        entries[name] = task
        return task
    }
    // Lazy, name-filtered configuration (TaskContainer.named(Spec<String>).configureEach).
    NamedTasks named(Closure spec) {
        List<String> matches = agpTasks.findAll { spec.call(it) }
        assert matches.size() == 1
        return new NamedTasks(task: getAt(matches[0]))
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
Map<String, List<String>> flavorsBuildTypes = [editor: ["debug", "release"], template: ["debug", "release"]]
flavorsBuildTypes.each { flavor, buildTypes -> buildTypes.each { tasks.agpTasks.add("merge${flavor.capitalize()}${it.capitalize()}JniLibFolders".toString()) } }
// The lib script reads the root project's ABI and flavor tables explicitly (rootProject.*).
rootProject.selectedAbis = ["arm32", "arm64", "x86_32", "x86_64"]
rootProject.supportedAbis = ["arm32", "arm64", "x86_32", "x86_64"]
rootProject.supportedFlavorsBuildTypes = flavorsBuildTypes
Binding binding = new Binding(
    rootProject: rootProject, project: new Expando(findProperty: { String key -> key == "xmakeExecutable" ? "fixture-xmake" : null }),
    pathToRootDir: "../../../../", tasks: tasks, Exec: Exec,
    file: { String relative -> new File(new File(androidRoot, "lib"), relative).canonicalFile })
String source = new File(androidRoot, "lib/build.gradle").text
String registration = source.substring(source.indexOf("// BEGIN native build tasks"), source.indexOf("// END native build tasks"))
new GroovyShell(this.class.classLoader, binding).evaluate(registration)
List<NativeTask> nativeTasks = tasks.entries.values().findAll { !it.actions.empty }
assert nativeTasks.size() == 16
assert nativeTasks.every { it.command == null && it.directory == engine }
List<NativeTask> mergeTasks = tasks.entries.values().findAll { it.name.startsWith("merge") }
assert mergeTasks.size() == 4 && mergeTasks.every { it.dependencies.size() == 4 }
assert !source.contains("findInPath") && !source.toLowerCase().contains("scons")
// AGP 9 removed the old variant API: archives keep AGP's names and the root copy tasks rename them.
String rootBuild = new File(androidRoot, "build.gradle").text
assert source.contains('archivesName = "godot-lib"') && !source.contains("libraryVariants") && !source.contains("outputFileName")
assert rootBuild.contains('include("godot-lib-template-${target}.aar")') && rootBuild.count('rename { "godot-lib.template_${targetSuffix}.aar" }') == 2
assert rootBuild.contains('include("android-${edition}-${target}.apk", "android-${edition}-${target}-unsigned.apk")') && rootBuild.contains('rename { "android_${filenameSuffix}.apk" }')
checks += 6
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
// AGP 9 built-in Kotlin: no module applies the Kotlin Android plugin or the KGP 'kotlinOptions' block.
List<File> gradleScripts = []
androidRoot.eachFileRecurse { File f -> if (f.name.endsWith(".gradle") && !f.path.contains("${File.separator}build${File.separator}")) { gradleScripts.add(f) } }
assert gradleScripts.size() >= 10
assert gradleScripts.every { !it.text.contains("org.jetbrains.kotlin.android") && !it.text.contains("kotlinOptions") }
checks += 7
println "ANDROID_XMAKE_GRADLE_FIXTURE_PASS $checks"
