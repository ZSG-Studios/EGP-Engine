function main(root, compiler, policy_arguments)
    local source=io.readfile(path.join(root,"platform/windows/os_windows.cpp"))
    local implementation=assert(source:match("(class FallbackTextAnalysisSource : public IDWriteTextAnalysisSource.-\n};)"),"The private DirectWrite implementation must be available for the SDK compile contract")
    local prelude=[[
#include <windows.h>
#include <dwrite.h>
#include <mmdeviceapi.h>
// Only the string storage is substituted; the COM implementation is copied from the engine.
class Char16String {
public:
    int length() const noexcept { return 0; }
    const char16_t *get_data() const noexcept { return nullptr; }
};
// The production implementation already scopes this COM destructor diagnostic.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
]]
    local checks=[[
#pragma clang diagnostic pop
static_assert(noexcept(static_cast<FallbackTextAnalysisSource *>(nullptr)->GetTextAtPosition(0,nullptr,nullptr)));
static_assert(noexcept(static_cast<FallbackTextAnalysisSource *>(nullptr)->GetTextBeforePosition(0,nullptr,nullptr)));
static_assert(noexcept(static_cast<FallbackTextAnalysisSource *>(nullptr)->GetParagraphReadingDirection()));
static_assert(noexcept(static_cast<FallbackTextAnalysisSource *>(nullptr)->GetLocaleName(0,nullptr,nullptr)));
static_assert(noexcept(static_cast<FallbackTextAnalysisSource *>(nullptr)->GetNumberSubstitution(0,nullptr,nullptr)));
]]
    local directory=os.tmpfile() .. "-directwrite"
    os.mkdir(directory)
    local fixed=path.join(directory,"directwrite_fixed.cpp")
    local object=path.join(directory,"directwrite_fixed.obj")
    io.writefile(fixed,prelude .. implementation .. "\n" .. checks)
    local arguments=table.join(policy_arguments,{"/std:c++17","/EHsc","-fms-compatibility-version=19.51",fixed,"-o",object})
    os.vrunv(compiler,arguments,{timeout=60000})
    assert(os.isfile(object),"All five real DirectWrite overrides must compile against the installed Windows SDK")
    local previous,replacements=implementation:gsub("(GetTextAtPosition%b()%s*)noexcept(%s+override)","%1%2",1)
    assert(replacements==1,"The SDK regression control must remove exactly one override contract")
    local broken=path.join(directory,"directwrite_previous.cpp")
    io.writefile(broken,prelude .. previous .. "\n#pragma clang diagnostic pop\n")
    local ok,diagnostic=utils.trycall(function()
        os.vrunv(compiler,table.join(policy_arguments,{"/std:c++17","/EHsc","-fms-compatibility-version=19.51",broken,"-o",path.join(directory,"directwrite_previous.obj")}),{timeout=60000})
    end)
    assert(not ok and tostring(diagnostic):find("Wmicrosoft%-exception%-spec"),"The same real SDK compile must reject the previous lax COM exception contract")
    os.tryrm(directory)
    print("NATIVE_WINDOWS_COM_SDK_COMPILE_PASS")
    return 8
end
