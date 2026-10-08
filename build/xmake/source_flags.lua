-- Xmake uses distinct flag families for C/C++, Objective-C++, assembly and resources.
function file(policy, common_override)
    local common=common_override or {}
    if not common_override then for _, key in ipairs({'CCFLAGS','CPPFLAGS'}) do for _, flag in ipairs(policy[key] or {}) do table.insert(common,flag) end end end
    return {cxflags=table.clone(common),cflags=policy.CFLAGS,cxxflags=policy.CXXFLAGS,
        mxflags=table.clone(common),mflags=policy.CFLAGS,mxxflags=policy.CXXFLAGS,
        asflags=policy.ASFLAGS,mrcflags=policy.RCFLAGS}
end

function configure(target, policy)
    for _, flag in ipairs(policy.ARFLAGS or {}) do target:add('arflags',flag,{force=true}) end
end
