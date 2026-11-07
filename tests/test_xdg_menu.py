"""Desktop-entry precedence and literal Exec arguments for the native TWM menu."""
from pathlib import Path
import os,shlex,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class Menu(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.binary=Path(cls.tmp.name)/'menu'
  subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',str(ROOT/'desktop/ps2-xdg-menu.c'),'-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_precedence_locale_and_visibility(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);user=root/'user';system=root/'system';user.mkdir();system.mkdir()
   for directory,name,text in ((system,'clock','Name=Clock\nExec=/bin/true\n'),
      (user,'clock','Hidden=true\nName=Hide Clock\nExec=/bin/true\n'),
      (system,'good','Name=Default\nName[pt]=Relogio\nExec=/bin/true\n'),
      (system,'missing','Name=Missing\nTryExec=no-such-ps2-program\nExec=/bin/true\n'),
      (system,'other','Name=Other\nOnlyShowIn=GNOME;\nExec=/bin/true\n')):
    (directory/(name+'.desktop')).write_text('[Desktop Entry]\nType=Application\n'+text)
   text=subprocess.check_output([str(self.binary),str(user),str(system)],env=dict(os.environ,LANG='pt_PT.UTF-8'),text=True)
   self.assertIn('Relogio',text)
   for hidden in ('Clock','Missing','Other','Default'):self.assertNotIn(hidden,text)
 def test_exec_arguments_are_literal(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);marker=root/'injected';target=root/'args.txt';program=root/'print args'
   program.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "'+str(target)+'"\n');program.chmod(0o755)
   literal='$(touch '+str(marker)+')'
   (root/'literal.desktop').write_text('[Desktop Entry]\nType=Application\nName=Quoted Name\nExec="'+str(program)+'" "'+literal+'" %c %% %f\n')
   output=subprocess.check_output([str(self.binary),str(root)],text=True)
   line=next(line for line in output.splitlines() if 'f.exec' in line)
   command=shlex.split(line)[2]
   subprocess.run(['/bin/sh','-c',command+' wait'],check=True)
   self.assertFalse(marker.exists());self.assertEqual(target.read_text().splitlines(),[literal,'Quoted Name','%'])
 def test_cache_reuses_menu_and_invalidates_entry_edits_additions_and_removals(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);apps=root/'apps';apps.mkdir();cache=root/'menu.cache'
   entry=apps/'one.desktop'
   entry.write_text('[Desktop Entry]\nType=Application\nName=First\nExec=/bin/true\n')
   def render():return subprocess.check_output([str(self.binary),'--cache',str(cache),str(apps)],text=True)
   first=render();metadata=cache.stat()
   self.assertIn('First',first);self.assertEqual(render(),first)
   self.assertEqual((cache.stat().st_ino,cache.stat().st_mtime_ns),(metadata.st_ino,metadata.st_mtime_ns))
   entry.write_text('[Desktop Entry]\nType=Application\nName=Edited\nExec=/bin/true\n')
   self.assertIn('Edited',render());self.assertNotIn('First',render())
   (apps/'two.desktop').write_text('[Desktop Entry]\nType=Application\nName=Second\nExec=/bin/true\n')
   self.assertIn('Second',render());entry.unlink();self.assertNotIn('Edited',render())
 def test_cache_tracks_executable_permissions_and_new_higher_priority_entries(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);user=root/'user';system=root/'system';system.mkdir();cache=root/'menu.cache';program=root/'tool'
   program.write_text('#!/bin/sh\nexit 0\n');program.chmod(0o644)
   (system/'tool.desktop').write_text('[Desktop Entry]\nType=Application\nName=Tool\nExec='+str(program)+'\n')
   def render():return subprocess.check_output([str(self.binary),'--cache',str(cache),str(user),str(system)],text=True)
   self.assertNotIn('"Tool"',render());program.chmod(0o755);self.assertIn('"Tool"',render())
   user.mkdir();(user/'tool.desktop').write_text('[Desktop Entry]\nType=Application\nHidden=true\n')
   self.assertNotIn('"Tool"',render());program.chmod(0o644)
   (user/'tool.desktop').unlink();self.assertNotIn('"Tool"',render())
 def test_cache_context_corruption_and_unwritable_location_fall_back(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);apps=root/'apps';apps.mkdir();cache=root/'menu.cache'
   (apps/'one.desktop').write_text('[Desktop Entry]\nType=Application\nName=Default\nName[pt]=Traduzido\nExec=/bin/true\n')
   def render(lang='C',path=cache):
    return subprocess.check_output([str(self.binary),'--cache',str(path),str(apps)],
     env=dict(os.environ,LC_ALL=lang),text=True)
   self.assertIn('Default',render());self.assertIn('Traduzido',render('pt_PT.UTF-8'))
   cache.write_bytes(b'TWMENU1\0truncated');expected=render();self.assertIn('Default',expected)
   data=cache.read_bytes();cache.write_bytes(data[:-3]+b'Q'+data[-2:]);self.assertEqual(render(),expected)
   self.assertIn('Default',render(path=root/'absent'/'menu.cache'))
if __name__=='__main__':unittest.main()
