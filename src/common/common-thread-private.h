/*
 * Copyright 2013-present MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common-prelude.h"
#include "common-config.h"
#include "common-macros.h"

#ifndef COMMON_THREAD_PRIVATE_H
#define COMMON_THREAD_PRIVATE_H

#define BSON_INSIDE
#include "bson/bson-compat.h"
#include "bson/bson-config.h"
#include "bson/bson-macros.h"
#undef BSON_INSIDE

BSON_BEGIN_DECLS

#if defined(BSON_OS_UNIX)
#include <pthread.h>

#define BSON_ONCE_FUN(n) void n (void)
#define BSON_ONCE_RETURN return
#define BSON_ONCE_INIT PTHREAD_ONCE_INIT
#define bson_once pthread_once
#define bson_once_t pthread_once_t
#define bson_thread_t pthread_t
#define BSON_THREAD_FUN(_function_name, _arg_name) \
   void *(_function_name) (void *(_arg_name))
#define BSON_THREAD_FUN_TYPE(_function_name) void *(*(_function_name)) (void *)
#define BSON_THREAD_RETURN return NULL

/* this macro can be defined as a as a build configuration option
 * with -DENABLE_TESTING=ON.  its purpose is to allow for functions
 * that require a mutex to be locked on entry to assert that the mutex
 * is actually locked.
 * this can prevent bugs where a caller forgets to lock the mutex. */

#ifndef MONGOC_ENABLE_TESTING

#define bson_mutex_destroy pthread_mutex_destroy
#define bson_mutex_init(_n) pthread_mutex_init ((_n), NULL)
#define bson_mutex_lock pthread_mutex_lock
#define bson_mutex_t pthread_mutex_t
#define bson_mutex_unlock pthread_mutex_unlock

#else
typedef struct {
   pthread_t lock_owner;
   pthread_mutex_t wrapped_mutex;
   bool valid_tid;
} bson_mutex_t;

#define bson_mutex_destroy(mutex)                      \
   do {                                                \
      pthread_mutex_destroy (&(mutex)->wrapped_mutex); \
   } while (0);

#define bson_mutex_init(mutex)                            \
   do {                                                   \
      pthread_mutex_init (&(mutex)->wrapped_mutex, NULL); \
      (mutex)->valid_tid = false;                         \
   } while (0);

#define bson_mutex_lock(mutex)                      \
   do {                                             \
      pthread_mutex_lock (&(mutex)->wrapped_mutex); \
      (mutex)->lock_owner = pthread_self ();        \
      (mutex)->valid_tid = true;                    \
   } while (0);

#define bson_mutex_unlock(mutex)                      \
   do {                                               \
      pthread_mutex_unlock (&(mutex)->wrapped_mutex); \
      (mutex)->valid_tid = false;                     \
   } while (0);

#endif

#else
#include <process.h>
#define BSON_ONCE_FUN(n) \
   BOOL CALLBACK n (PINIT_ONCE _ignored_a, PVOID _ignored_b, PVOID *_ignored_c)
#define BSON_ONCE_INIT INIT_ONCE_STATIC_INIT
#define BSON_ONCE_RETURN return true
#define bson_mutex_destroy DeleteCriticalSection
#define bson_mutex_init InitializeCriticalSection
#define bson_mutex_lock EnterCriticalSection
#define bson_mutex_t CRITICAL_SECTION
#define bson_mutex_unlock LeaveCriticalSection
#define bson_once(o, c) InitOnceExecuteOnce (o, c, NULL, NULL)
#define bson_once_t INIT_ONCE
#define bson_thread_t HANDLE
#define BSON_THREAD_FUN(_function_name, _arg_name) \
   unsigned(__stdcall _function_name) (void *(_arg_name))
#define BSON_THREAD_FUN_TYPE(_function_name) \
   unsigned(__stdcall * _function_name) (void *)
#define BSON_THREAD_RETURN return
#endif

/* Functions that require definitions get the common prefix (_mongoc for
 * libmongoc or _bson for libbson) to avoid duplicate symbols when linking both
 * libbson and libmongoc statically. */
int COMMON_PREFIX (thread_join) (bson_thread_t thread);
int COMMON_PREFIX (thread_create) (bson_thread_t *thread,
                                   BSON_THREAD_FUN_TYPE (func),
                                   void *arg);

#if defined(MONGOC_ENABLE_TESTING) && defined(BSON_OS_UNIX)
bool COMMON_PREFIX (mutex_is_locked) (bson_mutex_t *mutex);
#endif

BSON_END_DECLS

#endif /* COMMON_THREAD_PRIVATE_H */
