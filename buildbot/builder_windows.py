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

  # Build tsan + pin.
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4',
                              'VALGRIND_ROOT=', 'PIN_ROOT=c:/pin',
                              'w32o'],
                     description='building tsan with pin',
                     descriptionDone='build tsan with pin'))

  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4',
                              'VALGRIND_ROOT=', 'PIN_ROOT=c:/pin',
                              'w32d'],
                     description='building tsan-debug with pin',
                     descriptionDone='build tsan-debug with pin'))

  # Run thread_sanitizer and suppressions tests.
  addTsanTestsStep(f1, ['x86-windows-debug'])

  # Run tests.
  test_binaries = {} # (os, bits, opt, static, name) -> (binary, desc)
  os = 'windows'
  #                  test binary | tsan + run parameters
  #             bits, opt, static,   tsan-debug,   mode
  variants = [
    # ((  32,   1, False),(        True, 'fast')),
    # ((  32,   1, False),(        True, 'slow')),
    ((  32,   1, False),(        True,  'phb')),
    # ((  32,   0, False),(        True, 'slow')),
    ((  32,   0, False),(       False,  'phb'))
    ]
  for (test_variant, run_variant) in variants:
    (tsan_debug, mode) = run_variant
    if not test_binaries.has_key(test_variant):
      (bits, opt, static) = test_variant
      test_desc = addBuildTestStep(f1, os, bits, opt, static)
      test_binaries[test_variant] = test_desc
    test_binary = unitTestBinary(os, bits, opt, static)
    addTestStep(f1, tsan_debug, mode, test_binary, test_desc, frontend='pin',
                pin_root='c:/pin', timeout=None, extra_args=["--error_exitcode=1"])


  binaries = {
    'tsan/bin/x86-windows-debug-ts_pin.dll' : 'tsan-r%s-x86-windows-debug-ts_pin.dll',
    'tsan/bin/x86-windows-ts_pin.dll' : 'tsan-r%s-x86-windows-ts_pin.dll'}
  addUploadBinariesStep(f1, binaries)

  b1 = {'name': 'buildbot-winxp',
        'slavename': 'bot2name',
        'builddir': 'full_winxp',
        'factory': f1,
        }

  b2 = {'name': 'buildbot-vista',
        'slavename': 'bot3name',
        'builddir': 'full_vista',
        'factory': f1,
        }

  return [b1, b2]
