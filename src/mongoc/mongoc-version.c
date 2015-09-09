/*
 * Copyright 2015 MongoDB, Inc.
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


#include "mongoc.h"
#include "mongoc-version.h"

/**
 * mongoc_get_major_version:
 *
 * Helper function to return the runtime major version of the library.
 */
int
mongoc_get_major_version (void)
{
   return MONGOC_MAJOR_VERSION;
}


/**
 * mongoc_get_minor_version:
 *
 * Helper function to return the runtime minor version of the library.
 */
int
mongoc_get_minor_version (void)
{
   return MONGOC_MINOR_VERSION;
}

/**
 * mongoc_get_micro_version:
 *
 * Helper function to return the runtime micro version of the library.
 */
int
mongoc_get_micro_version (void)
{
   return MONGOC_MICRO_VERSION;
}

/**
 * mongoc_get_version:
 *
 * Helper function to return the runtime string version of the library.
 */
const char *
mongoc_get_version (void)
{
   return MONGOC_VERSION_S;
}

/**
 * mongoc_check_version:
 *
 * Helper function to check the runtime string version of the library.
 * return NULL it met the required version, else return the version string.
 */
const char *
mongoc_check_version (int requiredmajor, int requiredminor, int requiredmicro)
{
   return (MONGOC_CHECK_VERSION(requiredmajor, requiredminor, requiredmicro) ? NULL : MONGOC_VERSION_S);
}
