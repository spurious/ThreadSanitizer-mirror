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
                'GTEST_ROOT=',
                'VALGRIND_ROOT=',
                'PIN_ROOT=']
  f1.addStep(Compile(command=['make', '-C', 'tsan', 'l64d'] + path_flags,
                     description='building offline tsan',
                     descriptionDone='build offline tsan'))

  f1.addStep(Compile(command='cd third_party/java-thread-sanitizer && ant clean',
                     description='ant cleaning',
                     descriptionDone='ant clean'))

  f1.addStep(Compile(command='cd third_party/java-thread-sanitizer && ant download',
                     description='ant downloading',
                     descriptionDone='ant download'))

  f1.addStep(Compile(command='cd third_party/java-thread-sanitizer && ant',
                     description='ant building',
                     descriptionDone='ant build'))

  f1.addStep(Test(command='cd third_party/java-thread-sanitizer && ant buildbot-test',
                  description='jtsan testing',
                  descriptionDone='jtsan test'))

  # binaries = {
  #   'tsan/bin/tsan-amd64-linux-debug-self-contained.sh' : 'tsan-r%s-amd64-linux-debug-self-contained.sh',
  #   'tsan/bin/tsan-amd64-linux-self-contained.sh' : 'tsan-r%s-amd64-linux-self-contained.sh',
  #   'tsan/bin32/tsan-x86-linux-self-contained.sh' : 'tsan-r%s-x86-linux-self-contained.sh'}
  # addUploadBinariesStep(f1, binaries)

  b1 = {'name': 'buildbot-jtsan',
        'slavename': 'bot6name',
        'builddir': 'full_jtsan',
        'factory': f1,
        }

  return [b1]
