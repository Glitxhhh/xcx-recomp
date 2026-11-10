-- Regression cases derived from the four recorded MIT Lua upstream fixes.
local nan=0/0
for i=1,nan do error('NaN loop') end
for i=1,nan,-1 do error('NaN descending loop') end
for i=nan,1 do error('NaN initial value') end
local mz=-0.0
local zi=0
assert(1/(mz-0)==1/(mz-zi), 'negative zero')
coroutine.wrap(function()
    local a <close> = setmetatable({}, {__close=function() coroutine.close(coroutine.running()) end})
    error('y')
end)()
local sm=collectgarbage('param','stepmul',0)
local mm=collectgarbage('param','majorminor',2000)
collectgarbage(); collectgarbage()
local t={}; for i=1,1000000 do t[i]=true end
collectgarbage('step'); collectgarbage('step'); collectgarbage('step')
for i=1,1000000 do t[i]=nil end
t[2000000]=true
collectgarbage('step')
collectgarbage('param','stepmul',sm)
collectgarbage('param','majorminor',mm)
print('four Lua upstream regression families passed')
