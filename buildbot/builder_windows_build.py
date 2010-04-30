from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from buildbot.steps.transfer import FileUpload
from common import *


def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
  f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  # Build tests.
  addBuildTestStep(f1, 'windows', 32, 0, 0, more_args=['EXTRA_CXXFLAGS=-DWINE'])

  f1.addStep(FileUpload(slavesrc='unittest/bin/racecheck_unittest-windows-x86-O0.exe',
                        masterdest='public_html/win32tests/racecheck_unittest-windows-x86-O0.exe', mode=0755))
  f1.addStep(FileUpload(slavesrc='unittest/bin/racecheck_unittest-windows-x86-O0.pdb',
                        masterdest='public_html/win32tests/racecheck_unittest-windows-x86-O0.pdb', mode=0755))

  b1 = {'name': 'buildbot-windows-build',
        'slavename': 'bot2name',
        'builddir': 'full_windows_build',
        'factory': f1,
        }

  return [b1]
