local clicks=0
return {
 init=function()
  host.settings.register {
   version=1,id='preferences',title='Hello workshop',
   fields={{id='hints',label='Show hints',type='bool',default=true}},
   actions={{id='greet',label='Say hello'}}
  }
  host.ui.status('Choose Say hello.')
 end,
 events={
  ['host.started']=function() host.log('Hello workshop loaded') end,
  ['settings.changed']=function(e)
   host.ui.status('Show hints: '..tostring(e.value))
  end,
  ['ui.action']=function(e)
   if e.action=='greet' then clicks=clicks+1;host.ui.status('Hello! Clicks this run: '..tostring(clicks)) end
  end
 },
 shutdown=function() end
}
