from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from buildbot.steps.transfer import FileDownload
from common import *


def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
  f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  f1.addStep(FileDownload(mastersrc='~/valgrind_source.tar.bz2',
                          slavedest='third_party/valgrind_source.tar.bz2'))

  f1.addStep(ShellCommand(command='cd third_party && ./update_valgrind.sh valgrind_source.tar.bz2',
                          description='unpacking valgrind',
                          descriptionDone='unpack valgrind'))

  # Build valgrind+tsan and install them to out/.
  f1.addStep(ShellCommand(command='cd third_party && ./build_and_install_valgrind.sh `pwd`/../out',
                          description='building valgrind',
                          descriptionDone='build valgrind'))

  path_flags = ['OFFLINE=',
                'VALGRIND_INST_ROOT=../out',
                'PIN_ROOT=']
  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4'] + path_flags + ['m32o'],
                     description='building tsan (m32o)',
                     descriptionDone='build tsan (m32o)'))

  f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4'] + path_flags + ['m32d'],
                     description='building tsan (m32d)',
                     descriptionDone='build tsan (m32d)'))

  # Build self-contained tsan binaries.
  f1.addStep(ShellCommand(command=['make', '-C', 'tsan'] + path_flags +
                          ['OS=darwin', 'ARCH=x86', 'DEBUG=0', 'self-contained-stripped'],
                          description='packing self-contained tsan',
                          descriptionDone='pack self-contained tsan'))

  f1.addStep(ShellCommand(command=['make', '-C', 'tsan'] + path_flags +
                          ['OS=darwin', 'ARCH=x86', 'DEBUG=1', 'self-contained'],
                          description='packing self-contained tsan (debug)',
                          descriptionDone='pack self-contained tsan (debug)'))

  f1.addStep(ShellCommand(command='ln -s tsan/bin/tsan-x86-darwin-self-contained.sh tsan.sh; ' +
                          'ln -s tsan/bin/tsan-x86-darwin-debug-self-contained.sh tsan-debug.sh',
                          description='symlinking tsan',
                          descriptionDone='symlink tsan'))

  binaries = {
    'tsan/bin/tsan-x86-darwin-debug-self-contained.sh' : 'tsan-r%s-macosx10.5-debug-self-contained.sh',
    'tsan/bin/tsan-x86-darwin-self-contained.sh' : 'tsan-r%s-macosx10.5-darwin-self-contained.sh'}
  addUploadBinariesStep(f1, binaries)


  # Run thread_sanitizer and suppressions tests.
  addTsanTestsStep(f1, ['x86-darwin-debug'])

  # Run unit tests.
  test_binaries = {} # (bits, opt, static) -> (binary, desc)
  os = 'darwin'
  #                  test binary | tsan + run parameters
  #             bits, opt, static,   tsan-debug,   mode
  variants = [((  32,   1, False),(        True, 'hybrid')),
              ((  32,   1, False),(        True,    'phb')),
              ((  32,   0, False),(        True, 'hybrid')),
              ((  32,   1, False),(       False,    'phb'))]
  for (test_variant, run_variant) in variants:
    (tsan_debug, mode) = run_variant
    if not test_binaries.has_key(test_variant):
      (bits, opt, static) = test_variant
      test_desc = addBuildTestStep(f1, os, bits, opt, static)
      test_binaries[test_variant] = test_desc
    test_binary = unitTestBinary(os, bits, opt, static)
    addTestStep(f1, tsan_debug, mode, test_binary, test_desc,
                extra_args=["--error_exitcode=1"])
    addTestStep(f1, tsan_debug, mode, test_binary, test_desc + ' RV 1st pass',
                extra_args=['--show-expected-races', '--error_exitcode=1'],
                extra_test_args=['--gtest_filter="RaceVerifierTests.*"'],
                append_command='2>&1 | tee raceverifier.log')
    addTestStep(f1, tsan_debug, mode, test_binary, test_desc + ' RV 2nd pass',
                extra_args=['--error_exitcode=1', '--race-verifier=raceverifier.log'],
                extra_test_args=['--gtest_filter="RaceVerifierTests.*"'],
                append_command='2>&1')


  b1 = {'name': 'buildbot-mac',
        'slavename': 'bot7name',
        'builddir': 'full_mac',
        'factory': f1,
        }

  return [b1]
