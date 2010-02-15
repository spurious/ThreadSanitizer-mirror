from buildbot.process import factory
from buildbot.steps.source import SVN
from buildbot.steps.shell import Compile
from buildbot.steps.shell import Test
from buildbot.steps.shell import ShellCommand

__all__ = [unitTestBinary, addBuildTestStep, addTestStep]

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

def addBuildTestStep(factory, os, bits, opt, static):
  """Adds a step for building a unit test binary."""
  desc = []
  command = ['make', '-C', 'unittest', 'all']
  command.append('OS=%s' % os)
  desc.append(os)

  if bits == 64:
    command.append('ARCH=amd64')
  else:
    command.append('ARCH=x86')
  desc.append(str(bits))

  command.append('OPT=%d' % opt)
  desc.append('O%d' % opt)

  command.append('STATIC=%d' % static)
  if static:
    desc.append('static')

  desc_common = '(' + ','.join(desc) + ')'
  print command
  factory.addStep(Compile(command = command,
                          description = 'building unittests ' + desc_common,
                          descriptionDone = 'build unittests ' + desc_common))
  return desc_common


def addTestStep(factory, debug, mode, test_binary, test_desc,
                frontend_binary=None, extra_args=[], frontend='valgrind',
                pin_root=None, timeout=1800, test_base_name='racecheck_unittest'):
  """Adds a step for running unit tests with tsan."""
  args = []
  env = {}
  desc = []

  if frontend == 'valgrind':
    frontend_binary = frontend_binary or 'out/bin/valgrind'
  elif frontend == 'pin':
    frontend_binary = frontend_binary or 'tsan/tsan_pin.sh'

  if frontend == 'valgrind':
    tool_arg = '--tool=tsan'
    if debug:
      tool_arg += '-debug'
      desc.append('debug')
    args.append(tool_arg)

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
  if timeout:
    command += ['alarm', '-l', str(timeout)]
  command += [frontend_binary] + extra_args + args + [test_binary]
  print command

  factory.addStep(Test(command = command, env = env,
                       description = 'testing ' + desc_common + ' on ' + test_base_name + test_desc,
                       descriptionDone = 'test ' + desc_common + ' on ' + test_base_name + test_desc))
