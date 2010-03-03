from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand

import os.path


def unitTestBinary(os, bits, opt, static, test_base_name='racecheck_unittest'):
  if bits == 64:
    arch = 'amd64'
  else:
    arch = 'x86'
  name = 'unittest/bin/%s-%s-%s-O%d' % (test_base_name, os, arch, opt)
  if static:
    name += '-static'
  if os == 'windows':
    name += '.exe'
  return name


def getTestDesc(os, bits, opt, static):
  desc = []
  desc.append(os)
  desc.append(str(bits))
  desc.append('O%d' % opt)
  if static:
    desc.append('static')
  return '(' + ','.join(desc) + ')'


def addBuildTestStep(factory, os, bits, opt, static):
  """Adds a step for building a unit test binary."""
  command = ['make', '-C', 'unittest', 'all']
  command.append('OS=%s' % os)

  if bits == 64:
    command.append('ARCH=amd64')
  else:
    command.append('ARCH=x86')

  command.append('OPT=%d' % opt)

  command.append('STATIC=%d' % static)

  desc_common = getTestDesc(os, bits, opt, static)
  print command
  factory.addStep(Compile(command = command,
                          description = 'building unittests ' + desc_common,
                          descriptionDone = 'build unittests ' + desc_common))
  return desc_common


def addTestStep(factory, debug, mode, test_binary, test_desc,
                frontend_binary=None, extra_args=[], frontend='valgrind',
                pin_root=None, timeout=1800, test_base_name='racecheck_unittest',
                append_command=None, step_generator=Test):
  """Adds a step for running unit tests with tsan."""
  args = []
  env = {}
  desc = []

  if frontend == 'valgrind':
    if debug:
      frontend_binary = frontend_binary or './tsan-debug.sh'
    else:
      frontend_binary = frontend_binary or './tsan.sh'
    # frontend_binary = frontend_binary or 'out/bin/valgrind'
  elif frontend == 'pin':
    frontend_binary = frontend_binary or 'tsan/tsan_pin.sh'

  if debug:
    desc.append('debug')
  # if frontend == 'valgrind':
  #   tool_arg = '--tool=tsan'
  #   if debug:
  #     tool_arg += '-debug'
  #     desc.append('debug')
  #   args.append(tool_arg)

  if frontend == 'pin':
    if pin_root:
      env['PIN_ROOT'] = pin_root
    env['TS_ROOT'] = 'tsan'

  if mode == 'phb':
    env['TSAN_PURE_HAPPENS_BEFORE'] = '1'
    args.extend(['--pure-happens-before=yes', '--ignore-in-dtor=no'])
  elif mode == 'fast':
    env['TSAN_FAST_MODE'] = '1'
    args.extend(['--fast-mode=yes', '--pure-happens-before=no', '--ignore-in-dtor=yes'])
  else: # mode == 'slow'
    args.extend(['--fast-mode=no', '--pure-happens-before=no', '--ignore-in-dtor=no'])

  args.append('--suppressions=unittest/racecheck_unittest.supp')

  desc.append(mode)
  desc_common = 'tsan-' + frontend + '(' + ','.join(desc) + ')'

  command = []
  # if timeout:
  #   command += ['alarm', '-l', str(timeout)]
  command += [frontend_binary] + extra_args + args + [test_binary]
  if append_command:
    command = ' '.join(command + [append_command])
  print command

  factory.addStep(step_generator(command = command, env = env,
                       description = 'testing ' + desc_common + ' on ' + test_base_name + test_desc,
                       descriptionDone = 'test ' + desc_common + ' on ' + test_base_name + test_desc))


def addClobberStep(factory):
  factory.addStep(ShellCommand(command='rm -rf -- *',
                               description='clobbering build dir',
                               descriptionDone='clobber build dir'))

def addArchiveStep(factory, archive_path, paths=['.']):
  factory.addStep(ShellCommand(
      command='tar czvf '+ archive_path + '.tmp '+ ' '.join(paths) + 
      ' && mv ' + archive_path + '.tmp ' + archive_path +
      ' && chmod 644 ' + archive_path,
      description='packing build tree',
      descriptionDone='pack build tree'))


def addExtractStep(factory, archive_path):
  factory.addStep(ShellCommand(
      command=['tar', 'xzvf', archive_path],
      description='extract build tree',
      descriptionDone='extract build tree'))


class GetRevisionStep(ShellCommand):

  def __init__(self, *args, **kwargs):
    kwargs['command'] = 'svnversion .'
    kwargs['description'] = 'getting revision'
    kwargs['descriptionDone'] = 'get revision'
    ShellCommand.__init__(self, *args, **kwargs)

  def commandComplete(self, cmd):
    revision = self.getLog('stdio').getText().rstrip();
    self.build.setProperty('got_revision', revision, 'Build');


# Gets full build tree from another builder, unpacks it and gets its svn revision
def addSetupTreeForTestsStep(factory):
  addClobberStep(factory)
  factory.addStep(ShellCommand(command=['wget', 'http://codf220/full_linux_build/full_build.tar.gz'],
                          description='getting build tree',
                          descriptionDone='get build tree'))

  addExtractStep(factory, 'full_build.tar.gz')
  factory.addStep(GetRevisionStep());



__all__ = ['unitTestBinary', 'addBuildTestStep', 'addTestStep',
           'addClobberStep', 'addArchiveStep', 'addExtractStep',
           'addSetupTreeForTestsStep', 'getTestDesc']
