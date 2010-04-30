from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from buildbot.steps.transfer import FileDownload
from common import *

def generate(settings):
  f1 = factory.BuildFactory()

  addSetupTreeForTestsStep(f1)

#   wineprefix = '.wine';
  f1.addStep(FileDownload(mastersrc='public_html/win32tests/racecheck_unittest-windows-x86-O0.exe',
                          slavedest='unittest/bin/racecheck_unittest-windows-x86-O0.exe', mode=0755))
  f1.addStep(FileDownload(mastersrc='public_html/win32tests/racecheck_unittest-windows-x86-O0.pdb',
                          slavedest='.wine/drive_c/cygwin/opt/Buildbot/slave/full_windows_build/build/unittest/bin/racecheck_unittest-windows-x86-O0.pdb', mode=0755))

#$HOME/usr/valgrind-svn/bin/valgrind --max-mem-in-mb=5000 --suppressions=unittest/racecheck_unittest.supp --ignore=unittest/racecheck_unittest.ignore --announce-threads --trace-children=yes --trace-children-skip='*wineserver*,*winemenubuilder*,*services.exe*,*wineboot.exe*,*winedevice.exe*' --vex-iropt-precise-memory-exns=yes --tool=tsan --pure-happens-before=no --show-pc=yes  $WINE c://cygwin/home/eugenis/data-race-test/unittest/bin/racecheck_unittest-windows-x86-O0.exe

  # Run racecheck_unittest under Wine
  f1.addStep(ShellCommand(command='WINEPREFIX=`pwd`/.wine ./tsan.sh --max-mem-in-mb=5000 --suppressions=unittest/racecheck_unittest.supp ' +
                          '--ignore=unittest/racecheck_unittest.ignore --announce-threads ' +
                          '--trace-children=yes --trace-children-skip="*wineserver*,*winemenubuilder*,*services.exe*,*wineboot.exe*,*winedevice.exe*" ' +
                          '--vex-iropt-precise-memory-exns=yes --pure-happens-before=no --show-pc=yes --error_exitcode=1' +
                          '/usr/local/wine/bin/wine unittest/bin/racecheck_unittest-windows-x86-O0.exe',
             env={'WINE': '/usr/local/wine/bin/wine', 'WINESERVER': '/usr/local/wine/bin/wineserver'},
             description='testing',
             descriptionDone='test'))
  


  b1 = {'name': 'buildbot-wine',
        'slavename': 'bot6name',
        'builddir': 'full_wine',
        'factory': f1,
        }

  return [b1]
