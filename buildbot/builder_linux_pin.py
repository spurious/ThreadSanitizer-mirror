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
      test_desc = addBuildTestStep(factory, os, bits, opt, static)
      test_variants[test_variant] = test_desc
    test_binary = unitTestBinary(os, bits, opt, static, test_base_name=base_name)
    addTestStep(factory, tsan_debug, mode, test_binary, test_desc, frontend='pin',
                pin_root='/usr/local/google/pin')


def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
  f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  # Build tsan + pin.
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4',
                              'VALGRIND_ROOT=',
                              'PIN_ROOT=/usr/local/google/pin',
                              'ld'],
                     description='building tsan with pin',
                     descriptionDone='build tsan with pin'))

  # Run suppressions tests.
  f1.addStep(Test(command='tsan/bin/amd64-linux-debug-suppressions_test',
                  description='running amd64 suppresions tests',
                  descriptionDone='run amd64 suppresions tests'))
  f1.addStep(Test(command='tsan/bin/x86-linux-debug-suppressions_test',
                  description='running x86 suppresions tests',
                  descriptionDone='run x86 suppresions tests'))

  # Run unit tests.
  variants = [((  64,   1, False),(        True, 'fast', 'racecheck_unittest')),
              ((  64,   1, False),(        True, 'slow', 'racecheck_unittest')),
              ((  64,   1, False),(        True,  'phb', 'racecheck_unittest')),
              ((  32,   1, False),(        True, 'slow', 'racecheck_unittest')),
              ((  64,   0, False),(        True, 'slow', 'racecheck_unittest')),
              ((  64,   1, False),(        True, 'slow',         'demo_tests'))]
  runAllTests(f1, variants, 'linux')


  b1 = {'name': 'buildbot-linux-pin',
        'slavename': 'bot3name',
        'builddir': 'full_linux_pin',
        'factory': f1,
        }

  return b1
