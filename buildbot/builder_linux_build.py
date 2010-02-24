from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from common import *

import os.path

def addArchiveStep(factory, archive_dir):
  factory.addStep(ShellCommand(
      command=['tar', 'czvf', os.path.join(archive_dir, 'build.tar.gz'), '.'],
      description='archiving build tree',
      descriptionDone='archive build tree'))

def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
  f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  # Build valgrind+tsan and install them to out/.
  f1.addStep(ShellCommand(command='cd third_party && ./update_valgrind.sh && ' +
                          './build_and_install_valgrind.sh `pwd`/../out',
                          description='building valgrind',
                          descriptionDone='build valgrind'))
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4', 'OFFLINE=',
                              'PIN_ROOT=/usr/local/google/pin',
                              'lo', 'ld'],
                     description='building tsan',
                     descriptionDone='build tsan'))
  f1.addStep(ShellCommand(command=['make', '-C', 'tsan', 'install',
                                   'VALGRIND_INST_ROOT=../out'],
                          description='installing tsan',
                          descriptionDone='install tsan'))

  os = 'linux'
  for bits in [32, 64]:
    for opt in [0, 1]:
      for static in [False, True]:
        addBuildTestStep(f1, os, bits, opt, static)


  addArchiveStep(f1, '/usr/local/google/Buildbot/archive')

  b1 = {'name': 'buildbot-linux-build',
        'slavename': 'bot7name',
        'builddir': 'full_linux_build',
        'factory': f1,
        }

  return b1
