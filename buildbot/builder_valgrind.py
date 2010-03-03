from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from buildbot.steps.transfer import FileDownload
from common import *

import os.path

def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
  f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  f1.addStep(FileDownload(mastersrc='~/valgrind_source.tar.bz2',
                          slavedest='third_party/valgrind_source.tar.bz2'))

  f1.addStep(ShellCommand(command='cd third_party && ./update_valgrind.sh valgrind_source.tar.bz2 && ' +
                          'cp -rv valgrind valgrind32',
                          description='unpacking valgrind',
                          descriptionDone='unpack valgrind'))

  # Build valgrind and install it to out/.
  f1.addStep(ShellCommand(command='cd third_party && ./build_and_install_valgrind.sh `pwd`/../out',
                          description='building valgrind',
                          descriptionDone='build valgrind'))

  # Build 32-bit valgrind and install it to out32/.
  f1.addStep(ShellCommand(command='cd third_party && ' +
                          './build_and_install_valgrind.sh `pwd`/../out32 valgrind32 --enable-only32bit',
                          description='building 32-bit valgrind',
                          descriptionDone='build 32-bit valgrind'))

  addArchiveStep(f1, '../valgrind_build.tar.gz', ['out', 'out32', 'third_party/valgrind', 'third_party/valgrind32'])

  b1 = {'name': 'buildbot-valgrind',
        'slavename': 'bot5name',
        'builddir': 'full_valgrind',
        'factory': f1,
        }

  return [b1]
