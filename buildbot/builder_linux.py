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

  # Build valgrind+tsan and install them to out/.
  f1.addStep(ShellCommand(command='cd third_party && ./update_valgrind.sh && ' +
                          './build_and_install_valgrind.sh `pwd`/../out',
                          description='building valgrind',
                          descriptionDone='build valgrind'))
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4', 'OFFLINE=',
                              'PIN_ROOT=',
                              'lo', 'ld'],
                     description='building tsan',
                     descriptionDone='build tsan'))
  f1.addStep(ShellCommand(command=['make', '-C', 'tsan', 'install',
                                   'VALGRIND_INST_ROOT=../out'],
                          description='installing tsan',
                          descriptionDone='install tsan'))

  # Test that mk-self-contained-tsan works. Output is unused.
  f1.addStep(ShellCommand(command=['tsan_binary/mk-self-contained-tsan.sh',
                                   'out', 'tsan.sh'],
                          description='packing self-contained tsan',
                          descriptionDone='pack self-contained tsan'))

  # Build 32-bit valgrind+tsan and install them to out32/.
  f1.addStep(ShellCommand(command='cd third_party && ' +
                          './build_and_install_valgrind.sh `pwd`/../out32 --enable-only32bit',
                          description='building 32-bit valgrind',
                          descriptionDone='build 32-bit valgrind'))
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4', 'OFFLINE=',
                              'PIN_ROOT=', 'lo', 'ld'],
                     description='building 32-bit tsan',
                     descriptionDone='build 32-bit tsan'))
  f1.addStep(ShellCommand(command=['make', '-C', 'tsan', 'install',
                                   'VALGRIND_INST_ROOT=../out32'],
                          description='installing 32-bit tsan',
                          descriptionDone='install 32-bit tsan'))

  # Run unit tests.
  test_binaries = {} # (bits, opt, static) -> (binary, desc)
  os = 'linux'
  #                  test binary | tsan + run parameters
  #             bits, opt, static,   tsan-debug,   mode
  variants = [((  64,   1, False),(        True, 'fast')),
              ((  64,   1, False),(        True, 'slow')),
              ((  64,   1, False),(        True,  'phb')),
              ((  32,   1, False),(        True, 'slow')),
              ((  64,   0, False),(        True, 'slow')),
              ((  64,   1, False),(       False,  'phb'))]
  for (test_variant, run_variant) in variants:
    (tsan_debug, mode) = run_variant
    if not test_binaries.has_key(test_variant):
      (bits, opt, static) = test_variant
      test_desc = addBuildTestStep(f1, os, bits, opt, static)
      test_binaries[test_variant] = test_desc
    test_binary = unitTestBinary(os, bits, opt, static)
    addTestStep(f1, tsan_debug, mode, test_binary, test_desc, extra_args=["--error_exitcode=1"])


  # Run unit tests with 32-bit valgrind.
  test_desc = test_binaries[(32, 1, False)]
  test_binary = unitTestBinary(os, 32, 1, False)
  addTestStep(f1, False, 'fast', test_binary, test_desc + '(32-bit valgrind)',
              frontend_binary='out32/bin/valgrind')

  b1 = {'name': 'buildbot-linux',
        'slavename': 'bot1name',
        'builddir': 'full',
        'factory': f1,
        }

  return b1
