import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
EXE=Path(os.environ.get('MOD_TEST_EXE',str(ROOT/'build/mod-test')))
def script(init='',event='',shutdown="host.log('cleanup')"):
    return "return {init=function() "+init+" end, events={['host.started']=function(event) "+event+" end}, shutdown=function() "+shutdown+" end}"

class Production(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'mods'; shutil.copytree(ROOT/'tests/mods/packages',self.root)
    def run_host(self,extra=(),fault=None):
        env={k:v for k,v in os.environ.items() if not k.startswith('MOD_FAIL_')}
        env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'; env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'
        if fault: env.update(fault)
        p=subprocess.run([str(EXE),str(self.root),*map(str,extra)],capture_output=True,text=True,timeout=5,env=env)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr); self.assertEqual(p.stderr,'')
        self.assertNotIn('accounting violation',p.stdout)
        for line in p.stdout.splitlines():
            if ' closed ' in line: self.assertIn('bytes=0 callbacks=0',line)
        return p.stdout
    def write(self,id,text):
        (self.root/id/'main.lua').write_text(text,encoding='utf-8')
    def add(self,id,text,requires=''):
        d=self.root/id; d.mkdir(exist_ok=True)
        (d/'mod.ini').write_text(f'id={id}\napi=1\nlanguage=lua55-basic-v1\nversion=0.1.0\nrequires={requires}\n',encoding='utf-8')
        self.write(id,text)
    def enable(self,*ids): (self.root/'enabled.txt').write_text('\n'.join(ids)+'\n',encoding='utf-8')
    def test_independent_event_error_copy_and_cleanup(self):
        out=self.run_host()
        for s in ['alpha initialized','beta initialized','alpha received host.started','beta received host.started','intentional fixture failure']:
            self.assertIn(s,out)
        self.assertNotIn('beta error',out)
        self.assertLess(out.index('beta shutdown'),out.index('alpha shutdown'))
    def test_absent_disabled_empty_and_blank_lines(self):
        (self.root/'enabled.txt').unlink(); self.assertEqual(self.run_host(),'no runtime\n')
        self.enable(); self.assertEqual(self.run_host(),'no runtime\n')
        self.add('disabled',"error('should never execute')")
        self.enable('beta'); out=self.run_host(); self.assertNotIn('disabled',out)
        (self.root/'enabled.txt').write_text('\n# comment\nbeta\n\n',encoding='utf-8')
        self.assertIn('beta initialized',self.run_host())
    def test_manifest_rejections_keep_unrelated(self):
        path=self.root/'alpha/mod.ini'; original=path.read_text()
        for bad in [original.replace('api=1','api=2'),original.replace('version=0.1.0','version=bad'),original+'api=1\n',original.replace('id=alpha','id=wrong')]:
            path.write_text(bad,encoding='utf-8'); out=self.run_host()
            self.assertIn('alpha:',out); self.assertIn('beta initialized',out)
    def test_missing_disabled_and_cyclic_dependencies(self):
        for dep in ['absent','disabled','alpha']:
            self.add('alpha',script(),dep); out=self.run_host()
            self.assertNotIn('alpha ready',out); self.assertIn('beta initialized',out)
        self.add('gamma',script(),'alpha'); self.add('alpha',script(),'gamma'); self.enable('alpha','gamma','beta')
        out=self.run_host(); self.assertIn('cycle',out); self.assertIn('beta initialized',out)
    def test_dependency_init_and_event_fault(self):
        self.add('gamma',script(event="host.log('gamma event')"),'alpha'); self.enable('gamma','beta','alpha')
        out=self.run_host(); self.assertIn('gamma error event: dependency unavailable',out); self.assertNotIn('gamma event',out)
        self.write('alpha',script(init="error('init failed')")); out=self.run_host()
        self.assertNotIn('gamma ready',out); self.assertIn('beta received host.started',out)
    def test_snapshots_and_script_only_restart_update(self):
        before=hashlib.sha256(EXE.read_bytes()).hexdigest()
        out=self.run_host([self.root/'alpha/main.lua'])
        self.assertIn('alpha initialized',out); self.assertNotIn('changed after validation',out)
        out=self.run_host(); self.assertIn('changed after validation',out)
        self.assertEqual(before,hashlib.sha256(EXE.read_bytes()).hexdigest())
    def test_bytecode_oversize_symlink_and_traversal(self):
        p=self.root/'alpha/main.lua'
        for data in [b'\x1bLua',b'x'*65537]:
            p.write_bytes(data); out=self.run_host(); self.assertIn('beta initialized',out); self.assertIn('alpha:',out)
        p.unlink(); p.symlink_to(self.root/'beta/main.lua'); out=self.run_host(); self.assertIn('alpha:',out)
        self.enable('../alpha','beta'); out=self.run_host(); self.assertIn('invalid ID',out); self.assertIn('beta initialized',out)
    def test_description_failure_cleanup_and_limits(self):
        for text in ["return {shutdown=function() host.log('partial cleanup') end,init=42}",script(shutdown="error('shutdown failed')"),script(event='while true do end'),script(event='local t={}; for i=1,1000000 do t[i]={i,i,i,i,i,i,i,i} end')]:
            self.write('alpha',text); out=self.run_host(); self.assertIn('alpha error',out); self.assertIn('beta received host.started',out)
        self.write('alpha',script(event='local t={'+','.join(['true']*10000)+'}'))
        out=self.run_host(); self.assertIn('beta received host.started',out)
    def test_memory_cap_from_bounded_library_call(self):
        self.write('alpha',script(event="local a='abcdefgh'; for i=1,30 do a=a..a end"))
        out=self.run_host()
        self.assertIn('not enough memory',out)
        self.assertIn('beta received host.started',out)
    def test_aggregate_and_log_bounds(self):
        costly=script(event='local n=0; for i=1,6000 do n=n+i end')
        for id in ['alpha','costb','costc']: self.add(id,costly)
        self.enable('alpha','costb','costc','beta'); out=self.run_host(); self.assertIn('aggregate dispatch budget exceeded',out)
        self.assertIn('beta received host.started',out)
        self.enable('alpha','beta'); self.write('alpha',script(event="for i=1,1000 do host.log('bounded') end"))
        self.assertLessEqual(self.run_host().count('bounded'),16)
        self.write('alpha',script(event="error('"+'x'*10000+"')")); out=self.run_host()
        self.assertLessEqual(len(next(l for l in out.splitlines() if 'alpha error event:' in l)),300)
    def test_allocation_failure_sweep(self):
        out=self.run_host()
        phases={phase:int(n) for phase,n in re.findall(r'alloc alpha (\w+) attempts=(\d+) denied=0',out)}
        for phase in ['state','setup','load','chunk','descriptor','init','event','shutdown']:
            self.assertIn(phase,phases)
            for after in range(phases[phase]):
                with self.subTest(phase=phase,after=after):
                    out=self.run_host(fault={'MOD_FAIL_ID':'alpha','MOD_FAIL_PHASE':phase,'MOD_FAIL_AFTER':str(after)})
                    self.assertIn('beta received host.started',out)
                    self.assertIn('denied=',out)
    @unittest.skipUnless(os.environ.get('MOD_LIFECYCLE_EXE'), 'Linux coordinator adapter not built')
    def test_thread_shutdown_and_self_stop(self):
        for text in [script(),script(init="host.log('self-exit-probe')")]:
            self.write('alpha',text)
            p=subprocess.run([os.environ['MOD_LIFECYCLE_EXE'],str(self.root)],capture_output=True,text=True,timeout=5)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertEqual(p.stderr,'')
            self.assertIn('lifecycle stopped idempotently',p.stdout)
            self.assertNotIn('accounting violation',p.stdout)
        self.enable()
        p=subprocess.run([os.environ['MOD_LIFECYCLE_EXE'],str(self.root)],capture_output=True,text=True,timeout=5)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertNotIn('ready init',p.stdout)
    def test_id_limit_and_duplicates(self):
        self.enable('alpha','alpha','beta'); out=self.run_host(); self.assertIn('duplicate enabled ID',out); self.assertIn('beta initialized',out)
        self.enable(*['id'+str(i) for i in range(17)]); self.assertIn('configuration rejected',self.run_host())

if __name__=='__main__': unittest.main()
