from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class BaselineFixture(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.base=Path(self.temp.name);self.packages=self.base/'mods';shutil.copytree(ROOT/'tests/mods/ui-packages',self.packages);self.settings=self.base/'settings';self.settings.mkdir()

 def run_host(self,*args,env=None,timeout=3):
  e=os.environ.copy();e.update(env or {})
  return subprocess.run([str(ROOT/'build/mod-settings-test'),str(self.packages),str(self.settings),*args],env=e,capture_output=True,text=True,timeout=timeout,check=True).stdout
