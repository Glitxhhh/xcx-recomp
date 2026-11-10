return {
  init = function()
    assert(host.api == 1 and host.id == 'beta')
    assert(os == nil and io == nil and package == nil and require == nil)
    assert(debug == nil and coroutine == nil and load == nil and pcall == nil)
    assert(setmetatable == nil and getmetatable == nil and host.panel == nil)
    assert(table.move == nil)
    assert(string.dump == nil and ('x').dump == nil and ('x').rep == nil)
    assert(table.concat({'portable', 'source'}, ' ') == 'portable source')
    assert(math.floor(1.5) == 1 and string.upper('lua') == 'LUA')
    host.log('beta initialized')
  end,
  events = { ['host.started'] = function(event)
    assert(event.phase == 'startup')
    host.log('beta received host.started')
  end },
  shutdown = function() host.log('beta shutdown') end
}
