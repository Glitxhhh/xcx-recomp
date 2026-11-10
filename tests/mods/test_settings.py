from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
EXE=Path(os.environ.get('MOD_SETTINGS_EXE',str(ROOT/'build/mod-settings-test')))

class Settings(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.base=Path(self.temp.name);self.packages=self.base/'mods';shutil.copytree(ROOT/'tests/mods/ui-packages',self.packages);self.settings=self.base/'mod-settings';self.settings.mkdir()
 def run_host(self,*args,fail=False):
  env=os.environ.copy();env.pop('MOD_SAVE_FAIL',None)
  if fail:env['MOD_SAVE_FAIL']='1'
  p=subprocess.run([str(EXE),str(self.packages),str(self.settings),*args],capture_output=True,text=True,env=env,timeout=5)
  self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertEqual(p.stderr,'');return p.stdout
 def values(self,out):return {(m,f):int(v) for m,f,t,v in re.findall(r'^field (\S+) (\S+) (\d+) (-?\d+)$',out,re.M)}
 def test_two_pages_changes_and_restart(self):
  out=self.run_host('--set=alpha:strength:41','--set=beta:size:7','--set=alpha:theme:1')
  self.assertIn('alpha changed strength 41',out);self.assertIn('beta changed size 7',out);self.assertIn('alpha changed theme bright',out)
  values=self.values(self.run_host());self.assertEqual(values['alpha','strength'],41);self.assertEqual(values['beta','size'],7);self.assertEqual(values['alpha','theme'],1)
 def test_status_and_disabled_never_runs(self):
  out=self.run_host();self.assertIn('view alpha 1 3',out);self.assertIn('view beta 1 3',out);self.assertIn('view disabled 2 0',out);self.assertIn('view incompatible 3 0',out);self.assertNotIn('must never execute',out)
 def test_stale_cancel_and_shutdown(self):
  for args in [('--stale=alpha:strength:99',),('--queue=alpha:strength:99','--close','--drain'),('--queue=alpha:strength:99','--shutdown','--drain')]:
   out=self.run_host(*args);self.assertEqual(self.values(out)['alpha','strength'],25)
  out=self.run_host('--shutdown','--set=alpha:strength:88');self.assertIn('submitted alpha strength 0',out)
 def test_coalescing(self):
  out=self.run_host('--queue=alpha:strength:20','--queue=alpha:strength:21','--queue=alpha:strength:22','--drain')
  self.assertEqual(self.values(out)['alpha','strength'],22);self.assertNotIn('alpha changed strength 20',out);self.assertIn('alpha changed strength 22',out)
 def test_invalid_stored_and_safe_failure(self):
  p=self.settings/'alpha.ini'
  for content in ['version=1\nstrength=i:500\n','version=1\nstrength=b:1\n','version=1\nstrength=i:7\nstrength=i:8\n','version=2\n','version=1\nstrength=\n']:
   p.write_text(content);out=self.run_host();self.assertEqual(self.values(out)['alpha','strength'],25);self.assertIn('Stored settings rejected',out)
  p.write_text('version=1\nstrength=i:40\n');before=p.read_bytes();out=self.run_host('--set=alpha:strength:41',fail=True)
  self.assertEqual(p.read_bytes(),before);self.assertIn('Save failed',out);self.assertEqual(self.values(out)['alpha','strength'],41)
 def test_removed_fields_defaults_enum_reorder(self):
  (self.settings/'alpha.ini').write_text('version=1\nremoved=i:8\nstrength=i:40\ntheme=e:bright\n')
  p=self.packages/'alpha/main.lua';s=p.read_text().replace('default=25','default=30').replace("{{id='calm',label='Calm'},{id='bright',label='Bright'}}","{{id='bright',label='Bright'},{id='calm',label='Calm'}}")
  p.write_text(s);out=self.run_host();self.assertEqual(self.values(out)['alpha','strength'],40);self.assertEqual(self.values(out)['alpha','theme'],0)
 def test_invalid_schema_and_field_ids(self):
  p=self.packages/'alpha/main.lua';original=p.read_text()
  cases=[original.replace("id='strength'","id='hints'"),original.replace("label='Show hints'","label='"+'x'*97+"'"),original.replace('version=1','version=2'),original.replace('min=0,max=100','min=100,max=0'),original.replace("type='bool'","type='widget'"),original.replace("id='bright'","id='calm'")]
  for source in cases:
   p.write_text(source);out=self.run_host();self.assertIn('view alpha 4 0',out);self.assertIn('view beta 1 3',out)
 def test_fault_while_open(self):
  p=self.packages/'alpha/main.lua';p.write_text(p.read_text().replace("host.log('alpha changed '","error('faulted handler');host.log('alpha changed '"))
  out=self.run_host('--set=alpha:strength:41','--set=alpha:strength:42');self.assertIn('view alpha 4 3',out);self.assertIn('submitted alpha strength 0',out);self.assertEqual(self.values(out)['alpha','strength'],41)
 def test_package_update_preserves_settings_without_rebuild(self):
  self.run_host('--set=beta:size:8');saved=(self.settings/'beta.ini').read_bytes();before=hashlib.sha256(EXE.read_bytes()).hexdigest()
  p=self.packages/'beta/main.lua';p.write_text(p.read_text().replace('beta ready','updated beta ready'))
  out=self.run_host();self.assertIn('updated beta ready',out);self.assertEqual(self.values(out)['beta','size'],8);self.assertEqual((self.settings/'beta.ini').read_bytes(),saved);self.assertEqual(hashlib.sha256(EXE.read_bytes()).hexdigest(),before)
 def test_queue_pressure(self):
  ids=[];commands=[]
  for m in range(5):
   id=f'queue{m}';ids.append(id);d=self.packages/id;d.mkdir();(d/'mod.ini').write_text(f'id={id}\nversion=0.1.0\napi=1\nlanguage=lua55-basic-v1\n')
   fields=','.join("{id='f%d',label='Field',type='bool',default=false}"%i for i in range(8))
   (d/'main.lua').write_text("return {init=function() host.settings.register{version=1,id='page',title='Queue',fields={"+fields+"}} end,events={['host.started']=function()end},shutdown=function()end}")
   commands+=['--queue=%s:f%d:1'%(id,i) for i in range(8)]
  (self.packages/'enabled.txt').write_text('\n'.join(ids));out=self.run_host(*commands,'--drain');self.assertEqual(out.count('submitted queue4'),8);self.assertIn('submitted queue4 f0 0',out)

if __name__=='__main__':unittest.main()
