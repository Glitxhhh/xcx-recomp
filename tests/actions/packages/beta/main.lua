local count=0
return {
 init=function()
  host.settings.register {version=1,id='preferences',title='Personal preferences',actions={{id="success",label="Run successful action",provider=true},{id="delayed",label="Run delayed action",provider=true},{id="reject",label="Try rejected action",provider=true},{id="fail",label="Run failed action",provider=true}},fields={
   {id='notices',label='Show notices',type='bool',default=false},
   {id='size',label='Notice size',type='integer',min=1,max=10,default=3},
   {id='style',label='Notice style',type='enum',default='compact',options={{id='compact',label='Compact'},{id='roomy',label='Roomy'},{id='quiet',label='Quiet'}}}
  }}
  host.log('beta size '..host.settings.get('size'))
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
  ['host.started']=function(e) host.log('beta ready') end,
  ['settings.changed']=function(e) host.log('beta changed '..e.field..' '..tostring(e.value)) end
 },
 shutdown=function() host.log('beta stopped') end
}
