/* Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

/**
  @file storage/perfschema/pfs_digest.h
  Statement Digest data structures (implementation).
*/

#include "my_global.h"
#include "my_sys.h"
#include "pfs_instr.h"
#include "pfs_digest.h"
#include "pfs_global.h"
#include "p_lex.h"
#include "p_lex.cc"
#include <string.h>

unsigned int statements_digest_size= 0;
/** EVENTS_STATEMENTS_HISTORY_LONG circular buffer. */
PFS_statements_digest_stat *statements_digest_stat_array= NULL;
/** Consumer flag for table EVENTS_STATEMENTS_SUMMARY_BY_DIGEST. */
bool flag_statements_digest= true;
/** 
  Current index in Stat array where new record is to be inserted.
  index 0 is reserved for "all else" case when entire array is full.
*/
int digest_index= 1;

static LF_HASH digest_hash;
static bool digest_hash_inited= false;


static void get_digest_text(char* digest_text,
                            unsigned int* token_array,
                            int token_count);
const char* symbol[MAX_TOKEN_COUNT];

/**
  Initialize table EVENTS_STATEMENTS_SUMMARY_BY_DIGEST.
  @param digest_sizing      
*/
int init_digest(unsigned int statements_digest_sizing)
{
  unsigned int index;

  initialize_lex_symbol();

  /* 
    TBD. Allocate memory for statements_digest_stat_array based on 
    performance_schema_digests_size values
  */
  statements_digest_size= statements_digest_sizing;
 
  if (statements_digest_size == 0)
    return 0;

  statements_digest_stat_array=
    PFS_MALLOC_ARRAY(statements_digest_size, PFS_statements_digest_stat,
                     MYF(MY_ZEROFILL));
   
  for (index= 0; index < statements_digest_size; index++)
  statements_digest_stat_array[index].m_stat.reset();


  return (statements_digest_stat_array ? 0 : 1);
}

/** Cleanup table EVENTS_STATEMENTS_SUMMARY_BY_DIGEST. */
void cleanup_digest(void)
{
  /* 
    TBD. Free memory allocated to statements_digest_stat_array. 
  */
  pfs_free(statements_digest_stat_array);
  statements_digest_stat_array= NULL;
}

C_MODE_START
static uchar *digest_hash_get_key(const uchar *entry, size_t *length,
                                my_bool)
{
  const PFS_statements_digest_stat * const *typed_entry;
  const PFS_statements_digest_stat *digest;
  const void *result;
  typed_entry= reinterpret_cast<const PFS_statements_digest_stat*const*>(entry);
  DBUG_ASSERT(typed_entry != NULL);
  digest= *typed_entry;
  DBUG_ASSERT(digest != NULL);
  *length= 16; 
  result= digest->m_md5_hash.m_md5;
  return const_cast<uchar*> (reinterpret_cast<const uchar*> (result));
}
C_MODE_END


/**
  Initialize the digest hash.
  @return 0 on success
*/
int init_digest_hash(void)
{
  if (! digest_hash_inited)
  {
    lf_hash_init(&digest_hash, sizeof(PFS_statements_digest_stat*),
                 LF_HASH_UNIQUE, 0, 0, digest_hash_get_key,
                 &my_charset_bin);
    digest_hash_inited= true;
  }
  return 0;
}

/** Cleanup the digest hash. */
void cleanup_digest_hash(void)
{
  if (digest_hash_inited)
  {
    lf_hash_destroy(&digest_hash);
    digest_hash_inited= false;
  }
}

static LF_PINS* get_digest_hash_pins(PFS_thread *thread)
{
  if (unlikely(thread->m_digest_hash_pins == NULL))
  {
    if (!digest_hash_inited)
      return NULL;
    thread->m_digest_hash_pins= lf_hash_get_pins(&digest_hash);
  }
  return thread->m_digest_hash_pins;
}

PFS_statements_digest_stat* 
find_or_create_digest(PFS_thread* thread, PFS_digest_storage* digest_storage)
/*
                               unsigned char* hash_key,
                               unsigned int* token_array,
                               int token_count,
                               char* digest_text,
                               unsigned int digest_text_length)
*/
{
  /* get digest pin. */
  LF_PINS *pins= get_digest_hash_pins(thread);
  /* There shoulod be at least one token. */
  if(unlikely(pins == NULL) || !(digest_storage->m_token_count > 0))
  {
    return NULL;
  }

  unsigned char* hash_key= digest_storage->m_digest_hash.m_md5;
  unsigned int* token_array= digest_storage->m_token_array; 
  int token_count= digest_storage->m_token_count;
 
  PFS_statements_digest_stat **entry;
  PFS_statements_digest_stat *pfs= NULL;

  /* Lookup LF_HASH using this new key. */
  entry= reinterpret_cast<PFS_statements_digest_stat**>
    (lf_hash_search(&digest_hash, pins,
                    hash_key, 16));

  if(!entry)
  {
    /* 
      If statement digest entry doesn't exist.
    */
    //printf("\n Doesn't Exist. Adding new entry. \n");

    if(digest_index==0)
    {
      /*
        digest_stat array is full. Add stat at index 0 and return.
      */
      pfs= &statements_digest_stat_array[0];
      //TODO
      return pfs;
    }

    /* 
      Add a new record in digest stat array. 
    */
    pfs= &statements_digest_stat_array[digest_index];
    
    /* Calculate and set digest text. */
    get_digest_text(pfs->m_digest_text,token_array,token_count);
    pfs->m_digest_text_length= strlen(pfs->m_digest_text);

    /* Set digest hash/LF Hash search key. */
    memcpy(pfs->m_md5_hash.m_md5, hash_key, 16);
    
    /* Increment index. */
    digest_index++;
    
    if(digest_index%statements_digest_size == 0)
    {
      /* 
        Digest stat array is full. Now all stat for all further 
        entries will go into index 0.
      */
      digest_index= 0;
    }
    
    /* Add this new digest into LF_HASH */
    int res;
    res= lf_hash_insert(&digest_hash, pins, &pfs);
    lf_hash_search_unpin(pins);
    if (res > 0)
    {
      /* TODO: Handle ERROR CONDITION */
      return NULL;
    }
    return pfs;
  }
  else if (entry && (entry != MY_ERRPTR))
  {
    /* 
      If stmt digest already exists, update stat and return 
    */
    //printf("\n Already Exists \n");
    pfs= *entry;
    lf_hash_search_unpin(pins);
    return pfs;
  }

  return NULL;
}
 
void reset_esms_by_digest()
{
  /*TBD*/ 
}

/*
  This function, iterates token array and updates digest_text.
*/
static void get_digest_text(char* digest_text,
                            unsigned int* token_array,
                            int token_count)
{
  int i= 0;
  while(i<token_count)
  {
    if(token_array[i] < 258) 
    {
      *digest_text= (char)token_array[i];
      digest_text++;
      i++;
      continue;
    }
    else
    {
      /* 
         For few tokens (like IDENT_QUOTED, which is token for all
         variables/literals), there is no string defined, so making them
         '?' as of now.
         TODO : do it properly.
      */
      if(symbol[token_array[i]-START_TOKEN_NUMBER]!=NULL)
      {
        strncpy(digest_text, symbol[token_array[i]-START_TOKEN_NUMBER],
                           strlen(symbol[token_array[i]-START_TOKEN_NUMBER]));
      }
      else
      {
        *digest_text= '?';
      }
      digest_text+= strlen(digest_text);
      i++;
    }
    *(digest_text)= ' ';
    digest_text++;
  }
  digest_text= '\0';
}

