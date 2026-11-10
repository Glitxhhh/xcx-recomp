from baseline_fix_fixture import *

class StartupFailures(BaselineFixture):
 def test_b2_startup_failures(self):
  for key in ['MOD_THREAD_FAIL','MOD_RUNTIME_FAIL']:
   with self.subTest(key=key):
    e=os.environ.copy();e[key]='1';p=subprocess.run([str(ROOT/'build/mod-startup-test'),str(self.packages)],env=e,capture_output=True,text=True,check=True,timeout=3);out=p.stdout
    self.assertEqual(p.stderr,'');self.assertIn('startup alpha 4 ',out);self.assertIn('startup beta 4 ',out);self.assertIn('startup disabled 2 ',out);self.assertIn('startup incompatible 3 ',out);self.assertNotIn(' log init:',out);self.assertNotIn(' ready init:',out);self.assertNotIn('callbacks=',out);self.assertIn('threads 0 0' if key=='MOD_THREAD_FAIL' else 'threads 1 1',out);self.assertIn('finished before shutdown 0' if key=='MOD_THREAD_FAIL' else 'finished before shutdown 1',out);self.assertIn('restart to retry',out)
