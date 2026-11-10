from baseline_fix_fixture import *
import socket

class SettingsFiles(BaselineFixture):
 def test_b1_missing_and_ordinary(self):
  out=self.run_host();self.assertNotIn('Stored settings',out)
  (self.settings/'alpha.ini').write_text('version=1\nstrength=i:42\n');self.assertIn('field alpha strength 2 42',self.run_host())

 def test_b1_unsafe_files(self):
  p=self.settings/'alpha.ini'
  for kind in ['fifo','directory','symlink','socket']:
   with self.subTest(kind=kind):
    sock=None
    if kind=='fifo':os.mkfifo(p)
    elif kind=='directory':p.mkdir()
    elif kind=='symlink':p.symlink_to(self.packages/'alpha/main.lua')
    else:sock=socket.socket(socket.AF_UNIX);sock.bind(str(p))
    try:
     out=self.run_host(timeout=1);self.assertIn('Stored settings unreadable or unsafe',out);self.assertIn('field alpha strength 2 25',out)
    finally:
     if sock:sock.close()
     if p.is_dir() and not p.is_symlink():p.rmdir()
     else:p.unlink()

 def test_b1_oversize_and_read_errors(self):
  p=self.settings/'alpha.ini';p.write_bytes(b'x'*4097);self.assertIn('Stored settings',self.run_host());self.assertEqual(len(p.read_bytes()),4097)
  p.write_text('version=1\nstrength=i:42\n')
  for key in ['MOD_READ_ERROR','MOD_READ_SHORT','MOD_OPEN_ERROR','MOD_STAT_SPECIAL']:
   out=self.run_host(env={key:'1'});self.assertIn('Stored settings unreadable or unsafe',out);self.assertIn('field alpha strength 2 25',out)

 def test_b1_missing_root_and_symlink_root(self):
  self.settings.rmdir();self.assertNotIn('Stored settings',self.run_host());self.assertFalse(self.settings.exists())
  target=self.base/'other';target.mkdir();self.settings.symlink_to(target,target_is_directory=True)
  self.assertIn('Stored settings unreadable or unsafe',self.run_host());self.assertEqual(list(target.iterdir()),[])
