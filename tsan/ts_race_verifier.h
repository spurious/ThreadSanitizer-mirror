/*
  This file is part of ThreadSanitizer, a dynamic data race detector.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/

// Author: Evgeniy Stepanov.

/*
  RaceVerifier is a tool for verifying race reports produced by ThreadSanitizer.
  It works by adding time delays after potentially racey instructions and making
  sure that they are executed simultaneously.

  To use RaceVerifier, save the stderr log of a ThreadSanitizer run to a file
  and run tsan again with --race-verifier=<log file name> option.
 */
#ifndef TS_RACE_VERIFIER_H_
#define TS_RACE_VERIFIER_H_

bool RaceVerifierGetAddresses(uintptr_t min_pc, uintptr_t max_pc,
    uintptr_t* instrument_pc);
bool RaceVerifierStartAccess(int thread_id, uintptr_t addr, uintptr_t pc,
    bool is_w);
void RaceVerifierEndAccess(int thread_id, uintptr_t addr, uintptr_t pc,
    bool is_w);

void RaceVerifierInit(const std::vector<std::string>& fileNames,
    const std::vector<std::string>& raceInfos);
void RaceVerifierFini();

#endif // TS_RACE_VERIFIER_H_
