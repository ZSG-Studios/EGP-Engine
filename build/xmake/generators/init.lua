function generate(job, context)
    context = context or {}
    context.root = context.root or os.projectdir() or os.curdir()
    if import('wayland', {rootdir=os.scriptdir()}).generate(job, context) then return end
    local extension = import('extension', {rootdir=os.scriptdir()})
    if extension.generate(job, context) then return end
    local virtuals = import('virtuals', {rootdir=os.scriptdir()})
    if virtuals.generate(job,context) then return end
    if job.builder:find('make_ltc_lut',1,true) then
        local util=import('support',{rootdir=os.scriptdir()})
        local text={'// LTC Lookup table for BRDF fitting by Eric Heitz (https://eheitzresearch.wordpress.com/415-2/)\nconstexpr int LTC_LUT_DIMENSIONS = 64;\n'}
        for index=1,2 do
            local bytes=util.read(job.sources[index+1],context):sub(149)
            local values={}
            for offset=1,#bytes do table.insert(values,string.format('0x%02x',bytes:byte(offset))) end
            table.insert(text,'inline constexpr uint8_t LTC_LUT' .. index .. '[] = {' .. table.concat(values,',') .. '};\n')
        end
        util.write(job.targets[1],table.concat(text))
        return
    end
    local name = assert(job.builder):match('([%w_]+)$')
    for _, module in ipairs({'core', 'modules', 'assets', 'editor', 'license'}) do
        local handler = import(module, {rootdir = os.scriptdir()})
        if handler.generate(name, job, context) then return end
    end
    raise('Unsupported native generator: %s', job.builder)
end
