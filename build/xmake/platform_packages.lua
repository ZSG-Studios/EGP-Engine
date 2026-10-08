-- Native platform recipe metadata; staging and archives run after linking.
function configure(graph)
    local env = graph.environment
    if graph.options.platform ~= "web" then return end
    for _, field in ipairs({"JS_LIBS", "JS_PRE", "JS_POST", "JS_EXTERNS"}) do env[field] = {} end
    env.EXPORTED_FUNCTIONS = {"_main", "_malloc", "_free"}
    env.EXPORTED_RUNTIME_METHODS = {"callMain", "cwrap"}
    for _, bits in ipairs({8, 16, 32, 64}) do
        table.insert(env.EXPORTED_RUNTIME_METHODS, "HEAP" .. bits)
        table.insert(env.EXPORTED_RUNTIME_METHODS, "HEAPU" .. bits)
    end
    table.join2(env.EXPORTED_RUNTIME_METHODS, {"HEAPF32", "HEAPF64"})
    env.ENV = {}
    local function add(self, field, inputs)
        for _, filename in ipairs(inputs) do table.insert(self[field], self:file(filename)) end
    end
    env.AddJSLibraries = function(self, inputs) add(self, "JS_LIBS", inputs) end
    env.AddJSPre = function(self, inputs) add(self, "JS_PRE", inputs) end
    env.AddJSPost = function(self, inputs) add(self, "JS_POST", inputs) end
    env.AddJSExterns = function(self, inputs) add(self, "JS_EXTERNS", inputs) end
end
