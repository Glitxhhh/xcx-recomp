return {
  init = function() host.log('alpha initialized') end,
  events = { ['host.started'] = function(event)
    assert(event.api == 1 and event.phase == 'startup')
    event.phase = 'alpha-local-copy'
    host.log('alpha received host.started')
    error('intentional fixture failure')
  end },
  shutdown = function() host.log('alpha shutdown') end
}
