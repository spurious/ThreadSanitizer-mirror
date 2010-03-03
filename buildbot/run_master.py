#!/usr/bin/python
# Copyright (c) 2008 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

""" Initialize the environment variables and start the buildbot slave.
"""

import os
import shutil
import subprocess
import sys


if '__main__' == __name__:
  # change the current directory to the directory of the script.
  os.chdir(sys.path[0])

  # Set the python path.
  parent_dir = os.path.abspath(os.path.pardir)
  python_path = [
  # Put anything that you want to keep private in this path.
    os.path.join(parent_dir, 'scripts', 'private'),
    os.path.join(parent_dir, 'scripts', 'common'),
    os.path.join(parent_dir, 'scripts', 'slave'),
    os.path.join(parent_dir, 'pylibs'),
    os.path.join(parent_dir, 'symsrc'),
  ]
  os.environ['PYTHONPATH'] = os.pathsep.join(python_path)

  # Add these in from of the PATH too.
  new_path = python_path
  new_path.extend(sys.path)
  sys.path = new_path

  # Run the master.
  import twisted.scripts.twistd as twistd
  twistd.run()
