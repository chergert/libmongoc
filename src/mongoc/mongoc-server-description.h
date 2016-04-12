/*
 * Copyright 2014 MongoDB, Inc.
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

#ifndef MONGOC_SERVER_DESCRIPTION_H
#define MONGOC_SERVER_DESCRIPTION_H

#include <bson.h>

#include "mongoc-read-prefs.h"
#include "mongoc-host-list.h"

typedef enum
{
   MONGOC_SERVER_UNKNOWN,
   MONGOC_SERVER_STANDALONE,
   MONGOC_SERVER_MONGOS,
   MONGOC_SERVER_POSSIBLE_PRIMARY,
   MONGOC_SERVER_RS_PRIMARY,
   MONGOC_SERVER_RS_SECONDARY,
   MONGOC_SERVER_RS_ARBITER,
   MONGOC_SERVER_RS_OTHER,
   MONGOC_SERVER_RS_GHOST,
   /* Update MONGOC_SERVER_DESCRIPTION_TYPES when extending this enum */
} mongoc_server_description_type_t;

typedef struct _mongoc_server_description_t mongoc_server_description_t;

void
mongoc_server_description_destroy (mongoc_server_description_t *description);

mongoc_server_description_t *
mongoc_server_description_new_copy (const mongoc_server_description_t *description);

uint32_t
mongoc_server_description_id (mongoc_server_description_t *description);

mongoc_host_list_t *
mongoc_server_description_host (mongoc_server_description_t *description);

int64_t
mongoc_server_description_round_trip_time (mongoc_server_description_t *description);

mongoc_server_description_type_t
mongoc_server_description_type (const mongoc_server_description_t *description);

const bson_t *
mongoc_server_description_ismaster (mongoc_server_description_t *description);

#endif
