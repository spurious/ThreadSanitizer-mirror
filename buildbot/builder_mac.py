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
                              'm'],
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

  # Run unit tests.
  test_binaries = {} # (bits, opt, static) -> (binary, desc)
  os = 'darwin'
  #                  test binary | tsan + run parameters
  #             bits, opt, static,   tsan-debug,   mode
  variants = [((  32,   1, False),(        True, 'fast')),
              ((  32,   1, False),(        True, 'slow')),
              ((  32,   1, False),(        True,  'phb')),
              ((  32,   0, False),(        True, 'slow')),
              ((  32,   1, False),(       False,  'phb'))]
  for (test_variant, run_variant) in variants:
    (tsan_debug, mode) = run_variant
    if not test_binaries.has_key(test_variant):
      (bits, opt, static) = test_variant
      test_desc = addBuildTestStep(f1, os, bits, opt, static)
      test_binaries[test_variant] = test_desc
    test_binary = unitTestBinary(os, bits, opt, static)
    addTestStep(f1, tsan_debug, mode, test_binary, test_desc,
                extra_args=["--error_exitcode=1"])


  b1 = {'name': 'buildbot-mac',
        'slavename': 'bot4name',
        'builddir': 'full_mac',
        'factory': f1,
        }

  return b1
