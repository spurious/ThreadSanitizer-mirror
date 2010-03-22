from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand

from common import *

import os.path

def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
  f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  # Get valgrind build.
  f1.addStep(ShellCommand(command=['wget', 'http://codf220/full_valgrind/valgrind_build.tar.gz'],
                          description='getting valgrind build',
                          descriptionDone='get valgrind build'))

  addExtractStep(f1, 'valgrind_build.tar.gz')

  # Build tsan and install it to out/.
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4', 'OFFLINE=',
                              'VALGRIND_INST_ROOT=../out',
                              'VALGRIND_ROOT=../third_party/valgrind',
                              'PIN_ROOT=../../../../third_party/pin',
                              'lo', 'ld'],
                     description='building tsan',
                     descriptionDone='build tsan'))

  # Build self-contained tsan binaries.
  f1.addStep(ShellCommand(command=['tsan_binary/mk-self-contained-valgrind.sh',
                                   'out', 'tsan', 'tsan.sh'],
                          description='packing self-contained tsan',
                          descriptionDone='pack self-contained tsan'))

  f1.addStep(ShellCommand(command=['tsan_binary/mk-self-contained-valgrind.sh',
                                   'out', 'tsan-debug', 'tsan-debug.sh'],
                          description='packing self-contained tsan (debug)',
                          descriptionDone='pack self-contained tsan (debug)'))

  # Build 32-bit tsan and install it to out32/.
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4', 'OFFLINE=',
                              'OUTDIR=bin32',
                              'VALGRIND_INST_ROOT=../out32',
                              'VALGRIND_ROOT=../third_party/valgrind32',
                              'PIN_ROOT=',
                              'l32o', 'l32d'],
                     description='building 32-bit tsan',
                     descriptionDone='build 32-bit tsan'))

  f1.addStep(ShellCommand(command=['tsan_binary/mk-self-contained-valgrind.sh',
                                   'out32', 'tsan', 'tsan32.sh'],
                          description='packing self-contained tsan (32-bit)',
                          descriptionDone='pack self-contained tsan (32-bit)'))


  binaries = {
    'tsan.sh' : 'tsan-r%s-linux-amd64.sh',
    'tsan-debug.sh' : 'tsan-r%s-linux-amd64-debug.sh',
    'tsan32.sh' : 'tsan-r%s-linux-x86.sh'};
  addUploadBinariesStep(f1, binaries)

  os = 'linux'
  for bits in [32, 64]:
    for opt in [0, 1]:
      for static in [False, True]:
        addBuildTestStep(f1, os, bits, opt, static)


  addArchiveStep(f1, '../full_build.tar.gz')

  b1 = {'name': 'buildbot-linux-build',
        'slavename': 'bot5name',
        'builddir': 'full_linux_build',
        'factory': f1,
        }

  return [b1]
