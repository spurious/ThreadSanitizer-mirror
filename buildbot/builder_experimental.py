from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand
from common import *

from buildbot.status import builder
import process_log
import chromium_utils

import os.path


def addClobberStep(factory):
  factory.addStep(ShellCommand(command='rm -rf -- *',
                               description='clobbering build dir',
                               descriptionDone='clobber build dir'))

def addExtractStep(factory, archive_dir):
  factory.addStep(ShellCommand(
      command=['tar', 'xzvf', os.path.join(archive_dir, 'build.tar.gz')],
      description='extract build tree',
      descriptionDone='extract build tree'))

class GetRevisionStep(ShellCommand):

  def __init__(self, *args, **kwargs):
    kwargs['command'] = 'svnversion .'
    ShellCommand.__init__(self, *args, **kwargs)

  def commandComplete(self, cmd):
    revision = self.getLog('stdio').getText().rstrip();
    self.build.setProperty('got_revision', revision, 'Build');


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
  report_link = '%s/report.html' % (base_dir,)
  output_dir = 'public_html/%s' % (base_dir,)

  log_processor_class = chromium_utils.InitializePartiallyWithArguments(
      process_log.GraphingLogProcessor,
      report_link=report_link,
      output_dir=output_dir)

  step = chromium_utils.InitializePartiallyWithArguments(
      ProcessLogShellStep, log_processor_class,
      *args, **kwargs)

  return step

def addBenchmarkStep(factory, platform, benchmark, *args, **kwargs):
  step = genBenchmarkStep(factory, platform, benchmark, *args, **kwargs)
  factory.addStep(step)


def generate(settings):
  f1 = factory.BuildFactory()

  # Checkout sources.
#   f1.addStep(SVN(svnurl=settings['svnurl'], mode='copy'))

  platform = 'linux-experimental'
  benchmark = 'bigtest'

#   log_processor_class = chromium_utils.InitializePartiallyWithArguments(
#       process_log.GraphingLogProcessor,
#       report_link='perf/perf1/report.html',
#       output_dir='public_html/perf/perf1')

#   step = chromium_utils.InitializePartiallyWithArguments(
#       ProcessLogShellStep, log_processor_class,



  # Build valgrind+tsan and install them to out/.
#   f1.addStep(ShellCommand(command='cd third_party && ./update_valgrind.sh && ' +
#                           './build_and_install_valgrind.sh `pwd`/../out',
#                           description='building valgrind',
#                           descriptionDone='build valgrind'))
#   f1.addStep(Compile(command=['make', '-C', 'tsan', '-j4', 'OFFLINE=',
#                               'PIN_ROOT=',
#                               'lo', 'ld'],
#                      description='building tsan',
#                      descriptionDone='build tsan'))
#   f1.addStep(ShellCommand(command=['make', '-C', 'tsan', 'install',
#                                    'VALGRIND_INST_ROOT=../out'],
#                           description='installing tsan',
#                           descriptionDone='install tsan'))


  addClobberStep(f1)
  addExtractStep(f1, '/usr/local/google/Buildbot/archive')

  f1.addStep(GetRevisionStep());

  # Run benchmarks.
  bigtest_binary = unitTestBinary('linux', 64, 0, False, test_base_name='bigtest')
  bigtest_desc = 'some test' # FIXME
#   bigtest_desc = addBuildTestStep(f1, 'linux', 64, 0, False);

  step_generator = chromium_utils.InitializePartiallyWithArguments(
      genBenchmarkStep, factory, platform, benchmark)
  addTestStep(f1, False, 'phb', bigtest_binary,
              bigtest_desc,
              extra_args=["--error_exitcode=1"],
              test_base_name='bigtest',
              step_generator=step_generator)


  b1 = {'name': 'buildbot-experimental',
        'slavename': 'bot6name',
        'builddir': 'full_experimental',
        'factory': f1,
        }

  return b1
