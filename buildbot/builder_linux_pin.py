from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from common import *


def runAllTests(factory, variants, os):
  test_variants = {} # (bits, opt, static, base_name) -> (binary, desc)
  for (test_variant, run_variant) in variants:
    (tsan_debug, mode, base_name) = run_variant
    if test_variants.has_key(test_variant):
      test_desc = test_variants[test_variant]
    else:
      (bits, opt, static) = test_variant
      test_desc = getTestDesc(os, bits, opt, static)
      test_variants[test_variant] = test_desc
    test_binary = unitTestBinary(os, bits, opt, static, test_base_name=base_name)
    if base_name == 'demo_tests':
      extra_args = []
    else:
      extra_args=["--error_exitcode=1"]
    addTestStep(factory, tsan_debug, mode, test_binary, test_desc, frontend='pin',
                pin_root='../../../third_party/pin', extra_args = extra_args)


def generate(settings):
  f1 = factory.BuildFactory()

  addSetupTreeForTestsStep(f1)

  # Run thread_sanitizer and suppressions tests.
  addTsanTestsStep(f1, ['amd64-linux-debug', 'x86-linux-debug'])

  # Run unit tests.
  #                  test binary | tsan + run parameters
  #             bits, opt, static,   tsan-debug,   mode
#   variants = [((  64,   1, False),(        True, 'fast', 'racecheck_unittest')),
#               ((  64,   1, False),(        True, 'slow', 'racecheck_unittest')),
#               ((  64,   1, False),(        True,  'phb', 'racecheck_unittest')),
#               ((  32,   1, False),(        True, 'slow', 'racecheck_unittest')),
#               ((  64,   0, False),(        True, 'slow', 'racecheck_unittest')),
#               ((  64,   1, False),(        True, 'slow',         'demo_tests'))]
  variants = [((  64,   0, False),(        True, 'phb',    'racecheck_unittest')),
              ((  64,   1, False),(        True, 'hybrid',         'demo_tests'))]
  runAllTests(f1, variants, 'linux')


  b1 = {'name': 'buildbot-linux-pin',
        'slavename': 'bot6name',
        'builddir': 'full_linux_pin',
        'factory': f1,
        }

  return [b1]
