from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,tempfile,time,unittest
ROOT=Path(__file__).resolve().parents[2];BUILD=Path(os.environ['PROVIDER_BUILD']);WIN=sys.platform=='win32';EXT='.dll' if WIN else '.so';TARGET='windows-x86_64' if WIN else 'linux-x86_64';HASHKEY='windows-sha256' if WIN else 'linux-sha256'
class Loader(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.base=Path(self.temp.name);self.mods=self.base/'mods';shutil.copytree(ROOT/'tests/mods/ui-packages',self.mods);self.marker=self.base/'markers';self.settings=self.base/'settings';self.settings.mkdir()
 def package(self,name='alpha',binary='c0',required=0):
  lib=self.mods/name/'native'/TARGET/('provider'+EXT);lib.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(BUILD/(binary+EXT),lib)
  self.manifest(name,required);return lib
 def manifest(self,name='alpha',required=0):
  lib=self.mods/name/'native'/TARGET/('provider'+EXT);h=hashlib.sha256(lib.read_bytes()).hexdigest()
  (self.mods/name/'provider.ini').write_text(f'schema=1\nid={name}.native\nowner={name}\nabi=1.0\nrequired={required}\n{HASHKEY}={h}\n')
 def activate(self,text='alpha:alpha.native\n'):(self.mods/'providers-enabled.txt').write_text(text)
 def markers(self,all=False):
  text=self.marker.read_text() if self.marker.exists() else '';return text if all else text.replace('host before-exit\n','')
 def host(self,mode=None,env=None,expected=0,lua=False,timeout=5):
  e=os.environ.copy();e['XMP_MARKER']=str(self.marker);e.update(env or {});exe=BUILD/(('lua-host' if lua else 'host')+('.exe' if WIN else ''))
  cmd=[str(exe),str(self.mods)]+([str(self.settings)] if lua else [mode] if mode else [])
  p=subprocess.run(cmd,env=e,capture_output=True,text=True,timeout=timeout);self.assertEqual(p.returncode,expected,p.stdout+p.stderr);self.assertEqual(p.stderr,'');return p.stdout
 def test_script_absent_disabled_and_valid(self):
  self.package();out=self.host(lua=True);self.assertIn('alpha ready init',out);self.assertNotIn('constructor',self.markers())
  for owner in ['disabled','absent','incompatible']:
   self.activate(owner+':alpha.native\n');self.host();self.assertEqual(self.markers(),'')
  self.activate();out=self.host();self.assertIn('tokens 1 stopped 1',out);self.assertIn('alpha.native destroy',self.markers())
 def test_two_independent_c_cpp(self):
  self.package();self.package('beta','cpp');self.activate('alpha:alpha.native\nbeta:beta.native\n');out=self.host('tokens');self.assertIn('tokens 2 stopped 1',out)
  for name in ['alpha','beta']:self.assertEqual(self.markers().count(name+'.native create'),1);self.assertEqual(self.markers().count(name+'.native destroy'),1)
 def test_invalid_duplicate_activation_no_constructors(self):
  self.package()
  for text in ['alpha:alpha.native\nalpha:alpha.native\n','../alpha:alpha.native\n','alpha\n','alpha:alpha.native\nbeta:alpha.native\n','alpha:alpha.native\x00\n']:
   self.activate(text);self.host();self.assertEqual(self.markers(),'')
 def test_metadata_and_hash_preflight(self):
  self.package();self.activate();p=self.mods/'alpha/provider.ini';original=p.read_text()
  for bad in [original.replace('owner=alpha','owner=beta'),original.replace('abi=1.0','abi=3.0'),original+'schema=1\n',original.replace('schema=1','schema=2'),original+'path=../../evil.so\n',original.replace('required=0','required=-1'),original.replace('alpha.native','wrong.native')]:
   p.write_text(bad);self.host();self.assertEqual(self.markers(),'')
  p.write_text(original.replace(HASHKEY+'=',HASHKEY+'=00'));self.host();self.assertEqual(self.markers(),'')
 def test_unsafe_paths_and_special_files(self):
  lib=self.package();self.activate();outside=self.base/('outside'+EXT);shutil.copy2(lib,outside);lib.unlink()
  try:lib.symlink_to(outside)
  except OSError:self.skipTest('symlink privileges unavailable')
  self.host();self.assertEqual(self.markers(),'');lib.unlink();lib.mkdir();self.host();self.assertEqual(self.markers(),'');lib.rmdir()
  if not WIN:os.mkfifo(lib);self.host(timeout=2);self.assertEqual(self.markers(),'');lib.unlink()
  lib.parent.rename(lib.parent.with_name('saved'));lib.parent.symlink_to(lib.parent.with_name('saved'),target_is_directory=True);self.host();self.assertEqual(self.markers(),'')
 def test_wrong_binary_and_dependency_preflight(self):
  lib=self.package();self.activate();lib.write_bytes(b'not an image');self.manifest();self.host();self.assertEqual(self.markers(),'')
  if not WIN:
   for name in ['dependency','rpath']:
    shutil.copy2(BUILD/(name+EXT),lib);self.manifest();self.host();self.assertEqual(self.markers(),'')
   data=bytearray((BUILD/('c0'+EXT)).read_bytes());data[18:20]=(183).to_bytes(2,'little');lib.write_bytes(data);self.manifest();self.host();self.assertEqual(self.markers(),'')
 def test_abi_rejection_retains_mapping(self):
  for binary in ['missing','c1','c2','c3','c4','c5','c6','c7','c13','c14','c15']:
   self.marker.unlink(missing_ok=True);self.package(binary=binary,required=1);self.activate();out=self.host();self.assertIn('tokens 0 stopped 1',out);m=self.markers(all=True);self.assertEqual(m.count('constructor'),1);self.assertNotIn(' create',m);self.assertEqual(m.count('destructor'),1);self.assertLess(m.index('host before-exit'),m.index('destructor'));self.assertIn('retained',out)
 def test_create_failure_rolls_back(self):
  for binary in ['c8','c9']:
   self.marker.unlink(missing_ok=True);self.package(binary=binary);self.activate();out=self.host();self.assertIn('tokens 0 stopped 1',out);self.assertNotIn(' destroy',self.markers())
   if binary=='c9':self.assertIn('rollback',self.markers())
 def test_delayed_stop_and_generation_tokens(self):
  self.package(binary='c10');self.activate();out=self.host('generations');self.assertIn('old generation rejected',out);self.assertEqual(self.markers().count(' destroy'),2)
 def test_timeout_retains_live_state(self):
  self.package(binary='c12');self.activate();out=self.host(expected=70);self.assertIn('cleanup timeout',out);self.assertIn('refused free',out);self.assertNotIn(' destroy',self.markers());self.assertNotIn('destructor',self.markers())
 def test_hung_cleanup_external_deadline(self):
  self.package(binary='c11');self.activate()
  with self.assertRaises(subprocess.TimeoutExpired):self.host(timeout=.5)
  self.assertIn('hung',self.markers());self.assertNotIn(' destroy',self.markers())
 def test_provider_cleanup_before_lua(self):
  self.package();self.activate();out=self.host(lua=True);self.assertLess(out.index('provider destroyed before Lua teardown'),out.index('alpha stopped'));self.assertIn('alpha closed bytes=0 callbacks=0',out)
  self.marker.unlink();out=self.host(lua=True,env={'MOD_FAIL_ID':'alpha','MOD_FAIL_PHASE':'setup','MOD_FAIL_AFTER':'0'});self.assertIn('alpha error setup',out);self.assertLess(out.index('provider destroyed before Lua teardown'),out.index('alpha closed bytes=0'));self.assertIn(' destroy',self.markers())
 def test_update_without_relink(self):
  lib=self.package();self.activate();exe=BUILD/('host'+('.exe' if WIN else ''));before=hashlib.sha256(exe.read_bytes()).hexdigest();out=self.host();self.assertIn('build=c1',out)
  protected={};
  for name in ['profile/sentinel','logs/sentinel','settings.ini']:
   p=self.base/name;p.parent.mkdir(exist_ok=True);p.write_text(name);protected[name]=p.read_bytes()
  shutil.copy2(BUILD/('update'+EXT),lib);self.manifest();self.assertIn('build=c2',self.host());self.assertEqual(before,hashlib.sha256(exe.read_bytes()).hexdigest())
  for name,data in protected.items():self.assertEqual((self.base/name).read_bytes(),data)
  (BUILD/'update-proof.json').write_text(json.dumps({'host_sha256_before':before,'host_sha256_after':hashlib.sha256(exe.read_bytes()).hexdigest(),'provider_build_before':'c1','provider_build_after':'c2','unrelated_files_preserved':True},indent=2)+'\n')
 @unittest.skipIf(WIN,'ELF environment policy')
 def test_loader_environment_rejected(self):
  self.package();self.activate();self.host(env={'LD_LIBRARY_PATH':str(self.base)});self.assertEqual(self.markers(),'')

 def test_capacity_preflight_no_constructors(self):
  names=['alpha','beta','gamma','delta','epsilon']
  for name in names:
   if not (self.mods/name).exists():
    shutil.copytree(self.mods/'alpha',self.mods/name);p=self.mods/name/'mod.ini';p.write_text(p.read_text().replace('id=alpha','id='+name))
   self.package(name)
  (self.mods/'enabled.txt').write_text('\n'.join(names)+'\n');self.activate(''.join(n+':'+n+'.native\n' for n in names));self.assertIn('more than four',self.host());self.assertEqual(self.markers(),'')

 def test_direct_abi_errors_and_idempotency(self):
  self.package();e=os.environ.copy();e['XMP_MARKER']=str(self.marker);p=subprocess.run([str(BUILD/('abi-probe'+('.exe' if WIN else ''))),str(self.mods)],env=e,capture_output=True,text=True,timeout=3);self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertIn('ABI probe passed',p.stdout)
 @unittest.skipIf(WIN,'Linux coordinator test adapter uses pthreads; native Windows standalone lifecycle is separate')
 def test_coordinator_concurrent_shutdown(self):
  self.package();self.activate();e=os.environ.copy();e['XMP_MARKER']=str(self.marker);p=subprocess.run([str(BUILD/'coordinator-host'),str(self.mods)],env=e,capture_output=True,text=True,timeout=5);self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertEqual(p.stderr,'');self.assertEqual(self.markers().count(' destroy'),1);self.assertLess(p.stdout.index('provider destroyed before Lua teardown'),p.stdout.index('alpha stopped'));self.assertIn('lifecycle stopped idempotently',p.stdout)
 def test_unsafe_activation_and_metadata(self):
  self.package();self.activate();p=self.mods/'providers-enabled.txt';p.write_bytes(b'x'*2049);self.host();self.assertEqual(self.markers(),'')
  self.activate();meta=self.mods/'alpha/provider.ini';original=meta.read_bytes();meta.write_bytes(original+b'x'*4096);self.host();self.assertEqual(self.markers(),'');meta.write_bytes(original)
  if not WIN:
   outside=self.base/'meta';shutil.copy2(meta,outside);meta.unlink();meta.symlink_to(outside);self.host();self.assertEqual(self.markers(),'');meta.unlink();meta.write_bytes(original)
   outside=self.base/'activation';shutil.copy2(p,outside);p.unlink();p.symlink_to(outside);self.host();self.assertEqual(self.markers(),'')

 @unittest.skipIf(WIN,'Windows reparse privilege and path tests are a native release gate')
 def test_symlink_root_with_trailing_slash(self):
  self.package();self.activate();link=self.base/'root-link';link.symlink_to(self.mods,target_is_directory=True);e=os.environ.copy();e['XMP_MARKER']=str(self.marker)
  p=subprocess.run([str(BUILD/'host'),str(link)+'/'],env=e,capture_output=True,text=True,timeout=3);self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertEqual(self.markers(),'')

 def test_no_replacement_until_process_exit(self):
  lib=self.package();self.activate();e=os.environ.copy();e['XMP_MARKER']=str(self.marker)
  child=subprocess.Popen([str(BUILD/('host'+('.exe' if WIN else ''))),str(self.mods),'hold'],env=e,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   lines=[]
   while True:
    line=child.stdout.readline();self.assertTrue(line);lines.append(line)
    if 'ready for restart attempt' in line:break
   replacement=lib.with_name('replacement'+EXT);shutil.copy2(BUILD/('update'+EXT),replacement)
   try:os.replace(replacement,lib)
   except PermissionError:
    # Windows keeps the mapped image locked; this is already the required no-replacement behavior.
    self.assertTrue(WIN)
   else:self.manifest()
   out,err=child.communicate('x',timeout=5);self.assertEqual(child.returncode,0,err);self.assertEqual(err,'')
   if not WIN:self.assertIn('replacement active 0',out)
   self.assertEqual(self.markers().count('constructor'),1)
  finally:
   if child.poll() is None:child.kill();child.communicate()

 def test_owner_fault_retires_provider_before_other_delivery(self):
  self.package();self.activate();p=self.mods/'alpha/main.lua';p.write_text(p.read_text().replace("host.log('alpha ready')","error('fixture owner event fault')"));out=self.host(lua=True)
  self.assertIn('fixture owner event fault',out);self.assertLess(out.index('provider destroyed before Lua teardown'),out.index('beta ready\n') if 'beta ready\n' in out else out.index('beta stopped'));self.assertEqual(self.markers().count(' destroy'),1)

 def test_schema2_host_game_preflight(self):
  self.package();self.activate();p=self.mods/'alpha/provider.ini';original=p.read_text().replace('schema=1','schema=2')+'host=xcx-mods-service-2.0\ngame=2.0.0\n'
  for text,game in [(original,'1.4.3'),(original.replace('service-2.0','service-9.0'),'2.0.0'),(original.replace('game=2.0.0\n',''),'2.0.0'),(original,'')]:
   p.write_text(text);out=self.host(env={'XMP_TEST_GAME':game});self.assertEqual(self.markers(),'');self.assertIn('preflight rejected',out)
  p.write_text(original);self.host(env={'XMP_TEST_GAME':'2.0.0'});self.assertIn('alpha.native destroy',self.markers())
