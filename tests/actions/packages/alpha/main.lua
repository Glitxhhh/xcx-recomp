local count=0
return {
 init=function()
  host.settings.register {version=1,id='preferences',title='Workshop settings',actions={{id="success",label="Run successful action",provider=true},{id="delayed",label="Run delayed action",provider=true},{id="reject",label="Try rejected action",provider=true},{id="fail",label="Run failed action",provider=true}},fields={
   {id='hints',label='Show hints',type='bool',default=true},
   {id='strength',label='Effect strength',type='integer',min=0,max=100,default=25},
   {id='theme',label='Theme',type='enum',default='calm',options={{id='calm',label='Calm'},{id='bright',label='Bright'}}}
  }}
  host.log('alpha strength '..host.settings.get('strength'))
 end,
 events={
  ['ui.action']=function(e)
   local request,reason
   if e.action=='delayed' then request,reason=host.provider.submit('delayed',5000)
   elseif e.action=='fail' then request,reason=host.provider.submit('fail',true)
   else request,reason=host.provider.submit(e.action) end
   host.ui.status(request and ('Accepted '..e.action) or ('Not accepted: '..reason))
  end,
  ['provider.event']=function(e)
   if e.kind=='result' then count=count+1;host.ui.status(e.result..': '..e.message..' (results '..count..')');host.log(host.id..' result '..e.result) end
  end,
  ['host.started']=function(e) host.log('alpha ready') end,
  ['settings.changed']=function(e) host.log('alpha changed '..e.field..' '..tostring(e.value)) end
 },
 shutdown=function() host.log('alpha stopped') end
}
