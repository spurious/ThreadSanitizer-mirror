/*
  This file is part of ThreadSanitizer, a dynamic data race detector 
  based on Valgrind.

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

// Author: Konstantin Serebryany.

#ifndef TS_VALGRIND_CLIENT_REQUESTS_H__
#define TS_VALGRIND_CLIENT_REQUESTS_H__

#include "valgrind.h"

// see ts_handle_client_request for explanations.
enum {
  TSREQ_NOOP = VG_USERREQ_TOOL_BASE('T','S'),
  TSREQ_CLEAN_MEMORY,               
  TSREQ_MAIN_IN,
  TSREQ_MAIN_OUT,
  TSREQ_MALLOC,
  TSREQ_FREE,
  TSREQ_BENIGN_RACE,                
  TSREQ_EXPECT_RACE,                
  TSREQ_PCQ_CREATE,                 
  TSREQ_PCQ_DESTROY,                
  TSREQ_PCQ_PUT,                    
  TSREQ_PCQ_GET,                    
  TSREQ_TRACE_MEM,                  
  TSREQ_MUTEX_IS_USED_AS_CONDVAR,   
  TSREQ_IGNORE_READS_BEGIN,         
  TSREQ_IGNORE_READS_END,           
  TSREQ_IGNORE_WRITES_BEGIN,        
  TSREQ_IGNORE_WRITES_END,          
  TSREQ_SET_THREAD_NAME,
  TSREQ_SET_LOCK_NAME,
  TSREQ_IGNORE_ALL_ACCESSES_BEGIN,
  TSREQ_IGNORE_ALL_ACCESSES_END,
  TSREQ_IGNORE_ALL_SYNC_BEGIN,
  TSREQ_IGNORE_ALL_SYNC_END,
  TSREQ_PUBLISH_MEMORY_RANGE,       
  TSREQ_UNPUBLISH_MEMORY_RANGE,
  TSREQ_PRINT_MEMORY_USAGE,         
  TSREQ_PRINT_STATS,                
  TSREQ_RESET_STATS,                
  TSREQ_SET_MY_PTHREAD_T,
  TSREQ_PTH_API_ERROR,              
  TSREQ_PTHREAD_JOIN_POST,          
  TSREQ_PTHREAD_COND_SIGNAL_PRE,    
  TSREQ_PTHREAD_COND_BROADCAST_PRE, 
  TSREQ_PTHREAD_COND_WAIT_PRE, 
  TSREQ_PTHREAD_COND_WAIT_POST,
  TSREQ_PTHREAD_COND_TWAIT_POST,
  TSREQ_PTHREAD_RWLOCK_CREATE_POST,   
  TSREQ_PTHREAD_RWLOCK_DESTROY_PRE, 
  TSREQ_PTHREAD_RWLOCK_LOCK_PRE,    
  TSREQ_PTHREAD_RWLOCK_LOCK_POST,   
  TSREQ_PTHREAD_RWLOCK_UNLOCK_PRE,  
  TSREQ_PTHREAD_RWLOCK_UNLOCK_POST, 
  TSREQ_PTHREAD_SPIN_LOCK_INIT_OR_UNLOCK,
  TSREQ_POSIX_SEM_INIT_POST,        
  TSREQ_POSIX_SEM_DESTROY_PRE,      
  TSREQ_POSIX_SEM_POST_PRE,         
  TSREQ_POSIX_SEM_WAIT_POST,        
  TSREQ_GET_MY_SEGMENT,             
  TSREQ_GET_THREAD_ID,              
  TSREQ_GET_VG_THREAD_ID,
  TSREQ_GET_SEGMENT_ID
};
#endif // TS_VALGRIND_CLIENT_REQUESTS_H__
// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab
