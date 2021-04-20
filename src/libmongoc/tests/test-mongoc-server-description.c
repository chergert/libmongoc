/*
 * Copyright 2020-present MongoDB, Inc.
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

#include "mongoc/mongoc.h"
#include "mongoc/mongoc-client-private.h"
#include "mongoc/mongoc-server-description-private.h"
#include "TestSuite.h"

void
reset_basic_sd (mongoc_server_description_t *sd)
{
   bson_error_t error;
   bson_t *ismaster;

   ismaster = BCON_NEW ("minWireVersion",
                        BCON_INT32 (0),
                        "maxWireVersion",
                        BCON_INT32 (WIRE_VERSION_MAX));

   mongoc_server_description_reset (sd);
   memset (&error, 0, sizeof (bson_error_t));
   mongoc_server_description_handle_ismaster (
      sd, ismaster, 0 /* rtt */, &error);
   bson_destroy (ismaster);
}

/* These checks will start failing and need to be updated once CDRIVER-3527 is
 * addressed. */
static void
_test_hostlist (mongoc_server_description_t *sd1,
                bson_t *sd1_hostlist,
                mongoc_server_description_t *sd2,
                bson_t *sd2_hostlist)
{
   bson_reinit (sd1_hostlist);
   bson_reinit (sd2_hostlist);
   BSON_ASSERT (_mongoc_server_description_equal (sd1, sd2));

   /* [ "h1" ] vs [] */
   BSON_APPEND_UTF8 (sd1_hostlist, "0", "h1");
   BSON_ASSERT (!_mongoc_server_description_equal (sd1, sd2));

   /* [ "h1" ] vs [ "h1" ] */
   BSON_APPEND_UTF8 (sd2_hostlist, "0", "h1");
   BSON_ASSERT (_mongoc_server_description_equal (sd1, sd2));

   /* [ "h1", "h2", "h3" ] vs [ "h1" ] */
   BSON_APPEND_UTF8 (sd1_hostlist, "1", "h2");
   BSON_APPEND_UTF8 (sd1_hostlist, "2", "h3");
   BSON_ASSERT (!_mongoc_server_description_equal (sd1, sd2));

   /* [ "h1", "h2", "h3" ] vs [ "h1", "h3", "h2" ]. Considered unequal since we
    * do not do a set comparison. */
   BSON_APPEND_UTF8 (sd2_hostlist, "1", "h3");
   BSON_APPEND_UTF8 (sd2_hostlist, "2", "h2");
   BSON_ASSERT (!_mongoc_server_description_equal (sd1, sd2));

   /* [ "h1", "h1" ] vs [ "h1" ]. Considered unequal since we do not do a set
    * comparison. */
   bson_reinit (sd1_hostlist);
   bson_reinit (sd2_hostlist);
   BSON_APPEND_UTF8 (sd1_hostlist, "0", "h1");
   BSON_APPEND_UTF8 (sd1_hostlist, "1", "h1");
   BSON_APPEND_UTF8 (sd2_hostlist, "0", "h1");
   BSON_ASSERT (!_mongoc_server_description_equal (sd1, sd2));

   /* Arbitrary non-array like equal docs. Considered equal since we don't do
    * any parsing. */
   bson_reinit (sd1_hostlist);
   bson_reinit (sd2_hostlist);
   BSON_APPEND_UTF8 (sd1_hostlist, "test", "h1");
   BSON_APPEND_UTF8 (sd2_hostlist, "test", "h1");
   BSON_ASSERT (_mongoc_server_description_equal (sd1, sd2));
}

/* Unit test of server description. */
/* Check that variations on all (=) fields result in inequal, and variations on
 * all non (=) fields do not */
void
test_server_description_equal (void)
{
   mongoc_server_description_t sd1;
   mongoc_server_description_t sd2;

   mongoc_server_description_init (&sd1, "host:1234", 1);
   mongoc_server_description_init (&sd2, "host:1234", 2);

   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   BSON_ASSERT (_mongoc_server_description_equal (&sd1, &sd2));

   /* "address" differs, still considered equal. */
   sd2.connection_address = "host2:5678";
   BSON_ASSERT (_mongoc_server_description_equal (&sd1, &sd2));

   /* "roundTripTime" differs, still considered equal. */
   sd1.round_trip_time_msec = 1234;
   BSON_ASSERT (_mongoc_server_description_equal (&sd1, &sd2));

   /* "lastWriteDate"/"opTime" are stored in last_hello_response and not parsed
    * out. Check that overwriting the stored reply does not factor into the
    * equality check. */
   bson_reinit (&sd1.last_hello_response);
   BSON_ASSERT (_mongoc_server_description_equal (&sd1, &sd2));

   /* "error" differs, considered unequal. */
   bson_set_error (&sd1.error, MONGOC_ERROR_SERVER, 123, "some error");
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "type" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.type = MONGOC_SERVER_RS_GHOST;
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "minWireVersion" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.min_wire_version = 2;
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "maxWireVersion" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.max_wire_version = 7;
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "me" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.me = "test:1234";
   sd2.me = "test:1235";
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* But if "me" only differs only in case, considered equal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.me = "tesT:1234";
   sd2.me = "test:1234";
   BSON_ASSERT (_mongoc_server_description_equal (&sd1, &sd2));

   /* Test variations of "hosts", "passives", and "arbiters". */
   _test_hostlist (&sd1, &sd1.hosts, &sd2, &sd2.hosts);
   _test_hostlist (&sd1, &sd1.passives, &sd2, &sd2.passives);
   _test_hostlist (&sd1, &sd1.arbiters, &sd2, &sd2.arbiters);

   /* "tags" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   bson_reinit (&sd1.tags);
   BSON_APPEND_UTF8 (&sd1.tags, "tag", "nyc");
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "setName" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.set_name = "set";
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "setName" differs by case only, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.set_name = "set";
   sd2.set_name = "SET";
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "setVersion" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.set_version = 1;
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "electionId" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   bson_oid_init_from_string (&sd1.election_id, "000000000000000000001234");
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "primary" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.current_primary = "host";
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "primary" differs in case only, considered equal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.current_primary = "host";
   sd2.current_primary = "HOST";
   BSON_ASSERT (_mongoc_server_description_equal (&sd1, &sd2));

   /* "logicalSessionTimeoutMinutes" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   sd1.session_timeout_minutes = 1;
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   /* "compressors" differs, still considered equal since that is only
    * applicable for handshake. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   bson_reinit (&sd1.compressors);
   BSON_APPEND_UTF8 (&sd1.compressors, "0", "zstd");
   BSON_ASSERT (_mongoc_server_description_equal (&sd1, &sd2));

   /* "topologyVersion" differs, considered unequal. */
   reset_basic_sd (&sd1);
   reset_basic_sd (&sd2);
   BCON_APPEND (&sd1.topology_version, "x", BCON_INT32 (1));
   BSON_ASSERT (!_mongoc_server_description_equal (&sd1, &sd2));

   mongoc_server_description_cleanup (&sd1);
   mongoc_server_description_cleanup (&sd2);
}

