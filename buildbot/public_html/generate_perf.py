#!/usr/bin/env python
# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Initializes all the pref directories.

import os
import sys

base_url = 'http://kcc-box.eem:8010/waterfall'

# Template contents of a config.js file.  One of these gets created in
# each subdirectory for a given perf test, identified with the slave
# that it's part of and an appropriate title.

config_template = """\
var Config = {
  buildslave: '%(slave)s',
  title: '%(title)s',
  changeLinkPrefix: '%(base_url)s/perf/dashboard/ui/changelog.html?url=/trunk/src&mode=html&range=',
  coverageLinkPrefix: '%(base_url)s/coverage/%(tester_dir)s/',
"""
config_template_end = """};
"""

default_symlink_list = [
    ('details.html',  '../../dashboard/ui/details.html'),
    ('report.html',   '../../dashboard/ui/generic_plotter.html'),
    ('js',            '../../dashboard/ui/js'),
]

# Descriptions for optional detail tabs
detail_tab_desc = {
  'view-pages': 'Pages',
  'view-coverage': 'Coverage',
}

class Subdir:
  def __init__(self, name, title, detail_tabs=None):
    self.name = name
    self.title = title
    self.symlink_list = default_symlink_list

    if detail_tabs is None:
      self.detail_tabs = ['view-pages']
    else:
      self.detail_tabs = detail_tabs

  def initialize(self, tester_dir, slave):
    """
    Creates or initializes this subdirectory in the specified tester_dir
    with the specified slave name.
    """
    subdir = os.path.join(tester_dir, self.name)
    if not os.path.exists(subdir):
      os.mkdir(subdir)

    for slink, target in self.symlink_list:
      slink = os.path.join(subdir, slink)
      # Remove the old symlinks first.  Catch exceptions on the
      # assumption that this is the first time and the symlink
      # doesn't exist.  If it's due to some other problem, the
      # symlink creation afterwards will fail for us.
      try:
        os.unlink(slink)
      except EnvironmentError:
        pass
      os.symlink(target, slink)

    contents = config_template % {
        'base_url': base_url,
        'slave': slave,
        'title': self.title,
        'tester_dir': tester_dir,
    }

    # Add detail tabs to config
    contents += "  detailTabs: { 'view-change': 'CL'"
    for tab in self.detail_tabs:
      contents += ", '%s': '%s'" % (tab, detail_tab_desc.get(tab, tab))
    contents += ' }\n'

    contents += config_template_end

    open(os.path.join(subdir, 'config.js'), 'w').write(contents)


default_subdir_list = [
    Subdir('bigtest', 'Bigtest performance'),
]


class PerfTester:
  def __init__(self, dir, title):
    self.dir = dir
    self.title = title
    self.subdirs = default_subdir_list
  def initialize(self):
    """
    Creates or initializes this perf tester dir, with all of the
    subdirectories for specific tests.
    """
    if not os.path.exists(self.dir):
      os.mkdir(self.dir)
    for subdir in self.subdirs:
      subdir.initialize(self.dir, self.title)


PerfTester_list = [
    PerfTester('linux-experimental', 'Experimental Linux bot'),
]


def main():
  for perf_tester in PerfTester_list:
    perf_tester.initialize()
  return 0


if __name__ == '__main__':
  sys.exit(main())
