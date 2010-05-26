from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from common import *

from buildbot.status import builder
import process_log
import chromium_utils



class ProcessLogShellStep(ShellCommand):

  def __init__(self, log_processor_class, *args, **kwargs):
    self._result_text = []
    self._log_processor = log_processor_class()
    ShellCommand.__init__(self, *args, **kwargs)

  def start(self):
    """Overridden shell.ShellCommand.start method.

    Adds a link for the activity that points to report ULR.
    """
    self._CreateReportLinkIfNeccessary()
    ShellCommand.start(self)

  def _GetRevision(self):
    """Returns the revision number for the build.

    Result is the revision number of the latest change that went in
    while doing gclient sync. If None, will return -1 instead.
    """
    if self.build.getProperty('got_revision'):
      return self.build.getProperty('got_revision')
    return -1

  def commandComplete(self, cmd):
    """Callback implementation that will use log process to parse 'stdio' data.
    """
    self._result_text = self._log_processor.Process(
        self._GetRevision(), self.getLog('stdio').getText())

  def getText(self, cmd, results):
    text_list = self.describe(True)
    if self._result_text:
      self._result_text.insert(0, '<div class="BuildResultInfo">')
      self._result_text.append('</div>')
      text_list = text_list + self._result_text
    return text_list

  def evaluateCommand(self, cmd):
    shell_result = ShellCommand.evaluateCommand(self, cmd)
    log_result = None
    if self._log_processor and 'evaluateCommand' in dir(self._log_processor):
      log_result = self._log_processor.evaluateCommand(cmd)
    if shell_result is builder.FAILURE or log_result is builder.FAILURE:
      return builder.FAILURE
    if shell_result is builder.WARNINGS or log_result is builder.WARNINGS:
      return builder.WARNINGS
    return builder.SUCCESS

  def _CreateReportLinkIfNeccessary(self):
    if self._log_processor.ReportLink():
      self.addURL('results', "%s" % self._log_processor.ReportLink())


def genBenchmarkStep(factory, platform, benchmark, *args, **kwargs):
  base_dir = 'perf/%s/%s' % (platform, benchmark)
  report_link = '%s/report.html?history=1000' % (base_dir,)
  output_dir = 'public_html/%s' % (base_dir,)

  log_processor_class = chromium_utils.InitializePartiallyWithArguments(
      process_log.GraphingLogProcessor,
      report_link=report_link,
      output_dir=output_dir)

  step = chromium_utils.InitializePartiallyWithArguments(
      ProcessLogShellStep, log_processor_class,
      *args, **kwargs)

  return step


def generate(settings):
  f1 = factory.BuildFactory()

  addSetupTreeForTestsStep(f1)

  # Run benchmarks.
  platform = 'linux-experimental'
  benchmark_modes = {
    35:'phb',
    72:'phb',
    73:'phb',
    151:'phb',
    502:'phb',
    503:'phb',
    512:'hybrid',
  }

  bigtest_binary = unitTestBinary('linux', 64, 0, False, test_base_name='bigtest')
  bigtest_desc = getTestDesc('linux', 64, 0, False)
  step_generator = chromium_utils.InitializePartiallyWithArguments(
      genBenchmarkStep, factory, platform, 'bigtest')
  addTestStep(f1, False, 'phb', bigtest_binary,
              bigtest_desc,
              extra_args=["--error_exitcode=1", "--suppressions=unittest/bigtest.supp"],
              test_base_name='bigtest',
              step_generator=step_generator)

  racecheck_binary = unitTestBinary('linux', 64, 0, False, test_base_name='racecheck_unittest')
  racecheck_desc = getTestDesc('linux', 64, 0, False)
  step_generator = chromium_utils.InitializePartiallyWithArguments(
      genBenchmarkStep, factory, platform, 'racecheck_unittest')
  for test_id, mode in benchmark_modes.items():
    addTestStep(f1, False, mode, racecheck_binary,
                racecheck_desc + ', test ' + str(test_id),
                extra_args=["--error_exitcode=1"],
                extra_test_args=["--gtest_filter=NonGtestTests*", str(test_id)],
                test_base_name='racecheck_unittest',
                step_generator=step_generator)

  # 32-bit benchmarks
  bigtest32_binary = unitTestBinary('linux', 32, 0, False, test_base_name='bigtest')
  bigtest32_desc = getTestDesc('linux', 32, 0, False)
  step_generator = chromium_utils.InitializePartiallyWithArguments(
      genBenchmarkStep, factory, platform, 'bigtest32')
  addTestStep(f1, False, 'phb', bigtest32_binary,
              bigtest32_desc,
              extra_args=["--error_exitcode=1"],
              test_base_name='bigtest',
              frontend_binary='./tsan32.sh',
              step_generator=step_generator)

  racecheck32_binary = unitTestBinary('linux', 32, 0, False, test_base_name='racecheck_unittest')
  racecheck32_desc = getTestDesc('linux', 32, 0, False)
  step_generator = chromium_utils.InitializePartiallyWithArguments(
      genBenchmarkStep, factory, platform, 'racecheck_unittest32')
  for test_id, mode in benchmark_modes.items():
    addTestStep(f1, False, mode, racecheck32_binary,
                racecheck32_desc + ', test ' + str(test_id),
                extra_args=["--error_exitcode=1"],
                extra_test_args=["--gtest_filter=NonGtestTests*", str(test_id)],
                test_base_name='racecheck_unittest',
                frontend_binary='./tsan32.sh',
                step_generator=step_generator)




  b1 = {'name': 'buildbot-experimental',
        'slavename': 'bot1name',
        'builddir': 'full_experimental',
        'factory': f1,
        }

  return [b1]
