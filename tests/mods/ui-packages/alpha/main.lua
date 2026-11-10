return {
 init=function()
  host.settings.register {version=1,id='preferences',title='Workshop settings',fields={
   {id='hints',label='Show hints',type='bool',default=true},
   {id='strength',label='Effect strength',type='integer',min=0,max=100,default=25},
   {id='theme',label='Theme',type='enum',default='calm',options={{id='calm',label='Calm'},{id='bright',label='Bright'}}}
  }}
  host.log('alpha strength '..host.settings.get('strength'))
 end,
 events={
  ['host.started']=function(e) host.log('alpha ready') end,
  ['settings.changed']=function(e) host.log('alpha changed '..e.field..' '..tostring(e.value)) end
 },
 shutdown=function() host.log('alpha stopped') end
}
