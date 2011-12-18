/*
 Copyright (c) 2011, Oracle and/or its affiliates. All rights
 reserved.
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; version 2 of
 the License.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 02110-1301  USA
 */

/* System headers */
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Memcache headers */
#include "memcached/types.h"
#include "memcached/extension_loggers.h"
#include "memcached/server_api.h"

#include "ndb_error_logger.h"


/* ***********************************************************************
   ndb_error_logger 

   Log NDB error messages, but try to avoid flooding the logfile with them. 
   *********************************************************************** 
*/


/* Memcached externals */
extern EXTENSION_LOGGER_DESCRIPTOR *logger;
SERVER_CORE_API * core_api;
size_t verbose_logging;

/* Internal Static Globals and Declarations */
#define ERROR_HASH_TABLE_SIZE 251
pthread_mutex_t error_table_lock;
class ErrorEntry;
ErrorEntry * error_hash_table[ERROR_HASH_TABLE_SIZE];

/* Prototypes */
void manage_error(const NdbError &, const char * mesg, int interval);



/********* PUBLIC API *************************************/
/* Initialize the NDB Error Logger */
void ndb_error_logger_init(SERVER_CORE_API * api, size_t level) {
  int r = pthread_mutex_init(& error_table_lock, NULL);
  if(r) logger->log(LOG_WARNING,0, "CANNOT INIT ERROR MUTEX: %d\n", r);
  core_api = api;
  verbose_logging = level;
  
  for(int i = 0; i < ERROR_HASH_TABLE_SIZE; i++) 
    error_hash_table[i] = 0;
}


int log_ndb_error(const NdbError &error) {
  switch(error.status) {
    case ndberror_st_success:
      break;

    case ndberror_st_temporary:
      manage_error(error, "NDB Temporary Error", 10);
      break;

    case ndberror_st_permanent:
    case ndberror_st_unknown:
      manage_error(error, "NDB Error", 10);
      break;
  }
  /* NDB classifies "Out Of Memory" (827) errors as permament errors, but we 
     reclassify them to temporary */
  if(error.classification == NdbError::InsufficientSpace)
    return ERR_TEMP;
  return error.status;
}


/********* IMPLEMENTATION *******************************/

class ErrorEntry {
public:
  unsigned int error_code;
  rel_time_t first;
  rel_time_t time[2];   /* odd and even timestamps */
  Uint32 count;
  ErrorEntry *next;
  
  ErrorEntry(int code, rel_time_t tm) :
    error_code(code), first(tm), count(1), next(0) 
  { 
    time[0] = 0;
    time[1] = tm;
  };
};


class Lock {
public:
  pthread_mutex_t *mutex;
  int status;
  Lock(pthread_mutex_t *m) : mutex(m) { status = pthread_mutex_lock(mutex); }
  ~Lock()                             { pthread_mutex_unlock(mutex); }
};

ErrorEntry * error_table_lookup(int code, rel_time_t now);


/* Lock the error table and look up an error. 
   If found, increment the count and set either the odd or even timestamp.
   If not found, create.
*/
ErrorEntry * error_table_lookup(int code, rel_time_t now) {
  int hash_val = code % ERROR_HASH_TABLE_SIZE;
  Lock lock(& error_table_lock);
  ErrorEntry *sym;
  
  for(sym = error_hash_table[hash_val] ; sym != 0 ; sym = sym->next) {
    if(sym->error_code == code) {
      sym->time[(++(sym->count)) % 2] = now;
      return sym;
    }
  }

  /* Create */
  sym = new ErrorEntry(code, now);
  sym->next = error_hash_table[hash_val];
  error_hash_table[hash_val] = sym;
  return sym;
}


/* Record the error message, and possibly log it. */
void manage_error(const NdbError & error, const char *type_mesg, int interval) {
  char note[256];
  ErrorEntry *entry = 0;
  bool first_ever, interval_passed, flood = false;
  int current = 0, prior = 0;  // array indexes

  if(verbose_logging == 0) { 
    entry = error_table_lookup(error.code, core_api->get_current_time());

    if((entry->count | 1) == entry->count)
      current = 1;  // odd count
    else
      prior   = 1;  // even count

    /* We have four pieces of information: the first timestamp, the two 
       most recent timestamps, and the error count. When to write a log message?
       (A) On the first occurrence of an error. 
       (B) If a time > interval has passed since the previous message.
       (C) At certain count numbers in error flood situations
    */
    first_ever = (entry->count == 1);
    interval_passed = (entry->time[current] - entry->time[prior] > interval);
    if(! interval_passed) 
      for(int i = 10 ; i <= entry->count ; i *= 10) 
        if(entry->count < (10 * i) && (entry->count % i == 0))
          { flood = true; break; }
  }
  
  if(verbose_logging || first_ever || interval_passed || flood) 
  {
    if(flood) 
      snprintf(note, 256, "[occurrence %d of this error]", entry->count);
    else
      note[0] = '\0';
    logger->log(LOG_WARNING, 0, "%s %d: %s %s\n", 
                type_mesg, error.code, error.message, note);
  }
}
  
