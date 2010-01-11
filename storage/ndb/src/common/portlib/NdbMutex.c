/*
   Copyright (C) 2003 MySQL AB
    All rights reserved. Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include <ndb_global.h>

#include <NdbMutex.h>
#include <NdbMem.h>

NdbMutex* NdbMutex_Create(void)
{
  NdbMutex* pNdbMutex;
  int result;
  DBUG_ENTER("NdbMutex_Create");
  
  pNdbMutex = (NdbMutex*)NdbMem_Allocate(sizeof(NdbMutex));
  DBUG_PRINT("info",("NdbMem_Allocate 0x%lx", (long) pNdbMutex));
  
  if (pNdbMutex == NULL)
    DBUG_RETURN(NULL);
  
  result = pthread_mutex_init(pNdbMutex, NULL);
  assert(result == 0);
			     
  DBUG_RETURN(pNdbMutex);		     
}

int NdbMutex_Init(NdbMutex* pNdbMutex)
{
  int result;
  DBUG_ENTER("NdbMutex_Init");
  
  result = pthread_mutex_init(pNdbMutex, NULL);
  assert(result == 0);
			     
  DBUG_RETURN(result);
}

int NdbMutex_Destroy(NdbMutex* p_mutex)
{
  int result;
  DBUG_ENTER("NdbMutex_Destroy");

  if (p_mutex == NULL)
    DBUG_RETURN(-1);

  result = pthread_mutex_destroy(p_mutex);

  DBUG_PRINT("info",("NdbMem_Free 0x%lx", (long) p_mutex));
  NdbMem_Free(p_mutex);
			     
  DBUG_RETURN(result);

}


int NdbMutex_Lock(NdbMutex* p_mutex)
{
  int result;

  if (p_mutex == NULL)
    return -1;

  result = pthread_mutex_lock(p_mutex);
  
  return result;
}


int NdbMutex_Unlock(NdbMutex* p_mutex)
{
  int result;

  if (p_mutex == NULL)
    return -1;

  result = pthread_mutex_unlock(p_mutex);
			     
  return result;
}


int NdbMutex_Trylock(NdbMutex* p_mutex)
{
  int result = -1;

  if (p_mutex != NULL) {
    result = pthread_mutex_trylock(p_mutex);
  }

  return result;
}

