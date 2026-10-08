-- Fail closed when test setup returns zero before executing the native suite.
function parse(text)
    text=text:gsub("\27%[[%d;]*m", "")
    local result={}
    for _,label in ipairs({"test cases", "assertions"}) do
        local summaries={}
        for total,passed,failed in text:gmatch("%[doctest%]%s+"..label..":%s+(%d+)%s+|%s+(%d+)%s+passed%s+|%s+(%d+)%s+failed") do
            table.insert(summaries,{total=tonumber(total),passed=tonumber(passed),failed=tonumber(failed)})
        end
        assert(#summaries==1,"Missing or ambiguous doctest "..label.." completion summary")
        local summary=summaries[1]
        assert(summary.total>0 and summary.passed==summary.total and summary.failed==0,"Native "..label.." did not complete successfully")
        result[label=="test cases" and "test_cases" or "assertions"]=summary.total
    end
    local statuses={}
    for status in text:gmatch("%[doctest%]%s+Status:%s+(%u+)!?") do table.insert(statuses,status) end
    assert(#statuses==1 and statuses[1]=="SUCCESS","Missing or failed doctest final status")
    result.passed=true
    return result
end

function main(logfile,receiptfile)
    assert(logfile and receiptfile,"Usage: xmake lua misc/scripts/validate_egp_unit_summary.lua <log> <receipt>")
    local result=parse(assert(io.readfile(logfile)))
    result.log=path.absolute(logfile)
    result.log_sha256=hash.sha256(logfile)
    import("core.base.json").savefile(receiptfile,result)
    print("NATIVE_UNIT_SUITE_PASS cases="..result.test_cases.." assertions="..result.assertions)
end