/* Test that msg set to anything else besides "isdbgrid" is not considered
 * server type mongos */
void
test_server_description_msg_without_isdbgrid (void)
{
   mongoc_server_description_t sd;
   bson_t *ismaster;
   bson_error_t error;

   mongoc_server_description_init (&sd, "host:1234", 1);
   ismaster = BCON_NEW ("minWireVersion",
                        BCON_INT32 (0),
                        "maxWireVersion",
                        BCON_INT32 (WIRE_VERSION_MAX),
                        "msg",
                        "isdbgrid");
   memset (&error, 0, sizeof (bson_error_t));
   mongoc_server_description_handle_ismaster (
      &sd, ismaster, 0 /* rtt */, &error);
   BSON_ASSERT (sd.type == MONGOC_SERVER_MONGOS);

   mongoc_server_description_reset (&sd);
   bson_destroy (ismaster);
   ismaster = BCON_NEW ("minWireVersion",
                        BCON_INT32 (0),
                        "maxWireVersion",
                        BCON_INT32 (WIRE_VERSION_MAX),
                        "msg",
                        "something_else");
   mongoc_server_description_handle_ismaster (
      &sd, ismaster, 0 /* rtt */, &error);
   BSON_ASSERT (sd.type == MONGOC_SERVER_STANDALONE);

   bson_destroy (ismaster);
   mongoc_server_description_cleanup (&sd);
}

static void
test_server_description_ignores_rtt (void)
{
   mongoc_server_description_t sd;
   bson_error_t error;
   bson_t ismaster;

   bson_init (&ismaster);
   BCON_APPEND (&ismaster, "ismaster", BCON_BOOL (true));

   memset (&error, 0, sizeof (bson_error_t));
   mongoc_server_description_init (&sd, "host:1234", 1);
   /* Initially, the RTT is MONGOC_RTT_UNSET. */
   ASSERT_CMPINT64 (sd.round_trip_time_msec, ==, MONGOC_RTT_UNSET);
   BSON_ASSERT (sd.type == MONGOC_SERVER_UNKNOWN);
   /* If MONGOC_RTT_UNSET is passed as the RTT, it remains MONGOC_RTT_UNSET. */
   mongoc_server_description_handle_ismaster (
      &sd, &ismaster, MONGOC_RTT_UNSET, &error);
   ASSERT_CMPINT64 (sd.round_trip_time_msec, ==, MONGOC_RTT_UNSET);
   BSON_ASSERT (sd.type == MONGOC_SERVER_STANDALONE);
   /* The first real RTT overwrites the stored RTT. */
   mongoc_server_description_handle_ismaster (&sd, &ismaster, 10, &error);
   ASSERT_CMPINT64 (sd.round_trip_time_msec, ==, 10);
   BSON_ASSERT (sd.type == MONGOC_SERVER_STANDALONE);
   /* But subsequent MONGOC_RTT_UNSET values do not effect it. */
   mongoc_server_description_handle_ismaster (
      &sd, &ismaster, MONGOC_RTT_UNSET, &error);
   ASSERT_CMPINT64 (sd.round_trip_time_msec, ==, 10);
   BSON_ASSERT (sd.type == MONGOC_SERVER_STANDALONE);

   mongoc_server_description_cleanup (&sd);
   bson_destroy (&ismaster);
}

void
test_server_description_install (TestSuite *suite)
{
   TestSuite_Add (
      suite, "/server_description/equal", test_server_description_equal);
   TestSuite_Add (suite,
                  "/server_description/msg_without_isdbgrid",
                  test_server_description_msg_without_isdbgrid);
   TestSuite_Add (suite,
                  "/server_description/ignores_unset_rtt",
                  test_server_description_ignores_rtt);
}
