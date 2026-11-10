return {
 init=function()
  host.settings.register {version=1,id='preferences',title='Personal preferences',fields={
   {id='notices',label='Show notices',type='bool',default=false},
   {id='size',label='Notice size',type='integer',min=1,max=10,default=3},
   {id='style',label='Notice style',type='enum',default='compact',options={{id='compact',label='Compact'},{id='roomy',label='Roomy'},{id='quiet',label='Quiet'}}}
  }}
  host.log('beta size '..host.settings.get('size'))
 end,
 events={
  ['host.started']=function(e) host.log('beta ready') end,
  ['settings.changed']=function(e) host.log('beta changed '..e.field..' '..tostring(e.value)) end
 },
 shutdown=function() host.log('beta stopped') end
}
