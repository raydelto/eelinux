"""Render the actual PS2 TWM configuration in a disposable host Xvfb session."""
from pathlib import Path
import os, subprocess, tempfile, time
ROOT=Path(__file__).resolve().parents[1]
DEST=ROOT/'build/desktop';DEST.mkdir(parents=True,exist_ok=True)
processes=[]
with tempfile.TemporaryDirectory(prefix='ps2-twm-preview-') as temporary:
    work=Path(temporary)
    subprocess.run(['gcc','-O2','-Wall','-Wextra',str(ROOT/'desktop/ps2-desktop.c'),'-lX11',
                    '-o',str(work/'ps2-desktop')],check=True)
    theme=(ROOT/'rootfs/etc/skel/.twmrc').read_text().replace('/usr/share/ps2/twm',
                    str(ROOT/'rootfs/usr/share/ps2/twm'))
    (work/'.twmrc').write_text(theme)
    env={**os.environ,'DISPLAY':':97','HOME':str(work),'LC_ALL':'C'}
    try:
        processes.append(subprocess.Popen(['Xvfb',':97','-screen','0','800x600x24','-nolisten','tcp','-noreset'],
                             stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL))
        time.sleep(1)
        subprocess.run(['xrdb','-merge',str(ROOT/'rootfs/etc/X11/ps2.Xresources')],env=env,check=True)
        subprocess.run([str(work/'ps2-desktop')],env=env,check=True)
        with (DEST/'twm-preview.log').open('w') as log:
            processes.append(subprocess.Popen(['twm','-f',str(work/'.twmrc')],env=env,stdout=log,stderr=log))
            time.sleep(1)
            demo=work/'terminal.sh'
            demo.write_text("#!/bin/bash\nprintf '\\033[1;36mEmotion Engine / eeLinux\\033[0m\\n\\n"
              "A quiet desktop for your PlayStation 2.\\n\\n"
              "  F1          open the PS2 menu\\n"
              "  Alt+Enter   open a terminal\\n"
              "  Alt+Tab     cycle windows\\n"
              "  Alt+F4      close a window\\n\\n"
              "Choose Session > Return to console to exit.\\n\\n"
              "Theme preview - 800 x 600\\n\\n'\n"
              "export PS1='preview:~# '\nexec bash --noprofile --norc\n")
            processes.append(subprocess.Popen(['xterm','-title','Emotion Engine | Terminal',
                  '-geometry','66x18+36+68','-e','bash',str(demo)],env=env,
                  stdout=subprocess.DEVNULL,stderr=log))
            time.sleep(1)
            subprocess.run(['xdotool','mousemove','620','155','mousedown','1'],env=env,check=True)
            time.sleep(.3)
            subprocess.run(['xwd','-root','-silent','-out',str(work/'screen.xwd')],env=env,check=True)
            subprocess.run(['convert',str(work/'screen.xwd'),str(DEST/'twm-preview.png')],check=True)
            subprocess.run(['xdotool','mouseup','1'],env=env,check=True)
            if processes[1].poll() is not None:
                raise RuntimeError('TWM exited; inspect twm-preview.log')
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired: process.kill();process.wait()
print(DEST/'twm-preview.png')
