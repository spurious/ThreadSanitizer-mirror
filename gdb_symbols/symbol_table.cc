// Copyright (c) 2011, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: glider@google.com (Alexander Potapenko)
//
// SymbolTable implementation.
// Some of the code below is derived from Google Perftools
// (http://code.google.com/p/google-perftools/)

#include "symbol_table.h"

#include <stdio.h>  // TODO(glider): remove

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

SymbolTable::SymbolTable() {
  gdb_in = -1;
  gdb_out = -1;
  finalized = false;
  OpenPipe();
}

SymbolTable::~SymbolTable() {
  if (!finalized) Finalize();
}

// TODO(glider): {Before,After}Fork* should execute callbacks set by the user.
bool SymbolTable::BeforeFork() {
  return true;
}

bool SymbolTable::AfterForkChild() {
  return true;
}

bool SymbolTable::AfterForkParent() {
  return true;
}

int SymbolTable::ReadBuffer(char *buf, int size) {
  int total_read = 0;
  while (1) {
    int bytes_read = read(gdb_out, buf + total_read, size - total_read);
    if (bytes_read < 0) {
      return 0;
    } else if (bytes_read == 0) {
      break;
    } else {
      total_read += bytes_read;
      break;
    }
  }
  return total_read;
}

int SymbolTable::OpenPipe() {
// Updates symbolization_table with the pointers to symbol names corresponding
// to its keys. The symbol names are stored in out, which is allocated and
// freed by the caller of this routine.
// Note that the forking/etc is not thread-safe or re-entrant.  That's
// ok for the purpose we need -- reporting leaks detected by heap-checker
// -- but be careful if you decide to use this routine for other purposes.
  // All this work is to do two-way communication.  ugh.
  int *child_in = NULL;   // file descriptors
  int *child_out = NULL;  // for now, we don't worry about child_err
  int child_fds[5][2];    // socketpair may be called up to five times below

  // The client program may close its stdin and/or stdout and/or stderr
  // thus allowing socketpair to reuse file descriptors 0, 1 or 2.
  // In this case the communication between the forked processes may be broken
  // if either the parent or the child tries to close or duplicate these
  // descriptors. The loop below produces two pairs of file descriptors, each
  // greater than 2 (stderr).
  for (int i = 0; i < 5; i++) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, child_fds[i]) == -1) {
      for (int j = 0; j < i; j++) {
        close(child_fds[j][0]);
        close(child_fds[j][1]);
        return 0;
      }
    } else {
      if ((child_fds[i][0] > 2) && (child_fds[i][1] > 2)) {
        if (child_in == NULL) {
          child_in = child_fds[i];
        } else {
          child_out = child_fds[i];
          for (int j = 0; j < i; j++) {
            if (child_fds[j] == child_in) continue;
            close(child_fds[j][0]);
            close(child_fds[j][1]);
          }
          break;
        }
      }
    }
  }
  if (!BeforeFork()) return 0;
  switch (fork()) {
    case -1: {  // error
      close(child_in[0]);
      close(child_in[1]);
      close(child_out[0]);
      close(child_out[1]);
      return 0;
    }
    case 0: {  // child
      close(child_in[1]);   // child uses the 0's, parent uses the 1's
      close(child_out[1]);  // child uses the 0's, parent uses the 1's
      close(0);
      close(1);
      if (dup2(child_in[0], 0) == -1) _exit(1);
      if (dup2(child_out[0], 1) == -1) _exit(2);
      // Unset vars that might cause trouble when we fork
      if (!AfterForkChild()) _exit(4);
      // Start gdb in quiet mode.
      execlp(kGdbPath, kGdbPath, "-q", NULL);
      //execlp(kGdbPath, "-q", "2>/dev/null", NULL);
      _exit(3);  // if execvp fails, it's bad news for us
    }
    default: {  // parent
      close(child_in[0]);   // child uses the 0's, parent uses the 1's
      close(child_out[0]);  // child uses the 0's, parent uses the 1's
      if (!AfterForkParent()) _exit(4);
#ifdef HAVE_POLL_H
      // For maximum safety, we check to make sure the execlp
      // succeeded before trying to write.  (Otherwise we'll get a
      // SIGPIPE.)  For systems without poll.h, we'll just skip this
      // check, and trust that the user set PPROF_PATH correctly!
      struct pollfd pfd = { child_in[1], POLLOUT, 0 };
      if (!poll(&pfd, 1, 0) || !(pfd.revents & POLLOUT) ||
          (pfd.revents & (POLLHUP|POLLERR))) {
        return 0;
      }
#endif
      gdb_in = child_in[1];
      gdb_out = child_out[1];
      // Read the "(gdb) " prompt.
      char prompt[6];
      ReadBuffer(prompt, sizeof(prompt));
      write(gdb_in, "set prompt\n", 11);
      write(gdb_in, "set confirm 0\n", 14);
    }
  }
}

