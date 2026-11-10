from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];BUILD=Path(os.environ['ACTIONS_BUILD']);WIN=sys.platform=='win32';EXT='.dll' if WIN else '.so';EXE='.exe' if WIN else '';TARGET='windows-x86_64' if WIN else 'linux-x86_64';KEY='windows-sha256' if WIN else 'linux-sha256'
class Actions(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.base=Path(self.temp.name);self.mods=self.base/'mods';shutil.copytree(ROOT/'tests/actions/packages',self.mods);self.settings=self.base/'settings';self.settings.mkdir()
  for name in ['alpha','beta']:
   p=self.mods/name/'main.lua';p.write_text(p.read_text().replace("'delayed',5000","'delayed',80"))
  self.package('alpha','c0');self.package('beta','cpp');(self.mods/'providers-enabled.txt').write_text('alpha:alpha.native\nbeta:beta.native\n')
 def package(self,name,binary):
  lib=self.mods/name/'native'/TARGET/('provider'+EXT);lib.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(BUILD/(binary+EXT),lib);h=hashlib.sha256(lib.read_bytes()).hexdigest();(self.mods/name/'provider.ini').write_text(f'schema=1\nid={name}.native\nowner={name}\nabi=2.0\nrequired=1\n{KEY}={h}\n')
 def run_host(self,*steps,env=None,native=False):
  e=os.environ.copy();e.update(env or {});args=[str(BUILD/(('native' if native else 'lua')+EXE)),str(self.mods)]+list(steps if native else [str(self.settings),*steps]);p=subprocess.run(args,env=e,capture_output=True,text=True,timeout=10);self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertEqual(p.stderr,'');return p.stdout
 def edit(self,name,old,new):
  p=self.mods/name/'main.lua';s=p.read_text();self.assertIn(old,s);p.write_text(s.replace(old,new))
 def test_native_credit_capacity_and_slow_consumption(self):self.assertIn('32 reserved credits',self.run_host('credits',native=True))
 def test_success_delayed_rejected_and_failed(self):
  out=self.run_host('intent=alpha:success','pump=50','intent=beta:delayed','pump=150','intent=alpha:reject','pump=20','intent=beta:fail','pump=60');self.assertIn('alpha result ok',out);self.assertIn('Not accepted: rejected',out);self.assertIn('beta result ok',out);self.assertIn('beta result failed',out)
 def test_close_only_cancels_unaccepted_intents(self):
  out=self.run_host('intent=alpha:success','close','pump=120');self.assertNotIn('alpha result',out)
  out=self.run_host('intent=alpha:delayed','pump=10','close','pump=150','open');self.assertIn('alpha result ok',out);self.assertRegex(out,r'page alpha status=1 pending=0')
 def test_stop_pending_provider_first(self):
  out=self.run_host('intent=alpha:delayed','pump=10');self.assertNotIn('alpha result ok',out);self.assertLess(out.index('provider destroyed before Lua teardown'),out.index('alpha stopped'));self.assertIn('alpha closed bytes=0 callbacks=0',out)
 def test_malformed_duplicate_unknown_and_stale_results(self):
  for mode in range(1,7):
   with self.subTest(mode=mode):self.package('alpha','c'+str(mode));self.assertIn('protocol fault retired',self.run_host('fault',native=True))
 def test_provider_fault_feedback_does_not_fault_lua(self):
  self.package('alpha','c3');out=self.run_host('intent=alpha:success','pump=150');self.assertIn('Provider protocol fault',out);self.assertRegex(out,r'page alpha status=1 pending=0 available=0');self.assertIn('alpha result failed',out)
 def test_absent_disabled_and_incompatible_feedback(self):
  for text in ['', 'disabled:alpha.native\n']:
   (self.mods/'providers-enabled.txt').write_text(text);out=self.run_host('intent=alpha:success','pump=10');self.assertIn('intent alpha success 0',out);self.assertIn('Provider absent or disabled',out)
  (self.mods/'providers-enabled.txt').write_text('alpha:alpha.native\n');p=self.mods/'alpha/provider.ini';p.write_text(p.read_text().replace('abi=2.0','abi=1.0'));out=self.run_host('pump=5');self.assertIn('ABI rejected',out);self.assertIn('Provider unavailable or incompatible',out)
 def test_typed_requests_fault_only_owner(self):
  for call in ["host.provider.submit('delayed','x')","host.provider.submit('delayed',10001)","host.provider.submit('success',{})","host.provider.submit('fail',1)","host.provider.submit('unknown')"]:
   p=self.mods/'alpha/main.lua';original=p.read_text();self.edit('alpha',"host.log('alpha ready')",call);out=self.run_host('intent=beta:success','pump=80');self.assertIn('alpha error event',out);self.assertIn('beta result ok',out);self.assertIn('alpha closed bytes=0 callbacks=0',out);p.write_text(original)
 def test_callback_oom_fault_and_budget(self):
  out=self.run_host('intent=alpha:success','intent=beta:success','pump=150',env={'MOD_FAIL_ID':'alpha','MOD_FAIL_PHASE':'provider','MOD_FAIL_AFTER':'0'});self.assertIn('alpha error provider',out);self.assertIn('beta result ok',out)
  p=self.mods/'alpha/main.lua';original=p.read_text();self.edit('alpha',"if e.kind=='result' then count=count+1", "if e.kind=='result' then error('completion fault');count=count+1");out=self.run_host('intent=alpha:success','intent=beta:success','pump=120');self.assertIn('completion fault',out);self.assertIn('beta result ok',out);p.write_text(original)
  self.edit('alpha',"if e.kind=='result' then count=count+1", "if e.kind=='result' then while true do end;count=count+1");out=self.run_host('intent=alpha:success','intent=beta:success','pump=120');self.assertIn('budget exceeded',out);self.assertIn('beta result ok',out)
 def test_ui_callback_oom(self):
  out=self.run_host('intent=alpha:success','intent=beta:success','pump=120',env={'MOD_FAIL_ID':'alpha','MOD_FAIL_PHASE':'action','MOD_FAIL_AFTER':'0'});self.assertIn('alpha error action',out);self.assertIn('beta result ok',out)
 def test_duplicate_action_schema_fault(self):
  self.edit('alpha','id="delayed",label="Run delayed action"','id="success",label="Run delayed action"');out=self.run_host('intent=beta:success','pump=100');self.assertIn('duplicate action ID',out);self.assertIn('beta result ok',out)
 def test_pressure_fairness_and_full_identity(self):
  for name in ['alpha','beta']:
   self.edit(name,"host.log('"+name+" ready')", "local state=host.provider.status();assert(#state.handle==50);for i=1,32 do local r=host.provider.submit('success');assert(type(r)=='string' and #r==50) end;local r,why=host.provider.submit('success');assert(r==nil and why=='busy')")
   self.edit(name,"host.log(host.id..' result '..e.result)","if count==32 then host.log(host.id..' completed 32') end")
  out=self.run_host('pump=500');self.assertIn('alpha completed 32',out);self.assertIn('beta completed 32',out);self.assertNotIn(' error ',out);self.assertRegex(out,r'page alpha status=1 pending=0');self.assertRegex(out,r'page beta status=1 pending=0')
 def test_provider_and_script_updates_without_relink(self):
  (self.settings/'alpha.ini').write_text('version=1\nstrength=i:41\n');host=BUILD/('lua'+EXE);before=hashlib.sha256(host.read_bytes()).hexdigest();out=self.run_host('intent=alpha:success','pump=100');self.assertIn('build=work1',out)
  for name in ['profile','logs']:(self.base/name).mkdir();(self.base/name/'sentinel').write_text(name)
  (self.settings/'sentinel').write_text('settings');preserved={str(p.relative_to(self.base)):p.read_bytes() for name in ['profile','logs','settings'] for p in (self.base/name).rglob('*') if p.is_file()}
  self.package('alpha','update');out=self.run_host('intent=alpha:success','pump=100');self.assertIn('build=work2',out);self.assertNotIn('script update ready',out);self.assertIn('alpha strength 41',out);self.assertEqual(before,hashlib.sha256(host.read_bytes()).hexdigest());self.edit('alpha',"host.log('alpha ready')","host.log('script update ready')");out=self.run_host('intent=alpha:success','pump=100');self.assertIn('build=work2',out);self.assertIn('script update ready',out);self.assertIn('alpha result ok',out);self.assertEqual(before,hashlib.sha256(host.read_bytes()).hexdigest())
  for name,data in preserved.items():self.assertEqual((self.base/name).read_bytes(),data)
  (BUILD/'update-proof.json').write_text(json.dumps({'host_before':before,'host_after':hashlib.sha256(host.read_bytes()).hexdigest(),'provider_before':'work1','provider_after':'work2','provider_only_update_observed':True,'script_only_update_observed':True,'profile_settings_logs_preserved':True},indent=2)+'\n')

 def test_capability_identity_preserves_high_bits(self):
  p=self.mods/'alpha/provider.ini';p.write_text(p.read_text().replace('required=1','required=9223372036854775809'));self.edit('alpha',"host.log('alpha ready')","assert(host.provider.status().capabilities=='8000000000000001')");self.assertNotIn(' error ',self.run_host('intent=alpha:success','pump=100'))
 def test_shutdown_does_not_accept_new_work(self):
  self.edit('alpha',"host.log('alpha stopped')","local r,why=host.provider.submit('success');assert(r==nil and why);assert(host.provider.status().message=='Provider stopped');host.log('alpha stopped')");out=self.run_host('intent=alpha:success','pump=80');self.assertNotIn(' error ',out);self.assertIn('alpha stopped',out)

 def test_service_abi_strict_versions_and_native_types(self):
  p=subprocess.run([str(BUILD/('abi-probe'+EXE)),str(self.mods)],capture_output=True,text=True,timeout=3);self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertEqual(p.stderr,'');self.assertIn('strict version',p.stdout)

 def test_shutdown_cancels_unstarted_delivery(self):
  out=self.run_host('intent=alpha:delayed','pump=10','cancel','pump=120');self.assertNotIn('alpha result',out);self.assertIn('provider destroyed before Lua teardown',out);self.assertIn('alpha closed bytes=0 callbacks=0',out)

 def test_restart_does_not_replay_actions(self):
  self.assertIn('alpha result ok',self.run_host('intent=alpha:success','pump=100'));out=self.run_host('pump=150');self.assertNotIn('alpha result',out);self.assertRegex(out,r'page alpha status=1 pending=0')
 def test_script_only_action_without_native_files(self):
  (self.mods/'providers-enabled.txt').unlink();shutil.rmtree(self.mods/'alpha/native');(self.mods/'alpha/provider.ini').unlink()
  (self.mods/'alpha/main.lua').write_text("return {init=function() host.settings.register{version=1,id='actions',title='Script actions',fields={},actions={{id='ping',label='Ping'}}} end,events={['host.started']=function() end,['ui.action']=function(e) assert(e.action=='ping');host.ui.status('Script-only action completed') end},shutdown=function() end}")
  out=self.run_host('intent=alpha:ping','pump=80');self.assertIn('intent alpha ping 1',out);self.assertIn('Script-only action completed',out);self.assertNotIn('alpha error',out)

 def test_failed_create_with_live_token_never_exposes_service(self):
  self.package('alpha','c7')
  out=self.run_host('intent=alpha:success','pump=50')
  self.assertIn('provider violated failed-create token contract',out)
  self.assertIn('intent alpha success 0',out)
  self.assertRegex(out,r'page alpha status=1 pending=0 available=0')
  self.assertNotIn('alpha result',out)
  self.assertIn('provider destroyed before Lua teardown',out)
  self.assertIn('alpha closed bytes=0 callbacks=0',out)
