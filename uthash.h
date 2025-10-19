/*
Copyright (c) 2003-2022, Troy D. Hanson     http://troydhanson.github.io/uthash/
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef UTHASH_H
#define UTHASH_H

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#define HASH_ADD(hh,head,fieldname,keylen_in,add) \
        HASH_ADD_KEYPTR(hh,head,&((add)->fieldname),keylen_in,add)

#define HASH_ADD_KEYPTR(hh,head,keyptr,keylen_in,add) do { \
 (add)->hh.key = (char*)(keyptr); \
 (add)->hh.keylen = (unsigned)(keylen_in); \
 HASH_ADD_TO_TABLE(hh,head,add); \
} while (0)

#define HASH_ADD_TO_TABLE(hh,head,add) do { \
 (add)->hh.next = NULL; \
 (add)->hh.prev = NULL; \
 if (head) { \
   (add)->hh.next = (head); \
   (head)->hh.prev = (add); \
 } \
 (head) = (add); \
} while (0)

#define HASH_FIND(hh,head,findkey,keylen,out) \
        HASH_FIND_BYHASHVALUE(hh,head,findkey,keylen,0,out)

#define HASH_FIND_BYHASHVALUE(hh,head,findkey,keylen,hashval,out) do { \
  out = NULL; \
  if (head) { \
    typeof(head) _hf_tmp; \
    for (_hf_tmp = (head); _hf_tmp; _hf_tmp = _hf_tmp->hh.next) { \
      if ((_hf_tmp->hh.keylen == (unsigned)(keylen)) && \
          (memcmp(_hf_tmp->hh.key, findkey, keylen) == 0)) { \
        out = _hf_tmp; \
        break; \
      } \
    } \
  } \
} while (0)

#define HASH_DELETE(hh,head,delptr) do { \
  if ((delptr)->hh.prev) { \
    (delptr)->hh.prev->hh.next = (delptr)->hh.next; \
  } else { \
    (head) = (delptr)->hh.next; \
  } \
  if ((delptr)->hh.next) { \
    (delptr)->hh.next->hh.prev = (delptr)->hh.prev; \
  } \
} while (0)

#define HASH_COUNT(head) \
    ({ unsigned _count = 0; \
       typeof(head) _ctr; \
       for (_ctr = (head); _ctr; _ctr = _ctr->hh.next) { _count++; } \
       _count; })

#define HASH_ITER(hh,head,el,tmp) \
  for((el)=(head),(tmp)=((head)?(head)->hh.next:NULL); \
      (el); (el)=(tmp),(tmp)=((tmp)?(tmp)->hh.next:NULL))

typedef struct UT_hash_handle {
   void *next;
   void *prev;
   void *key;
   unsigned keylen;
} UT_hash_handle;

#endif
