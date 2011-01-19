#!/usr/bin/python2.4
#
# Copyright 2011 Google Inc. All Rights Reserved.

"""One-line documentation for common module.

A detailed description of common.
"""

__author__ = 'glider@google.com (Alexander Potapenko)'

from optparse import OptionParser
import os
import re
import subprocess
import sys

# TODO(glider): get the paths from env.
LLVM_GCC="/usr/bin/llvm-gcc"
LLVM_GPP="/usr/bin/llvm-g++"
GCC="/usr/bin/gcc"
GPP="/usr/bin/g++"
OPT="/usr/bin/opt"
LLC="/usr/bin/llc"
SCRIPT_ROOT = os.path.dirname(os.path.realpath(sys.argv[0]))
LD = "/usr/bin/g++"
LINK_CONFIG = SCRIPT_ROOT + '/link_config.txt'
PASS_SO = SCRIPT_ROOT + '/../opt/ThreadSanitizer/ThreadSanitizer.so'
DA_FLAGS=['-DDYNAMIC_ANNOTATIONS_WANT_ATTRIBUTE_WEAK',
          '-DRACECHECK_UNITTEST_WANT_ATTRIBUTE_WEAK',
          '-DDYNAMIC_ANNOTATIONS_PREFIX=LLVM',
          '-D__clang__']


P32='x86'
P64='x86-64'
PLATFORM = {'-m32': P32, '-m64': P64 }
MARCH = { P32: '-m32', P64: '-m64' }
XARCH = { P32: P32, P64: P64 }
TSAN_RTL = {P32: SCRIPT_ROOT+'/../tsan_rtl/tsan_rtl32.a',
            P64: SCRIPT_ROOT+'/../tsan_rtl/tsan_rtl64.a' }


def setup_parser(parser):
  pass

def print_args(args):
  for i in args[:-1]:
    print "    ", i, "\\"
  print "    ", args[-1]


def gcc(default_cc, fallback_cc):
  source_extensions = re.compile(".*(\.cc$|\.cpp$|\.c$|\.S$|\.cxx$)")
  obj_extensions = re.compile(".*(\.a$|\.o$)")
  drop_args = ['-c', '-std=c++0x', '-Werror', '-finstrument-functions',
  '-Wl,--gc-sections']
  drop_re = re.compile('^-Wno.*')

  from_asm = False
  preprocess_only = False
  run_linker = False
  build_so = False
  fpic = ""
  llc_pic = ""
  debug_info = ""
  compiler_args = []
  platform = P64
  optimization = "-O0"

  src_file = None
  src_obj = None

  args = sys.argv[1:]
  new_args = []
  for arg in args:
    if not (arg in drop_args):
      if not drop_re.match(arg):
        new_args.append(arg)
  args = new_args

  #print_args(args)
  skip_next_arg = False
  for i in range(len(args)):
    if skip_next_arg:
      skip_next_arg = False
      continue
    arg = args[i]
    if arg.startswith("-o"):
      if arg == "-o":
        src_obj = args[i+1]
        skip_next_arg = True
      else:
        src_obj = arg[2:]
      continue
    if arg == "-E":
      preprocess_only = True
      compiler_args += arg
      continue
    if arg == "-shared":
      build_so = True
      run_linker = True
    if arg == "-g":
      debug_info = arg
      continue
    if arg in ['-m32', '-m64']:
      platform = PLATFORM[arg[2:]]
      compiler_args += [arg]
      continue
    if arg.startswith("-O"):
      optimization = arg
      continue
    match = source_extensions.match(arg)
    if match:
      src_file = arg
      if match.groups()[0] == ".S":
        from_asm = True
      continue
    match = obj_extensions.match(arg)
    if match:
      run_linker = True
      continue
    if arg == "-fPIC":
      fpic = arg
      llc_pic = "-relocation-model=pic"
      continue
    if not (arg in drop_args):
      if not drop_re.match(arg):
        compiler_args.append(arg)

  # Cut the extension off.
  filename = ""
  if src_file:
    filename = '.'.join(src_file.split('.')[:-1])
  src_bitcode = filename + '.ll'
  src_tmp = filename + '-tmp.ll'
  src_instrumented = filename + '-instr.ll'
  src_asm = filename + '.S'
  if src_obj is None:
    src_obj = filename + '.o'
  src_exe = filename

  # Now let's decide how to invoke the compiler
  if run_linker:
    ld_args = [LD, '-march='+XARCH[platform]] + args
    if build_so:
      ld_args += ['-shared']
    else:
      ld_args += ['-lpthread']
      config = file(LINK_CONFIG).readlines()
      for line in config:
        pieces = line[:-1].split(" ")
        if len(pieces) == 2 and pieces[0] in ['wrap', 'undefined']:
          ld_args.append('-Wl,--' + pieces[0] + ',' + pieces[1])
      ld_args += [TSAN_RTL[platform]]
    ld_args += ['-o', src_obj]

    retcode = subprocess.call(ld_args)
    if retcode != 0: sys.exit(retcode)
    return


  if preprocess_only:
    exec_args = [fallback_cc] + args
    retcode = subprocess.call(exec_args)
    if retcode != 0: sys.exit(retcode)
    return

  if not from_asm:
    llvm_gcc_args = [default_cc, '-emit-llvm', MARCH[platform], src_file, optimization,
        debug_info, fpic, '-c'] + DA_FLAGS + compiler_args + ['-o', src_bitcode]
    retcode = subprocess.call(llvm_gcc_args)
    if retcode != 0: sys.exit(retcode)

    # TODO(glider): additional opt passes.
    opt_args = [OPT, '-load', PASS_SO, '-online', '-arch=' + XARCH[platform],
        src_bitcode, '-o', src_instrumented]
    retcode = subprocess.call(opt_args, stderr=file("instrumentation.log", 'w'))
    if retcode != 0: sys.exit(retcode)

    llc_args = [LLC, '-march=' + XARCH[platform], llc_pic, optimization,
        src_instrumented, '-o', src_asm]
    retcode = subprocess.call(llc_args)
    if retcode != 0:
      fallback_args = [fallback_cc] + args
      fallback_args += ['-c']
      retcode = subprocess.call(fallback_args)
      if retcode != 0: sys.exit(retcode)
      return

  cc_args = [default_cc, MARCH[platform], '-c', src_asm, optimization, fpic,
      debug_info, '-o', src_obj]
  retcode = subprocess.call(cc_args)
  if retcode != 0:
    fallback_args = [fallback_cc] + args
    if src_obj:
      fallback_args += ['-o', src_obj]
    retcode = subprocess.call(fallback_args)
    if retcode != 0: sys.exit(retcode)
    return

