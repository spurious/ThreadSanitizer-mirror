#!/usr/bin/env python

# A script for driving Arduino-based build status orb.
# Very hackish.

import os
import sys
import re
import serial
import time
import urllib
import glob
import os.path
import errno

urlBase = 'http://build.chromium.org/buildbot/tsan'
url = urlBase + '/one_box_per_builder'

class ArduinoSerial:
  def __init__(self):
    self.serial = None

  def guessDeviceName(self):
    # This is for Mac.
    return '/dev/tty.usbserial-A9007P5Q'
    # Linux magic. Remove the previous line.
    devices = glob.glob('/sys/bus/usb-serial/devices/*')
    if len(devices) > 0:
      return os.path.join('/dev', os.path.basename(devices[-1]))
    else:
      return None

  def close(self):
    if self.serial:
      try:
        self.serial.close()
      except:
        pass
    self.serial = None

  def reinit(self):
    self.close()
    while True:
      path = self.guessDeviceName()
      if path:
        print "Serial device: " + path
        self.serial = serial.Serial(path, 9600, timeout=1)
        if self.serial:
          return
      print "No serial device. Waiting..."
      time.sleep(5)

  def write(self, s):
    if not self.serial:
      self.reinit()
    while True:
      try:
        self.serial.write(s)
        break
      except OSError, e:
        if e.errno == errno.EIO or e.errno == errno.ENODEV:
          # Reinit serial.
          self.reinit()
        else:
          # Give up and report the error.
          raise e

def tryGetStatus():
  f = urllib.urlopen(url)
  text = f.read()
  f.close()
  status_all = len(re.findall('class="LastBuild box ', text))
  status_green = len(re.findall('class="LastBuild box success', text))
  return status_green == status_all

def getStatus():
  while True:
    try:
      return tryGetStatus()
    except IOError, e:
      print "getStatus, ", str(e)
      time.sleep(30)

def setColor(port, red):
  if (red):
    port.write('\x01')
  else:
    port.write('\x02')

def demonize():
  pid = os.fork()
  if not pid:
    otherpid = os.fork()
    if not otherpid:
      ppid = os.getppid()
      while ppid != 1:
        time.sleep(0.5)
        ppid = os.getppid()
      return
    else:
      os._exit(0)
  else:
    os.wait()
    sys.exit(0)

def main():
#   demonize()
  port = ArduinoSerial()
  while True:
    status = getStatus()
    print status
    try:
      setColor(port, not status)
    except OSError, e:
      print "setColor: error " + str(e)
    time.sleep(3)

if __name__ == '__main__':
  main()
