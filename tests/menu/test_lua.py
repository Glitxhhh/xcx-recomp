from pathlib import Path
import os,subprocess,tempfile,unittest
class Registration(unittest.TestCase):
 def run_mod(self,code,beta='',unsupported=False,steps=(),event=""):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);mods=root/'mods';settings=root/'settings';mods.mkdir();settings.mkdir();(mods/'enabled.txt').write_text('alpha\n'+('beta\n' if beta else ''))
   for owner,extra in [('alpha',code),('beta',beta)]:
    if owner=='beta' and not beta:continue
    p=mods/owner;p.mkdir();(p/'mod.ini').write_text(f'id={owner}\nname={owner}\nversion=0.1.0\napi=1\nlanguage=lua55-basic-v1\nrequires=\n')
    (p/'main.lua').write_text("return {init=function() host.settings.register{version=1,id='page',title='Page',fields={{id='flag',label='Flag',type='bool',default=false}}};"+extra+" end,events={['host.started']=function() "+event+" end,['ui.action']=function() end},shutdown=function() end}")
   env=os.environ.copy()
   if unsupported:env['MENU_TEST_UNSUPPORTED']='1'
   r=subprocess.run([os.environ['MENU_TEST_EXE'],str(mods),str(settings),*steps],env=env,capture_output=True,text=True,timeout=4);self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(r.stderr,'');self.assertIn('alpha closed bytes=0 callbacks=0',r.stdout);return r.stdout
 def call(self,fields=''):return 'host.menu.register{version=1,location="online",id="entry",label="Example",page="page"'+fields+'}'
 def test_valid_owned_registration(self):self.assertIn('registered alpha entry page Example',self.run_mod('assert('+self.call()+')'))
 def test_capacity_conflict(self):
  out=self.run_mod('assert('+self.call()+')','local ok,why='+self.call()+';assert(not ok and why=="full")');self.assertIn('registered alpha',out);self.assertNotIn('error',out)
 def test_missing_page_unsupported_location_version(self):
  for call,reason in [(self.call().replace('page="page"','page="missing"'),'unavailable'),(self.call().replace('online','arbitrary'),'unsupported'),(self.call().replace('version=1','version=2'),'unsupported'),(self.call().replace('version=1','version=4294967297'),'unsupported')]:
   out=self.run_mod('local ok,why='+call+';assert(not ok and why=="'+reason+'")');self.assertIn('unregistered',out);self.assertNotIn(' error ',out)
 def test_adapter_unavailable(self):self.assertNotIn(' error ',self.run_mod('local ok,why='+self.call()+';assert(not ok and why=="unsupported")',unsupported=True))
 def test_invalid_label_and_ids(self):
  for call in [self.call().replace('Example','<tag>'),self.call().replace('Example','x'*33),self.call().replace('entry','../bad'),self.call().replace('Example','bad\\nline')]:
   out=self.run_mod(call);self.assertIn('alpha error init',out);self.assertIn('unregistered',out)
 def test_owner_fault_quarantines_registration(self):self.assertIn('unregistered',self.run_mod('assert('+self.call()+');error("owner failed")'))

 def test_complete_first_mod_example(self):
  import shutil
  root=Path(__file__).resolve().parents[2]
  with tempfile.TemporaryDirectory() as tmp:
   t=Path(tmp);mods=t/'mods';shutil.copytree(root/'examples/mods/hello',mods/'hello');(mods/'enabled.txt').write_text('hello\n');settings=t/'settings';settings.mkdir()
   r=subprocess.run([os.environ['MENU_TEST_EXE'],str(mods),str(settings),'pump=20','intent=hello:greet','pump=50'],capture_output=True,text=True,timeout=4);self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(r.stderr,'');self.assertIn('Hello! Clicks this run: 1',r.stdout);self.assertIn('hello closed bytes=0 callbacks=0',r.stdout)

 def test_registration_after_init_unavailable(self):
  out=self.run_mod('',event='local ok,why='+self.call()+';assert(not ok and why=="unavailable")');self.assertIn('unregistered',out);self.assertNotIn(' error ',out)

 def test_first_mod_with_documented_menu_registration(self):
  import re,shutil
  root=Path(__file__).resolve().parents[2]
  with tempfile.TemporaryDirectory() as tmp:
   t=Path(tmp);mods=t/'mods';shutil.copytree(root/'examples/mods/hello',mods/'hello');(mods/'enabled.txt').write_text('hello\n');settings=t/'settings';settings.mkdir()
   snippet=re.search(r'```lua\n(.*?)```',(root/'docs/mods/03-menu-registration.md').read_text(),re.S)[1]
   source=mods/'hello/main.lua';source.write_text(source.read_text().replace("host.ui.status('Choose Say hello.')",snippet+"\n  host.ui.status('Choose Say hello.')"))
   r=subprocess.run([os.environ['MENU_TEST_EXE'],str(mods),str(settings)],capture_output=True,text=True,timeout=4)
   self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(r.stderr,'');self.assertNotIn(' error ',r.stdout);self.assertIn('registered hello workshop preferences Workshop',r.stdout);self.assertIn('hello closed bytes=0 callbacks=0',r.stdout)

 def test_complete_provider_actions_reference_example(self):
  import re
  root=Path(__file__).resolve().parents[2]
  with tempfile.TemporaryDirectory() as tmp:
   t=Path(tmp);mods=t/'mods';package=mods/'hello';package.mkdir(parents=True);(mods/'enabled.txt').write_text('hello\n');settings=t/'settings';settings.mkdir()
   (package/'mod.ini').write_text((root/'examples/mods/hello/mod.ini').read_text())
   (package/'main.lua').write_text(re.search(r'```lua\n(.*?)```',(root/'docs/provider-actions.md').read_text(),re.S)[1])
   r=subprocess.run([os.environ['MENU_TEST_EXE'],str(mods),str(settings)],capture_output=True,text=True,timeout=4)
   self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(r.stderr,'');self.assertNotIn(' error ',r.stdout);self.assertIn('hello ready init',r.stdout);self.assertIn('hello closed bytes=0 callbacks=0',r.stdout)
