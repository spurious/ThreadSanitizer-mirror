#!/usr/bin/python

# Symbolize the output of backtrace_symbols_fd()
#
# Read stdin, and replace lines that look like
#  ./a.out[0x4053ae]
# with lines like
#  ./a.out[0x4053ae] <SymbolWhichMatchesTheAddress>

import sys, re, os

for line in sys.stdin:
  frame_re = "^\s*([^()]*)\s*\[0x([0-9a-f]+)\]\s*$"
  frame_match = re.match(frame_re, line)
  if (not frame_match):
    print line.strip()  # Print unchanged.
  else:
    executable = frame_match.group(1)
    address    = frame_match.group(2)
    # A much more elegant solution is possible (e.g. with 'nm').
    objdump_string = ( "objdump -d --stop-address=0x%s %s | grep '^[0-9].*:'"
                     "| tail -1 | awk '{print $2}'" % (address, executable) )
    objdump_pipe = os.popen(objdump_string)
    symbol = objdump_pipe.read().strip()
    objdump_pipe.close()
    print line.strip(), symbol.strip(":")
