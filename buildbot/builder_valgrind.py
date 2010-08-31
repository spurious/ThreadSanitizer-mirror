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

  f1.addStep(ShellCommand(command='cd third_party && ./update_valgrind.sh && ' +
                          'cp -rv valgrind valgrind32 && cp -rv valgrind valgrind64',
                          description='unpacking valgrind',
                          descriptionDone='unpack valgrind'))

  # Build valgrind and install it to out/.
  f1.addStep(ShellCommand(command='cd third_party && ./build_and_install_valgrind.sh `pwd`/../out && ' +
                          'strip `pwd`/../out/lib/valgrind/memcheck-x86-linux && ' +
                          'strip `pwd`/../out/lib/valgrind/memcheck-amd64-linux',
                          description='building valgrind',
                          descriptionDone='build valgrind'))

  # Build 32-bit valgrind and install it to out32/.
  f1.addStep(ShellCommand(command='cd third_party && ' +
                          './build_and_install_valgrind.sh `pwd`/../out32 valgrind32 --enable-only32bit && ' +
                          'strip `pwd`/../out32/lib/valgrind/memcheck-x86-linux',
                          description='building 32-bit valgrind',
                          descriptionDone='build 32-bit valgrind'))

  # Build 64-bit valgrind and install it to out64/.
  f1.addStep(ShellCommand(command='cd third_party && ' +
                          './build_and_install_valgrind.sh `pwd`/../out64 valgrind64 --enable-only64bit && ' +
                          'strip `pwd`/../out64/lib/valgrind/memcheck-amd64-linux',
                          description='building 64-bit valgrind',
                          descriptionDone='build 64-bit valgrind'))

  # Pack 3 flavours of self-contained memcheck.
  f1.addStep(ShellCommand(command='tsan/mk-self-contained-valgrind.sh out memcheck memcheck-amd64-linux-self-contained.sh',
                          description='packing self-contained memcheck',
                          descriptionDone='pack self-contained memcheck'))

  f1.addStep(ShellCommand(command='tsan/mk-self-contained-valgrind.sh out32 memcheck memcheck-x86-linux-self-contained.sh',
                          description='packing self-contained 32-bit memcheck',
                          descriptionDone='pack self-contained 32-bit memcheck'))

  f1.addStep(ShellCommand(command='tsan/mk-self-contained-valgrind.sh out64 memcheck memcheck-amd64only-linux-self-contained.sh',
                          description='packing self-contained 64-bit memcheck',
                          descriptionDone='pack self-contained 64-bit memcheck'))

  # Send memcheck binaries to the master.
  binaries = {
    'memcheck-amd64-linux-self-contained.sh' : 'memcheck-r%s-amd64-linux-self-contained.sh',
    'memcheck-x86-linux-self-contained.sh' : 'memcheck-r%s-x86-linux-self-contained.sh',
    'memcheck-amd64only-linux-self-contained.sh' : 'memcheck-r%s-amd64only-linux-self-contained.sh'}

  addUploadBinariesStep(f1, binaries)

  addArchiveStep(f1, '../valgrind_build.tar.gz',
                 ['out', 'out32', 'out64',
                  'third_party/valgrind', 'third_party/valgrind32',
                  'third_party/valgrind64'])

  b1 = {'name': 'buildbot-valgrind',
        'slavename': 'bot5name',
        'builddir': 'full_valgrind',
        'factory': f1,
        }

  return [b1]