void SymbolTable::Finalize() {
  if (gdb_in != -1) {
    write(gdb_in, "quit\n", 5);
    close(gdb_in);
  }
  if (gdb_out != -1) close(gdb_out);
  finalized = true;
}

void SymbolTable::WriteHexAddr(uintptr_t addr) {
  char buf[50];
  memset(buf, 0, sizeof(buf));
  buf[0] = '0', buf[1] = 'x';
  int addr_len = 0;
  uintptr_t t_addr = addr;
  do {
    t_addr /= 16;
    addr_len++;
  } while (t_addr);
  for (int index = addr_len + 1; index >= 2; index--) {
    int digit = addr % 16;
    if ((digit >= 0) && (digit <= 9)) {
      buf[index] = '0' + digit;
    } else {
      buf[index] = 'a' + digit - 10;
    }
    addr /= 16;
  }
  write(gdb_in, buf, addr_len + 2);
}

void SymbolTable::ConsumeLines() {
  char buf[200];
  int total_read = 0;
  while (1) {
    int bytes_read = read(gdb_out, buf, sizeof(buf) - total_read);
    if (bytes_read < 0) {
      return;
    } else if (bytes_read == 0) {
      break;  // TODO(glider): or return?
    } else {
      total_read += bytes_read;
      if (buf[total_read-1] == '\n') break;
    }
  }
}

void SymbolTable::MapBinary(const char *path, int path_size) {
  write(gdb_in, "file ", 5);
  write(gdb_in, path, path_size);
  write(gdb_in, "\n", 1);
}

void SymbolTable::MapSharedLibrary(const char *path, int path_size,
                                   uintptr_t offset) {
  write(gdb_in, "add-symbol-file ", 16);
  write(gdb_in, path, path_size);
  write(gdb_in, " ", 1);
  WriteHexAddr(offset);
  write(gdb_in, "\n", 1);
  ConsumeLines();
}

bool SymbolTable::GetAddrInfoNocache(void *addr,
                                     /*out*/char *symbol, int symbol_size,
                                     /*out*/char *file, int file_size,
                                     /*out*/int *line) {
  write(gdb_in, "info line *", 11);
  WriteHexAddr((uintptr_t)addr);
  write(gdb_in, "\n", 1);
  char buf[1000];
  memset(buf, 0, sizeof(buf));
  ReadBuffer(buf, sizeof(buf));
  printf("%s\n", buf);
  if (strstr(buf, "No line") == buf) {
    return false;
  } else {
    // Assuming that we've got the line in the following format:
    //   Line 9 of "main.cc" starts at address 0x400b24 <main(int, char**)> \
    //     and ends at 0x400b41 <main(int, char**)+29>.
    const char kLine_[] = "Line ";
    assert(strstr(buf, kLine_) == buf);
    int index = sizeof(kLine_) - 1;  // without the trailing \0.
    int tmp_line = 0;
    while ((buf[index] >= '0') && (buf[index] <= '9')) {
      tmp_line *= 10;
      tmp_line += buf[index] - '0';
      index++;
    }
    *line = tmp_line;
    assert(buf[index] == ' ');
    char *start_file = &(buf[index+5]);  // consume " of \"".
    char *end_file = strstr(start_file, "\"");
    assert(end_file);
    int file_len = end_file - start_file;
    if (file_size > file_len) {
      strncpy(file, start_file, file_len);
      file[file_len] = '\0';
    } else {
      file[0] = '\0';
    }
    char *start_symbol = strstr(end_file, "<");
    assert(start_symbol);
    start_symbol++;  // consume "<".
    char *end_symbol = strstr(start_symbol, ">");
    assert(end_symbol);
    printf("%s\n", start_symbol);
    int symbol_len = end_symbol - start_symbol;
    if (symbol_size > symbol_len) {
      strncpy(symbol, start_symbol, symbol_len);
      symbol[symbol_len] = '\0';
    } else {
      symbol[0] = '\0';
    }
    return true;
  }
}

// /proc/self/maps line looks like follows:
//   7fb1c9d21000-7fb1c9d41000 r-xp 00000000 fc:00 1499148      /lib/ld-2.11.1.so
// We need the first field (address) and the last.

void SymbolTable::LoadSelfMaps() {
  char maps_line[1000];
  char read_buf[100];
  int maps_fd = open("/proc/self/maps", 0);

  close(maps_fd);
}
