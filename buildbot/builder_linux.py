from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from common import *

def generate(settings):
  f1 = factory.BuildFactory()

  addSetupTreeForTestsStep(f1)

  # Run thread_sanitizer and suppressions tests.
  addTsanTestsStep(f1, ['amd64-linux-debug', 'x86-linux-debug'])

  # Run output tests.
  output_test_binary = unitTestBinary('linux', 64, 0, False, test_base_name='output_test1')
  output_test_desc = getTestDesc('linux', 64, 0, False);
  addTestStep(f1, False, 'phb', output_test_binary,
              output_test_desc,
              extra_args=["--error_exitcode=1"],
              append_command = ' 2>&1 | unittest/match_output.py unittest/output_test1.tmpl',
              test_base_name='output_test1')


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
      test_desc = getTestDesc(os, bits, opt, static)
      test_binaries[test_variant] = test_desc
    test_binary = unitTestBinary(os, bits, opt, static)
    addTestStep(f1, tsan_debug, mode, test_binary, test_desc, extra_args=["--error_exitcode=1"])


  # Run unit tests with 32-bit valgrind.
  test_desc = test_binaries[(32, 1, False)]
  test_binary = unitTestBinary(os, 32, 1, False)
  addTestStep(f1, False, 'fast', test_binary, test_desc + '(32-bit valgrind)',
              frontend_binary='./tsan32.sh')

  b1 = {'name': 'buildbot-linux',
        'slavename': 'bot5name',
        'builddir': 'full',
        'factory': f1,
        }

  return [b1]
