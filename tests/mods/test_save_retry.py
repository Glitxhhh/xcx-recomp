from baseline_fix_fixture import *

class SaveRetry(BaselineFixture):
 def test_b3_same_value_retry(self):
  p=self.settings/'alpha.ini';p.write_text('version=1\nstrength=i:40\n')
  out=self.run_host('--save-fail','--set=alpha:strength:41','--dump','--set=alpha:strength:41','--dump','--save-ok','--set=alpha:strength:41')
  self.assertEqual(p.read_text().splitlines()[2],'strength=i:41');self.assertEqual(out.count('alpha changed strength 41'),1);self.assertIn('dirty alpha 1',out);self.assertIn('save alpha Saved',out);self.assertTrue(out.rfind('dirty alpha 0')>out.rfind('dirty alpha 1'))

 def test_b3_repeated_failure_preserves_file(self):
  p=self.settings/'alpha.ini';original='version=1\nstrength=i:40\n';p.write_text(original)
  out=self.run_host('--save-fail','--set=alpha:strength:41','--set=alpha:strength:41');self.assertEqual(p.read_text(),original);self.assertEqual(out.count('alpha changed strength 41'),1);self.assertIn('dirty alpha 1',out);self.assertIn('Save failed',out)

 def test_b3_rejected_settings_stay_clean(self):
  p=self.settings/'alpha.ini';original='version=1\nstrength=i:900\n';p.write_text(original)
  out=self.run_host();self.assertIn('Stored settings rejected',out);self.assertIn('dirty alpha 0',out);self.assertEqual(p.read_text(),original)

 def test_b3_clean_same_value_no_write_or_callback(self):
  p=self.settings/'alpha.ini';original='version=1\nstrength=i:40\n';p.write_text(original)
  out=self.run_host('--save-fail','--set=alpha:strength:40');self.assertEqual(p.read_text(),original);self.assertNotIn('Save failed',out);self.assertNotIn('alpha changed',out);self.assertIn('dirty alpha 0',out)
