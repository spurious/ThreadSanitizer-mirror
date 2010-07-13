from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from common import *

def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
  f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  # Build tsan and install it to out/.
  path_flags = ['OFFLINE=1',
                'VALGRIND_INST_ROOT=',
                'VALGRIND_ROOT=',
                'PIN_ROOT=']
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j2'] + path_flags + ['lo', 'ld'],
                     description='building offline tsan',
                     descriptionDone='build offline tsan'))

  # binaries = {
  #   'tsan/bin/tsan-amd64-linux-debug-self-contained.sh' : 'tsan-r%s-amd64-linux-debug-self-contained.sh',
  #   'tsan/bin/tsan-amd64-linux-self-contained.sh' : 'tsan-r%s-amd64-linux-self-contained.sh',
  #   'tsan/bin32/tsan-x86-linux-self-contained.sh' : 'tsan-r%s-x86-linux-self-contained.sh'}
  # addUploadBinariesStep(f1, binaries)

  f1.addStep(ShellCommand(command=["true"],
                          description='testing',
                          descriptionDone='test'))

  b1 = {'name': 'buildbot-jtsan',
        'slavename': 'bot6name',
        'builddir': 'full_jtsan',
        'factory': f1,
        }

  return [b1]
