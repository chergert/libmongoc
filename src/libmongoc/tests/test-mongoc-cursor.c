#include <mongoc/mongoc.h>
#include <mongoc/mongoc-client-private.h>

#include "TestSuite.h"
#include "test-libmongoc.h"
#include "mock_server/mock-rs.h"
#include "mock_server/future-functions.h"
#include "mongoc/mongoc-cursor-private.h"
#include "mongoc/mongoc-collection-private.h"
#include "mongoc/mongoc-read-concern-private.h"
#include "mongoc/mongoc-write-concern-private.h"
#include "test-conveniences.h"

/*
 * Prevent failing on pedantic GCC/clang warning: "ISO C forbids conversion of
 * function pointer to object pointer type."
 */
#ifdef __clang__
#pragma clang diagnostic warning "-Wpedantic"
#elif __GNUC__ > 6
#pragma GCC diagnostic warning "-Wpedantic"
#elif __GNUC__ <= 6
#pragma GCC diagnostic warning "-pedantic"
#endif

#define CURSOR_COMMON_SETUP                                         \
   do {                                                             \
      bson_error_t _err;                                            \
      bool _ret;                                                    \
      client = test_framework_new_default_client ();                \
      coll = mongoc_client_get_collection (client, "test", "test"); \
      /* populate to ensure db and coll exist. */                   \
      _ret = mongoc_collection_insert_one (                         \
         coll, tmp_bson ("{}"), NULL, NULL, &_err);                 \
      ASSERT_OR_PRINT (_ret, _err);                                 \
      ctor = (make_cursor_fn) ctx;                                  \
   } while (0)

#define CURSOR_COMMON_TEARDOWN          \
   do {                                 \
      mongoc_collection_destroy (coll); \
      mongoc_client_destroy (client);   \
   } while (0)

typedef mongoc_cursor_t *(*make_cursor_fn) (mongoc_collection_t *);

static mongoc_cursor_t *
_make_cmd_deprecated_cursor (mongoc_collection_t *coll);

static mongoc_cursor_t *
_make_array_cursor (mongoc_collection_t *coll);

/* test that the host a cursor returns belongs to a server it connected to. */
static void
_test_common_get_host (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;
   const mongoc_host_list_t *hosts;
   mongoc_host_list_t cursor_host;
   mongoc_uri_t *uri;
   const bson_t *doc;
   bson_error_t err;
   bool ret;

   CURSOR_COMMON_SETUP;
   cursor = ctor (coll);
   uri = test_framework_get_uri ();
   hosts = mongoc_uri_get_hosts (uri);
   ret = mongoc_cursor_next (cursor, &doc);
   if (!ret && mongoc_cursor_error (cursor, &err)) {
      test_error ("%s", err.message);
      abort ();
   }
   mongoc_cursor_get_host (cursor, &cursor_host);
   /* In a production deployment the driver can discover servers not in the seed
    * list, but for this test assume the cursor uses one of the seeds. */
   while (hosts) {
      if (strcmp (cursor_host.host_and_port, hosts->host_and_port) == 0) {
         /* the cursor is using this server */
         ASSERT_CMPSTR (cursor_host.host, hosts->host);
         ASSERT_CMPINT (cursor_host.port, ==, hosts->port);
         ASSERT_CMPINT (cursor_host.family, ==, hosts->family);
         break;
      }
      hosts = hosts->next;
   }
   mongoc_uri_destroy (uri);
   mongoc_cursor_destroy (cursor);
   CURSOR_COMMON_TEARDOWN;
}


/* test cloning cursors returns the same results. */
static void
_test_common_clone (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;
   mongoc_cursor_t *cloned;
   const bson_t *doc;
   bson_error_t err;

   CURSOR_COMMON_SETUP;
   cursor = ctor (coll);
   cloned = mongoc_cursor_clone (cursor);
   /* check that both cursors return the same number of documents. don't check
    * that they return the same exact documents. A cursor on the listDatabases
    * returns the database size, which may change in the background. */
   while (mongoc_cursor_next (cursor, &doc)) {
      BSON_ASSERT (mongoc_cursor_next (cloned, &doc));
      ASSERT_OR_PRINT (!mongoc_cursor_error (cloned, &err), err);
   }
   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &err), err);
   BSON_ASSERT (!mongoc_cursor_next (cloned, &doc));
   ASSERT_OR_PRINT (!mongoc_cursor_error (cloned, &err), err);
   mongoc_cursor_destroy (cursor);
   mongoc_cursor_destroy (cloned);
   CURSOR_COMMON_TEARDOWN;
}


/* test cloning cursors with read and write concerns set. */
static void
_test_common_clone_w_concerns (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;
   mongoc_cursor_t *cloned;
   mongoc_read_concern_t *read_concern;
   mongoc_write_concern_t *write_concern;
   const bson_t *bson;
   bson_iter_t iter;

   CURSOR_COMMON_SETUP;
   cursor = ctor (coll);
   read_concern = mongoc_read_concern_new ();
   ASSERT (read_concern);
   mongoc_read_concern_set_level (read_concern,
                                  MONGOC_READ_CONCERN_LEVEL_LOCAL);
   write_concern = mongoc_write_concern_new ();
   ASSERT (write_concern);
   mongoc_write_concern_set_fsync (write_concern, true);
   mongoc_write_concern_set_journal (write_concern, true);
   mongoc_write_concern_set_wmajority (write_concern, 1000);
   cursor->write_concern = write_concern;
   mongoc_read_concern_destroy (cursor->read_concern);
   cursor->read_concern = read_concern;
   /* don't call mongoc_cursor_next (), since the test may run against a version
    * of MongoDB that doesn't support read/write concerns, and we are only
    * interested in testing if the clone process works. */
   cloned = mongoc_cursor_clone (cursor);
   /* test cloned read_concern. */
   ASSERT (!mongoc_read_concern_is_default (cloned->read_concern));
   ASSERT_CMPSTR (mongoc_read_concern_get_level (cloned->read_concern),
                  MONGOC_READ_CONCERN_LEVEL_LOCAL);
   /* test cloned write_concern. */
   ASSERT (mongoc_write_concern_get_wmajority (cloned->write_concern));
   ASSERT (mongoc_write_concern_get_wtimeout_int64 (cloned->write_concern) ==
           1000);
   ASSERT (mongoc_write_concern_get_w (cloned->write_concern) ==
           MONGOC_WRITE_CONCERN_W_MAJORITY);
   /* check generated bson in cloned cursor. */
   ASSERT_MATCH (_mongoc_read_concern_get_bson (cloned->read_concern),
                 "{'level': 'local'}");
   bson = _mongoc_write_concern_get_bson (cloned->write_concern);
   ASSERT (bson);
   ASSERT (bson_iter_init_find (&iter, bson, "fsync") &&
           BSON_ITER_HOLDS_BOOL (&iter) && bson_iter_bool (&iter));
   ASSERT (bson_iter_init_find (&iter, bson, "j") &&
           BSON_ITER_HOLDS_BOOL (&iter) && bson_iter_bool (&iter));
   ASSERT (bson_iter_init_find (&iter, bson, "w") &&
           BSON_ITER_HOLDS_UTF8 (&iter));
   ASSERT_CMPSTR (bson_iter_utf8 (&iter, NULL), "majority");
   mongoc_cursor_destroy (cursor);
   mongoc_cursor_destroy (cloned);
   CURSOR_COMMON_TEARDOWN;
}


/* test calling mongoc_cursor_next again after it returns false. */
static void
_test_common_advancing_past_end (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;
   const bson_t *doc;
   const bson_t *current;
   const bson_t *err_doc;
   bson_error_t err;

   CURSOR_COMMON_SETUP;
   cursor = ctor (coll);
   while (mongoc_cursor_next (cursor, &doc)) {
      current = mongoc_cursor_current (cursor);
      /* should be same address. */
      BSON_ASSERT (doc == current);
      ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &err), err);
      /* cursor will report more until certain there are no documents left. */
      ASSERT (mongoc_cursor_more (cursor));
   }
   /* advance one past the end. */
   BSON_ASSERT (!mongoc_cursor_next (cursor, &doc));
   BSON_ASSERT (mongoc_cursor_error (cursor, &err));
   BSON_ASSERT (mongoc_cursor_error_document (cursor, &err, &err_doc));
   ASSERT_ERROR_CONTAINS (err,
                          MONGOC_ERROR_CURSOR,
                          MONGOC_ERROR_CURSOR_INVALID_CURSOR,
                          "Cannot advance a completed or failed cursor.");
   /* this is not a server error, the error document should be NULL. */
   BSON_ASSERT (bson_empty (err_doc));
   mongoc_cursor_destroy (cursor);
   CURSOR_COMMON_TEARDOWN;
}


typedef struct {
   char *expected_host_and_port;
   bool called;
} test_common_server_hint_ctx_t;


static void
_test_common_server_hint_command_started (
   const mongoc_apm_command_started_t *event)
{
   const mongoc_host_list_t *host = mongoc_apm_command_started_get_host (event);
   const char *cmd = mongoc_apm_command_started_get_command_name (event);
   test_common_server_hint_ctx_t *ctx;
   /* only check command associated with cursor priming. */
   if (strcmp (cmd, "find") == 0 ||
       strcasecmp (cmd, HANDSHAKE_CMD_LEGACY_HELLO) == 0 ||
       strcmp (cmd, "hello") == 0 || strcmp (cmd, "listDatabases") == 0) {
      ctx = (test_common_server_hint_ctx_t *)
         mongoc_apm_command_started_get_context (event);
      ASSERT_CMPSTR (host->host_and_port, ctx->expected_host_and_port);
      BSON_ASSERT (!ctx->called);
      ctx->called = true;
   }
}


/* test setting the server id (hint) on cursors that support it. */
static void
_test_common_server_hint (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;
   const bson_t *doc;
   bson_error_t err;
   mongoc_server_description_t *sd;
   mongoc_read_prefs_t *read_prefs;
   test_common_server_hint_ctx_t test_ctx = {0};
   mongoc_apm_callbacks_t *callbacks;

   CURSOR_COMMON_SETUP;
   /* set APM callbacks, and then set server hint. Make sure we target the same
    * host that we select. */
   callbacks = mongoc_apm_callbacks_new ();
   mongoc_apm_set_command_started_cb (callbacks,
                                      _test_common_server_hint_command_started);
   cursor = ctor (coll);
   read_prefs = mongoc_read_prefs_new (MONGOC_READ_PRIMARY);
   sd = mongoc_client_select_server (client, false, read_prefs, &err);
   ASSERT_OR_PRINT (sd, err);
   test_ctx.expected_host_and_port = bson_strdup (sd->host.host_and_port);
   mongoc_read_prefs_destroy (read_prefs);
   mongoc_client_set_apm_callbacks (client, callbacks, &test_ctx);
   mongoc_apm_callbacks_destroy (callbacks);
   BSON_ASSERT (mongoc_cursor_set_hint (cursor, sd->id));
   ASSERT_CMPUINT32 (mongoc_cursor_get_hint (cursor), ==, sd->id);
   mongoc_server_description_destroy (sd);

   BSON_ASSERT (mongoc_cursor_next (cursor, &doc));
   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &err), err);
   mongoc_cursor_destroy (cursor);
   bson_free (test_ctx.expected_host_and_port);
   BSON_ASSERT (test_ctx.called);
   CURSOR_COMMON_TEARDOWN;
}


/* test setting options on unprimed, non-aggregate cursors. */
static void
_test_common_opts (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;
   const bson_t *doc;
   mongoc_server_description_t *sd;
   bson_error_t err;

   CURSOR_COMMON_SETUP;
   sd = mongoc_client_select_server (client, true, NULL, &err);
   ASSERT_OR_PRINT (sd, err);
   cursor = ctor (coll);
   /* check that we get what we set. */
   BSON_ASSERT (mongoc_cursor_set_hint (cursor, sd->id));
   ASSERT_CMPINT (mongoc_cursor_get_hint (cursor), ==, sd->id);

   /* listDatabases and isMaster prohibits limit and batchSize */
   if ((make_cursor_fn) ctx != _make_array_cursor &&
       (make_cursor_fn) ctx != _make_cmd_deprecated_cursor) {
      mongoc_cursor_set_batch_size (cursor, 1);
      ASSERT_CMPINT (mongoc_cursor_get_batch_size (cursor), ==, 1);
      BSON_ASSERT (mongoc_cursor_set_limit (cursor, 2));
      ASSERT_CMPINT ((int) mongoc_cursor_get_limit (cursor), ==, 2);
   }

   mongoc_cursor_set_max_await_time_ms (cursor, 3);
   ASSERT_CMPINT (mongoc_cursor_get_max_await_time_ms (cursor), ==, 3);
   /* prime the cursor. */
   ASSERT_OR_PRINT (mongoc_cursor_next (cursor, &doc), cursor->error);
   /* options should be unchanged. */
   ASSERT_CMPINT (mongoc_cursor_get_hint (cursor), ==, sd->id);
   if ((make_cursor_fn) ctx != _make_array_cursor &&
       (make_cursor_fn) ctx != _make_cmd_deprecated_cursor) {
      ASSERT_CMPINT (mongoc_cursor_get_batch_size (cursor), ==, 1);
      ASSERT_CMPINT ((int) mongoc_cursor_get_limit (cursor), ==, 2);
      /* limit cannot be set again. */
      BSON_ASSERT (!mongoc_cursor_set_limit (cursor, 5));
      ASSERT_CMPINT ((int) mongoc_cursor_get_limit (cursor), ==, 2);
   }

   ASSERT_CMPINT (mongoc_cursor_get_max_await_time_ms (cursor), ==, 3);
   /* trying to set hint again logs an error. */
   capture_logs (true);
   BSON_ASSERT (!mongoc_cursor_set_hint (cursor, 123));
   capture_logs (false);
   ASSERT_CMPINT (mongoc_cursor_get_hint (cursor), ==, sd->id);
   /* batch size can be set again without issue. */
   mongoc_cursor_set_batch_size (cursor, 4);
   ASSERT_CMPINT (mongoc_cursor_get_batch_size (cursor), ==, 4);
   /* max await time ms cannot be set (but fails quietly). */
   mongoc_cursor_set_max_await_time_ms (cursor, 6);
   ASSERT_CMPINT (mongoc_cursor_get_max_await_time_ms (cursor), ==, 3);
   mongoc_cursor_destroy (cursor);
   mongoc_server_description_destroy (sd);
   CURSOR_COMMON_TEARDOWN;
}


/* test setting options on cursors that are primed on construction. */
static void
_test_common_opts_after_prime (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;

   CURSOR_COMMON_SETUP;
   cursor = ctor (coll);
   /* trying to set hint logs an error. */
   capture_logs (true);
   BSON_ASSERT (!mongoc_cursor_set_hint (cursor, 123));
   capture_logs (false);
   ASSERT_CMPINT (mongoc_cursor_get_hint (cursor), !=, 0);
   /* batch size can be set again without issue. */
   mongoc_cursor_set_batch_size (cursor, 4);
   ASSERT_CMPINT (mongoc_cursor_get_batch_size (cursor), ==, 4);
   /* limit cannot be set. */
   BSON_ASSERT (!mongoc_cursor_set_limit (cursor, 5));
   ASSERT_CMPINT ((int) mongoc_cursor_get_limit (cursor), ==, 0);
   /* max await time ms cannot be set (but fails quietly). */
   mongoc_cursor_set_max_await_time_ms (cursor, 6);
   ASSERT_CMPINT (mongoc_cursor_get_max_await_time_ms (cursor), ==, 0);
   mongoc_cursor_destroy (cursor);
   CURSOR_COMMON_TEARDOWN;
}


/* test setting options on a cursor constructed from an aggregation. */
static void
_test_common_opts_agg (void *ctx)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   make_cursor_fn ctor;
   mongoc_cursor_t *cursor;

   CURSOR_COMMON_SETUP;
   cursor = ctor (coll);
   /* trying to set hint logs an error. */
   capture_logs (true);
   BSON_ASSERT (!mongoc_cursor_set_hint (cursor, 123));
   capture_logs (false);
   ASSERT_CMPINT (mongoc_cursor_get_hint (cursor), !=, 0);
   /* batch size can be set again without issue. */
   mongoc_cursor_set_batch_size (cursor, 4);
   ASSERT_CMPINT (mongoc_cursor_get_batch_size (cursor), ==, 4);
   /* limit can be set. */
   BSON_ASSERT (mongoc_cursor_set_limit (cursor, 5));
   ASSERT_CMPINT ((int) mongoc_cursor_get_limit (cursor), ==, 5);
   /* max await time ms can be set. */
   mongoc_cursor_set_max_await_time_ms (cursor, 6);
   ASSERT_CMPINT (mongoc_cursor_get_max_await_time_ms (cursor), ==, 6);
   mongoc_cursor_destroy (cursor);
   CURSOR_COMMON_TEARDOWN;
}


static mongoc_cursor_t *
_make_find_cursor (mongoc_collection_t *coll)
{
   return mongoc_collection_find_with_opts (coll, tmp_bson ("{}"), NULL, NULL);
}


static mongoc_cursor_t *
_make_cmd_cursor (mongoc_collection_t *coll)
{
   return mongoc_collection_find_indexes_with_opts (coll, NULL);
}


static mongoc_cursor_t *
_make_cmd_cursor_from_agg (mongoc_collection_t *coll)
{
   return mongoc_collection_aggregate (
      coll, MONGOC_QUERY_SLAVE_OK, tmp_bson ("{}"), NULL, NULL);
}


static mongoc_cursor_t *
_make_cmd_deprecated_cursor (mongoc_collection_t *coll)
{
   return mongoc_collection_command (coll,
                                     MONGOC_QUERY_SLAVE_OK,
                                     0,
                                     0,
                                     0,
                                     tmp_bson ("{'hello': 1}"),
                                     NULL,
                                     NULL);
}


static mongoc_cursor_t *
_make_array_cursor (mongoc_collection_t *coll)
{
   return mongoc_client_find_databases_with_opts (coll->client, NULL);
}

#define TEST_CURSOR_FIND(prefix, fn)     \
   TestSuite_AddFull (suite,             \
                      prefix "/find",    \
                      fn,                \
                      NULL,              \
                      _make_find_cursor, \
                      TestSuite_CheckLive);

#define TEST_CURSOR_CMD(prefix, fn) \
   TestSuite_AddFull (              \
      suite, prefix "/cmd", fn, NULL, _make_cmd_cursor, TestSuite_CheckLive);

#define TEST_CURSOR_CMD_DEPRECATED(prefix, fn)     \
   TestSuite_AddFull (suite,                       \
                      prefix "/cmd_deprecated",    \
                      fn,                          \
                      NULL,                        \
                      _make_cmd_deprecated_cursor, \
                      TestSuite_CheckLive);

#define TEST_CURSOR_ARRAY(prefix, fn)     \
   TestSuite_AddFull (suite,              \
                      prefix "/array",    \
                      fn,                 \
                      NULL,               \
                      _make_array_cursor, \
                      TestSuite_CheckLive);

#define TEST_CURSOR_AGG(prefix, fn)              \
   TestSuite_AddFull (suite,                     \
                      prefix "/agg",             \
                      fn,                        \
                      NULL,                      \
                      _make_cmd_cursor_from_agg, \
                      TestSuite_CheckLive);


#define TEST_FOREACH_CURSOR(prefix, fn)     \
   TEST_CURSOR_FIND (prefix, fn);           \
   TEST_CURSOR_CMD (prefix, fn);            \
   TEST_CURSOR_CMD_DEPRECATED (prefix, fn); \
   TEST_CURSOR_ARRAY (prefix, fn);          \
   TEST_CURSOR_AGG (prefix, fn);


static void
test_common_cursor_functions_install (TestSuite *suite)
{
   /* test functionality common to all cursor implementations. */
   TEST_FOREACH_CURSOR ("/Cursor/common/get_host", _test_common_get_host);
   TEST_FOREACH_CURSOR ("/Cursor/common/clone", _test_common_clone);
   TEST_FOREACH_CURSOR ("/Cursor/common/clone_w_concerns",
                        _test_common_clone_w_concerns);
   TEST_FOREACH_CURSOR ("/Cursor/common/advancing_past_end",
                        _test_common_advancing_past_end);
   /* an agg/cmd cursors do not support setting server id. test others. */
   TEST_CURSOR_FIND ("/Cursor/common/hint", _test_common_server_hint);
   TEST_CURSOR_CMD_DEPRECATED ("/Cursor/common/hint", _test_common_server_hint);
   TEST_CURSOR_ARRAY ("/Cursor/common/hint", _test_common_server_hint);
   /* find, cmd_depr, and array cursors can have all options set. */
   TEST_CURSOR_FIND ("/Cursor/common/opts", _test_common_opts);
   TEST_CURSOR_CMD_DEPRECATED ("/Cursor/common/opts", _test_common_opts);
   TEST_CURSOR_ARRAY ("/Cursor/common/opts", _test_common_opts);
   /* a command cursor created from find_indexes_with_opts is already primed. */
   TEST_CURSOR_CMD ("/Cursor/common/opts", _test_common_opts_after_prime);
   /* a command cursor created from an agg has the server id set, but is not
    * primed. */
   TEST_CURSOR_AGG ("/Cursor/common/opts", _test_common_opts_agg);
}


static void
test_limit (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_bulk_operation_t *bulk;
   bson_t *b;
   bson_t *opts;
   int i, n_docs;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   int64_t limits[] = {5, -5};
   const bson_t *doc = NULL;
   bool r;

   client = test_framework_new_default_client ();
   collection = get_test_collection (client, "test_limit");
   bulk = mongoc_collection_create_bulk_operation_with_opts (collection, NULL);
   b = tmp_bson ("{}");
   for (i = 0; i < 10; ++i) {
      mongoc_bulk_operation_insert (bulk, b);
   }

   r = (0 != mongoc_bulk_operation_execute (bulk, NULL, &error));
   ASSERT_OR_PRINT (r, error);

   /* test positive and negative limit */
   for (i = 0; i < 2; i++) {
      cursor = mongoc_collection_find (
         collection, MONGOC_QUERY_NONE, 0, 0, 0, tmp_bson ("{}"), NULL, NULL);
      ASSERT_CMPINT64 ((int64_t) 0, ==, mongoc_cursor_get_limit (cursor));
      ASSERT (mongoc_cursor_set_limit (cursor, limits[i]));
      ASSERT_CMPINT64 (limits[i], ==, mongoc_cursor_get_limit (cursor));
      n_docs = 0;
      while (mongoc_cursor_next (cursor, &doc)) {
         ++n_docs;
      }

      ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);
      ASSERT (!mongoc_cursor_more (cursor));
      ASSERT_CMPINT (n_docs, ==, 5);
      ASSERT (!mongoc_cursor_set_limit (cursor, 123)); /* no effect */
      ASSERT_CMPINT64 (limits[i], ==, mongoc_cursor_get_limit (cursor));

      mongoc_cursor_destroy (cursor);

      if (limits[i] > 0) {
         opts =
            tmp_bson ("{'limit': {'$numberLong': '%" PRId64 "'}}", limits[i]);
      } else {
         opts = tmp_bson (
            "{'singleBatch': true, 'limit': {'$numberLong': '%" PRId64 "'}}",
            -limits[i]);
      }

      cursor = mongoc_collection_find_with_opts (
         collection, tmp_bson (NULL), opts, NULL);

      ASSERT_CMPINT64 (limits[i], ==, mongoc_cursor_get_limit (cursor));
      n_docs = 0;
      while (mongoc_cursor_next (cursor, &doc)) {
         ++n_docs;
      }

      ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);
      ASSERT_CMPINT (n_docs, ==, 5);

      mongoc_cursor_destroy (cursor);
   }

   mongoc_bulk_operation_destroy (bulk);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}


typedef struct {
   int succeeded_count;
   int64_t cursor_id;
} killcursors_test_t;


static void
killcursors_succeeded (const mongoc_apm_command_succeeded_t *event)
{
   killcursors_test_t *ctx;
   const bson_t *reply;
   bson_iter_t iter;
   bson_iter_t array;

   if (bson_strcasecmp (mongoc_apm_command_succeeded_get_command_name (event),
                        "killcursors") != 0) {
      return;
   }

   ctx =
      (killcursors_test_t *) mongoc_apm_command_succeeded_get_context (event);
   ctx->succeeded_count++;

   if (!test_framework_max_wire_version_at_least (
          WIRE_VERSION_KILLCURSORS_CMD)) {
      return;
   }

   reply = mongoc_apm_command_succeeded_get_reply (event);

#define ASSERT_EMPTY(_fieldname)                                   \
   BSON_ASSERT (bson_iter_init_find (&iter, reply, (_fieldname))); \
   BSON_ASSERT (bson_iter_recurse (&iter, &array));                \
   BSON_ASSERT (!bson_iter_next (&array));

   ASSERT_EMPTY ("cursorsNotFound");
   ASSERT_EMPTY ("cursorsAlive");
   ASSERT_EMPTY ("cursorsUnknown");

   BSON_ASSERT (bson_iter_init_find (&iter, reply, "cursorsKilled"));
   BSON_ASSERT (bson_iter_recurse (&iter, &array));
   BSON_ASSERT (bson_iter_next (&array));
   ASSERT_CMPINT64 (ctx->cursor_id, ==, bson_iter_int64 (&array));
}

extern void
_mongoc_cursor_impl_find_opquery_init (mongoc_cursor_t *cursor, bson_t *filter);

/* test killing a cursor with mongo_cursor_destroy and a real server */
static void
test_kill_cursor_live (void)
{
   mongoc_apm_callbacks_t *callbacks;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   bson_t *b;
   mongoc_bulk_operation_t *bulk;
   int i;
   bson_error_t error;
   uint32_t server_id;
   bool r;
   mongoc_cursor_t *cursor;
   const bson_t *doc;
   killcursors_test_t ctx;

   ctx.succeeded_count = 0;

   callbacks = mongoc_apm_callbacks_new ();
   mongoc_apm_set_command_succeeded_cb (callbacks, killcursors_succeeded);
   client = test_framework_new_default_client ();
   mongoc_client_set_apm_callbacks (client, callbacks, &ctx);
   collection = get_test_collection (client, "test");
   b = tmp_bson ("{}");
   bulk = mongoc_collection_create_bulk_operation_with_opts (collection, NULL);
   for (i = 0; i < 200; i++) {
      mongoc_bulk_operation_insert (bulk, b);
   }

   server_id = mongoc_bulk_operation_execute (bulk, NULL, &error);
   ASSERT_OR_PRINT (server_id > 0, error);

   cursor = mongoc_collection_find (collection,
                                    MONGOC_QUERY_NONE,
                                    0,
                                    0,
                                    2, /* batch size 2 */
                                    b,
                                    NULL,
                                    NULL);

   r = mongoc_cursor_next (cursor, &doc);
   ASSERT (r);
   ctx.cursor_id = mongoc_cursor_get_id (cursor);
   ASSERT (ctx.cursor_id);

   /* sends OP_KILLCURSORS or killCursors command to server */
   mongoc_cursor_destroy (cursor);

   ASSERT_CMPINT (ctx.succeeded_count, ==, 1);

   b = bson_new ();
   cursor = _mongoc_cursor_find_new (
      client, collection->ns, b, NULL, NULL, NULL, NULL);
   /* override the typical priming, and immediately transition to an OPQUERY
    * find cursor. */
   cursor->impl.destroy (&cursor->impl);
   _mongoc_cursor_impl_find_opquery_init (cursor, b);

   cursor->cursor_id = ctx.cursor_id;
   cursor->state = END_OF_BATCH; /* meaning, "finished reading first batch" */
   r = mongoc_cursor_next (cursor, &doc);
   ASSERT (!r);
   ASSERT (mongoc_cursor_error (cursor, &error));
   ASSERT_ERROR_CONTAINS (error, MONGOC_ERROR_CURSOR, 16, "cursor is invalid");

   mongoc_cursor_destroy (cursor);
   mongoc_bulk_operation_destroy (bulk);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   mongoc_apm_callbacks_destroy (callbacks);
}


/* test OP_KILLCURSORS or the killCursors command with mock servers */
static void
_test_kill_cursors (bool pooled, bool use_killcursors_cmd)
{
   mock_rs_t *rs;
   mongoc_client_pool_t *pool = NULL;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   bson_t *q = BCON_NEW ("a", BCON_INT32 (1));
   mongoc_read_prefs_t *prefs;
   mongoc_cursor_t *cursor;
   const bson_t *doc = NULL;
   future_t *future;
   request_t *request;
   bson_error_t error;
   request_t *kill_cursors;
   const char *ns_out;
   int64_t cursor_id_out;

   rs =
      mock_rs_with_autoismaster (use_killcursors_cmd ? 4 : 3, /* wire version */
                                 true,                        /* has primary */
                                 5,  /* number of secondaries */
                                 0); /* number of arbiters */

   mock_rs_run (rs);

   if (pooled) {
      pool =
         test_framework_client_pool_new_from_uri (mock_rs_get_uri (rs), NULL);
      client = mongoc_client_pool_pop (pool);
   } else {
      client = test_framework_client_new_from_uri (mock_rs_get_uri (rs), NULL);
   }

   collection = mongoc_client_get_collection (client, "db", "collection");

   prefs = mongoc_read_prefs_new (MONGOC_READ_SECONDARY);
   cursor = mongoc_collection_find (
      collection, MONGOC_QUERY_NONE, 0, 0, 0, q, NULL, prefs);

   future = future_cursor_next (cursor, &doc);
   request = mock_rs_receives_request (rs);

   /* reply as appropriate to OP_QUERY or find command */
   mock_rs_replies_to_find (request,
                            MONGOC_QUERY_SLAVE_OK,
                            123,
                            1,
                            "db.collection",
                            "{'b': 1}",
                            use_killcursors_cmd);

   if (!future_get_bool (future)) {
      mongoc_cursor_error (cursor, &error);
      fprintf (stderr, "%s\n", error.message);
      abort ();
   };

   ASSERT_MATCH (doc, "{'b': 1}");
   ASSERT_CMPINT (123, ==, (int) mongoc_cursor_get_id (cursor));

   future_destroy (future);
   future = future_cursor_destroy (cursor);

   if (use_killcursors_cmd) {
      kill_cursors =
         mock_rs_receives_command (rs, "db", MONGOC_QUERY_SLAVE_OK, NULL);

      /* mock server framework can't test "cursors" array, CDRIVER-994 */
      ASSERT (BCON_EXTRACT ((bson_t *) request_get_doc (kill_cursors, 0),
                            "killCursors",
                            BCONE_UTF8 (ns_out),
                            "cursors",
                            "[",
                            BCONE_INT64 (cursor_id_out),
                            "]"));

      ASSERT_CMPSTR ("collection", ns_out);
      ASSERT_CMPINT64 ((int64_t) 123, ==, cursor_id_out);

      mock_rs_replies_simple (request, "{'ok': 1}");
   } else {
      kill_cursors = mock_rs_receives_kill_cursors (rs, 123);
   }

   /* OP_KILLCURSORS was sent to the right secondary */
   ASSERT_CMPINT (request_get_server_port (kill_cursors),
                  ==,
                  request_get_server_port (request));

   BSON_ASSERT (future_wait (future));

   request_destroy (kill_cursors);
   request_destroy (request);
   future_destroy (future);
   mongoc_read_prefs_destroy (prefs);
   mongoc_collection_destroy (collection);
   bson_destroy (q);

   if (pooled) {
      mongoc_client_pool_push (pool, client);
      mongoc_client_pool_destroy (pool);
   } else {
      mongoc_client_destroy (client);
   }

   mock_rs_destroy (rs);
}


static void
test_kill_cursors_single (void)
{
   _test_kill_cursors (false, false);
}


static void
test_kill_cursors_pooled (void)
{
   _test_kill_cursors (true, false);
}


static void
test_kill_cursors_single_cmd (void)
{
   _test_kill_cursors (false, true);
}


static void
test_kill_cursors_pooled_cmd (void)
{
   _test_kill_cursors (true, true);
}


/* We already test that mongoc_cursor_destroy sends OP_KILLCURSORS in
 * test_kill_cursors_single / pooled. Here, test explicit
 * mongoc_client_kill_cursor. */
static void
_test_client_kill_cursor (bool has_primary, bool wire_version_4)
{
   mock_rs_t *rs;
   mongoc_client_t *client;
   mongoc_read_prefs_t *read_prefs;
   bson_error_t error;
   future_t *future;
   request_t *request;

   rs = mock_rs_with_autoismaster (wire_version_4 ? 4 : 3,
                                   has_primary, /* maybe a primary*/
                                   1,           /* definitely a secondary */
                                   0);          /* no arbiter */
   mock_rs_run (rs);
   client = test_framework_client_new_from_uri (mock_rs_get_uri (rs), NULL);
   read_prefs = mongoc_read_prefs_new (MONGOC_READ_SECONDARY);

   /* make client open a connection - it won't open one to kill a cursor */
   future = future_client_command_simple (
      client, "admin", tmp_bson ("{'foo': 1}"), read_prefs, NULL, &error);

   request =
      mock_rs_receives_command (rs, "admin", MONGOC_QUERY_SLAVE_OK, NULL);

   mock_rs_replies_simple (request, "{'ok': 1}");
   ASSERT_OR_PRINT (future_get_bool (future), error);
   request_destroy (request);
   future_destroy (future);

   future = future_client_kill_cursor (client, 123);
   mock_rs_set_request_timeout_msec (rs, 100);

   /* we don't pass namespace so client always sends legacy OP_KILLCURSORS */
   request = mock_rs_receives_kill_cursors (rs, 123);

   if (has_primary) {
      BSON_ASSERT (request);

      /* weird but true. see mongoc_client_kill_cursor's documentation */
      BSON_ASSERT (mock_rs_request_is_to_primary (rs, request));

      request_destroy (request); /* server has no reply to OP_KILLCURSORS */
   } else {
      /* TODO: catch and check warning */
      BSON_ASSERT (!request);
   }

   future_wait (future); /* no return value */
   future_destroy (future);
   mongoc_read_prefs_destroy (read_prefs);
   mongoc_client_destroy (client);
   mock_rs_destroy (rs);
}

static void
test_client_kill_cursor_with_primary (void)
{
   _test_client_kill_cursor (true, false);
}


static void
test_client_kill_cursor_without_primary (void)
{
   _test_client_kill_cursor (false, false);
}


static void
test_client_kill_cursor_with_primary_wire_version_4 (void)
{
   _test_client_kill_cursor (true, true);
}


static void
test_client_kill_cursor_without_primary_wire_version_4 (void)
{
   _test_client_kill_cursor (false, true);
}


static int
count_docs (mongoc_cursor_t *cursor)
{
   int n = 0;
   const bson_t *doc;
   bson_error_t error;

   while (mongoc_cursor_next (cursor, &doc)) {
      ++n;
   }

   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);

   return n;
}


static void
_test_cursor_new_from_command (const char *cmd_json,
                               const char *collection_name)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_bulk_operation_t *bulk;
   bool r;
   bson_error_t error;
   mongoc_server_description_t *sd;
   uint32_t server_id;
   bson_t reply;
   mongoc_cursor_t *cmd_cursor;

   client = test_framework_new_default_client ();
   collection = mongoc_client_get_collection (client, "test", collection_name);
   mongoc_collection_delete_many (
      collection, tmp_bson ("{}"), NULL, NULL, NULL);

   bulk = mongoc_collection_create_bulk_operation_with_opts (collection, NULL);
   mongoc_bulk_operation_insert (bulk, tmp_bson ("{'_id': 'a'}"));
   mongoc_bulk_operation_insert (bulk, tmp_bson ("{'_id': 'b'}"));
   r = (0 != mongoc_bulk_operation_execute (bulk, NULL, &error));
   ASSERT_OR_PRINT (r, error);

   sd = mongoc_topology_select (client->topology, MONGOC_SS_READ, NULL, &error);

   ASSERT_OR_PRINT (sd, error);
   server_id = sd->id;
   mongoc_client_command_simple_with_server_id (
      client, "test", tmp_bson (cmd_json), NULL, server_id, &reply, &error);
   cmd_cursor = mongoc_cursor_new_from_command_reply_with_opts (
      client, &reply, tmp_bson ("{'serverId': %d}", server_id));
   ASSERT_OR_PRINT (!mongoc_cursor_error (cmd_cursor, &error), error);
   ASSERT_CMPUINT32 (server_id, ==, mongoc_cursor_get_hint (cmd_cursor));
   ASSERT_CMPINT (count_docs (cmd_cursor), ==, 2);

   mongoc_cursor_destroy (cmd_cursor);
   mongoc_server_description_destroy (sd);
   mongoc_bulk_operation_destroy (bulk);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}

static void
test_cursor_empty_collection (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   bson_error_t error;
   const bson_t *doc;
   mongoc_cursor_t *cursor;

   client = test_framework_new_default_client ();
   collection = mongoc_client_get_collection (
      client, "test", "test_cursor_empty_collection");
   mongoc_collection_delete_many (
      collection, tmp_bson ("{}"), NULL, NULL, NULL);

   cursor = mongoc_collection_find_with_opts (
      collection, tmp_bson ("{}"), NULL, NULL);

   ASSERT (cursor);
   ASSERT (!mongoc_cursor_error (cursor, &error));
   ASSERT (mongoc_cursor_more (cursor));

   mongoc_cursor_next (cursor, &doc);

   ASSERT (!mongoc_cursor_error (cursor, &error));
   ASSERT (!mongoc_cursor_more (cursor));

   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}


static void
test_cursor_new_from_aggregate (void)
{
   _test_cursor_new_from_command (
      "{'aggregate': 'test_cursor_new_from_aggregate',"
      " 'pipeline': [], 'cursor': {}}",
      "test_cursor_new_from_aggregate");
}


static void
test_cursor_new_from_aggregate_no_initial (void)
{
   _test_cursor_new_from_command (
      "{'aggregate': 'test_cursor_new_from_aggregate_no_initial',"
      " 'pipeline': [], 'cursor': {'batchSize': 0}}",
      "test_cursor_new_from_aggregate_no_initial");
}


static void
test_cursor_new_from_find (void *ctx)
{
   _test_cursor_new_from_command ("{'find': 'test_cursor_new_from_find'}",
                                  "test_cursor_new_from_find");
}


static void
test_cursor_new_from_find_batches (void *ctx)
{
   _test_cursor_new_from_command (
      "{'find': 'test_cursor_new_from_find_batches', 'batchSize': 1}",
      "test_cursor_new_from_find_batches");
}


static void
test_cursor_new_invalid (void)
{
   mongoc_client_t *client;
   bson_error_t error;
   mongoc_cursor_t *cursor;
   bson_t b = BSON_INITIALIZER;
   const bson_t *error_doc;

   client = test_framework_new_default_client ();
   cursor = mongoc_cursor_new_from_command_reply_with_opts (client, &b, NULL);
   ASSERT (cursor);
   ASSERT (mongoc_cursor_error (cursor, &error));
   ASSERT_ERROR_CONTAINS (error,
                          MONGOC_ERROR_CURSOR,
                          MONGOC_ERROR_CURSOR_INVALID_CURSOR,
                          "Couldn't parse cursor document");

   ASSERT (mongoc_cursor_error_document (cursor, &error, &error_doc));
   ASSERT (bson_empty (error_doc));

   mongoc_cursor_destroy (cursor);
   mongoc_client_destroy (client);
}


static void
test_cursor_new_tailable_await (void)
{
   mock_server_t *server;
   mongoc_client_t *client;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   const bson_t *doc;
   future_t *future;
   request_t *request;

   server = mock_server_with_autoismaster (WIRE_VERSION_FIND_CMD);
   mock_server_run (server);

   client =
      test_framework_client_new_from_uri (mock_server_get_uri (server), NULL);
   cursor = mongoc_cursor_new_from_command_reply_with_opts (
      client,
      bson_copy (tmp_bson ("{'ok': 1,"
                           " 'cursor': {"
                           "    'id': {'$numberLong': '123'},"
                           "    'ns': 'db.collection',"
                           "    'firstBatch': []"
                           " },"
                           " 'tailable': true,"
                           " 'awaitData': true,"
                           " 'maxAwaitTimeMS': 100"
                           "}")),
      tmp_bson ("{'tailable': true,"
                " 'awaitData': true,"
                " 'maxAwaitTimeMS': 100"
                "}"));

   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);

   future = future_cursor_next (cursor, &doc);
   request = mock_server_receives_command (server,
                                           "db",
                                           MONGOC_QUERY_SLAVE_OK,
                                           "{'getMore': {'$numberLong': '123'},"
                                           " 'collection': 'collection',"
                                           " 'maxTimeMS': 100"
                                           "}");
   mock_server_replies_to_find (request,
                                MONGOC_QUERY_SLAVE_OK,
                                0 /* cursor id */,
                                1 /* number returned */,
                                "db.collection",
                                "{'_id': 1}",
                                true);

   BSON_ASSERT (future_get_bool (future));
   ASSERT_MATCH (doc, "{'_id': 1}");

   future_destroy (future);
   request_destroy (request);
   mongoc_cursor_destroy (cursor);
   mongoc_client_destroy (client);
   mock_server_destroy (server);
}


static void
test_cursor_int64_t_maxtimems (void)
{
   mock_server_t *server;
   mongoc_client_t *client;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   const bson_t *doc;
   future_t *future;
   request_t *request;
   bson_t *max_await_time_ms;
   uint64_t ms_int64 = UINT32_MAX + (uint64_t) 1;

   server = mock_server_with_autoismaster (WIRE_VERSION_FIND_CMD);
   mock_server_run (server);

   client =
      test_framework_client_new_from_uri (mock_server_get_uri (server), NULL);

   max_await_time_ms = tmp_bson (NULL);
   bson_append_bool (max_await_time_ms, "tailable", 8, true);
   bson_append_bool (max_await_time_ms, "awaitData", 9, true);
   bson_append_int64 (max_await_time_ms,
                      MONGOC_CURSOR_MAX_AWAIT_TIME_MS,
                      MONGOC_CURSOR_MAX_AWAIT_TIME_MS_LEN,
                      ms_int64);

   cursor = mongoc_cursor_new_from_command_reply_with_opts (
      client,
      bson_copy (tmp_bson ("{'ok': 1,"
                           " 'cursor': {"
                           "    'id': {'$numberLong': '123'},"
                           "    'ns': 'db.collection',"
                           "    'firstBatch': []"
                           " }"
                           "}")),
      max_await_time_ms);

   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);

   future = future_cursor_next (cursor, &doc);
   request = mock_server_receives_command (
      server,
      "db",
      MONGOC_QUERY_SLAVE_OK,
      "{'getMore': {'$numberLong': '123'},"
      " 'collection': 'collection',"
      " 'maxTimeMS': {'$numberLong': '%" PRIu64 "'}"
      "}",
      ms_int64);
   mock_server_replies_to_find (request,
                                MONGOC_QUERY_SLAVE_OK,
                                0 /* cursor id */,
                                1 /* number returned */,
                                "db.collection",
                                "{'_id': 1}",
                                true);

   BSON_ASSERT (future_get_bool (future));
   ASSERT_MATCH (doc, "{'_id': 1}");

   future_destroy (future);
   request_destroy (request);
   mongoc_cursor_destroy (cursor);
   mongoc_client_destroy (client);
   mock_server_destroy (server);
}


static void
test_cursor_new_ignores_fields (void)
{
   mock_server_t *server;
   mongoc_client_t *client;
   mongoc_cursor_t *cursor;
   const bson_t *doc;
   bson_error_t error;

   server = mock_server_with_autoismaster (WIRE_VERSION_FIND_CMD);
   mock_server_run (server);

   client =
      test_framework_client_new_from_uri (mock_server_get_uri (server), NULL);
   cursor = mongoc_cursor_new_from_command_reply_with_opts (
      client,
      bson_copy (tmp_bson ("{'ok': 1,"
                           " 'cursor': {"
                           "     'id': 0,"
                           "     'ns': 'test.foo',"
                           "     'firstBatch': []"
                           " },"
                           " 'operationTime' : {},"
                           " '$clusterTime': {},"
                           " '$gleStats': {}"
                           "}")),
      tmp_bson ("{'batchSize': 10}"));

   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);
   ASSERT_MATCH (&cursor->opts, "{'batchSize': 10}");
   ASSERT (!mongoc_cursor_next (cursor, &doc));
   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);

   mongoc_cursor_destroy (cursor);
   mongoc_client_destroy (client);
   mock_server_destroy (server);
}


static void
test_cursor_new_invalid_filter (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   const bson_t *error_doc;

   client = test_framework_new_default_client ();
   collection = mongoc_client_get_collection (client, "test", "test");

   cursor = mongoc_collection_find_with_opts (
      collection, tmp_bson ("{'': 1}"), NULL, NULL);

   ASSERT (cursor);
   ASSERT (mongoc_cursor_error (cursor, &error));
   ASSERT_ERROR_CONTAINS (error,
                          MONGOC_ERROR_CURSOR,
                          MONGOC_ERROR_CURSOR_INVALID_CURSOR,
                          "Invalid filter: empty key");

   ASSERT (mongoc_cursor_error_document (cursor, &error, &error_doc));
   ASSERT (bson_empty (error_doc));

   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}


static void
test_cursor_new_invalid_opts (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   const bson_t *error_doc;

   client = test_framework_new_default_client ();
   collection = mongoc_client_get_collection (client, "test", "test");

   cursor = mongoc_collection_find_with_opts (
      collection, tmp_bson (NULL), tmp_bson ("{'projection': {'': 1}}"), NULL);

   ASSERT (cursor);
   ASSERT (mongoc_cursor_error (cursor, &error));
   ASSERT_ERROR_CONTAINS (error,
                          MONGOC_ERROR_CURSOR,
                          MONGOC_ERROR_CURSOR_INVALID_CURSOR,
                          "Invalid opts: empty key");

   ASSERT (mongoc_cursor_error_document (cursor, &error, &error_doc));
   ASSERT (bson_empty (error_doc));
   mongoc_cursor_destroy (cursor);

   cursor = mongoc_collection_find_with_opts (
      collection, tmp_bson (NULL), tmp_bson ("{'$invalid': 1}"), NULL);

   ASSERT (cursor);
   ASSERT (mongoc_cursor_error (cursor, &error));
   ASSERT_ERROR_CONTAINS (error,
                          MONGOC_ERROR_CURSOR,
                          MONGOC_ERROR_CURSOR_INVALID_CURSOR,
                          "Cannot use $-modifiers in opts: \"$invalid\"");

   ASSERT (mongoc_cursor_error_document (cursor, &error, &error_doc));
   ASSERT (bson_empty (error_doc));

   mongoc_cursor_destroy (cursor);

   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}


static void
test_cursor_new_static (void)
{
   mongoc_client_t *client;
   bson_error_t error;
   mongoc_cursor_t *cursor;
   bson_t *bson_alloced;
   bson_t bson_static;
   bson_t *bson_copied;

   bson_alloced = tmp_bson ("{ 'ok':1,"
                            "  'cursor': {"
                            "     'id': 0,"
                            "     'ns': 'test.foo',"
                            "     'firstBatch': [{'x': 1}, {'x': 2}]}}");

   ASSERT (bson_init_static (
      &bson_static, bson_get_data (bson_alloced), bson_alloced->len));

   /* test heap-allocated bson */
   client = test_framework_new_default_client ();
   bson_copied = bson_copy (bson_alloced);
   cursor = mongoc_cursor_new_from_command_reply_with_opts (
      client, bson_copied, NULL);

   ASSERT (cursor);
   ASSERT (!mongoc_cursor_error (cursor, &error));
   mongoc_cursor_destroy (cursor);

   /* test static bson */
   cursor = mongoc_cursor_new_from_command_reply_with_opts (
      client, &bson_static, NULL);
   ASSERT (cursor);
   ASSERT (!mongoc_cursor_error (cursor, &error));

   mongoc_cursor_destroy (cursor);
   mongoc_client_destroy (client);
}


static void
test_cursor_hint_errors (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_cursor_t *cursor;

   client = test_framework_new_default_client ();
   collection = mongoc_client_get_collection (client, "db", "collection");
   cursor = mongoc_collection_find (
      collection, MONGOC_QUERY_NONE, 0, 0, 0, tmp_bson ("{}"), NULL, NULL);

   capture_logs (true);
   ASSERT (!mongoc_cursor_set_hint (cursor, 0));
   ASSERT_CAPTURED_LOG ("mongoc_cursor_set_hint",
                        MONGOC_LOG_LEVEL_ERROR,
                        "cannot set server_id to 0");

   capture_logs (true); /* clear logs */
   ASSERT (mongoc_cursor_set_hint (cursor, 123));
   ASSERT_CMPUINT32 ((uint32_t) 123, ==, mongoc_cursor_get_hint (cursor));
   ASSERT_NO_CAPTURED_LOGS ("mongoc_cursor_set_hint");
   ASSERT (!mongoc_cursor_set_hint (cursor, 42));
   ASSERT_CAPTURED_LOG ("mongoc_cursor_set_hint",
                        MONGOC_LOG_LEVEL_ERROR,
                        "server_id already set");

   /* last set_hint had no effect */
   ASSERT_CMPUINT32 ((uint32_t) 123, ==, mongoc_cursor_get_hint (cursor));

   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}


static uint32_t
server_id_for_read_mode (mongoc_client_t *client, mongoc_read_mode_t read_mode)
{
   mongoc_read_prefs_t *prefs;
   mongoc_server_description_t *sd;
   bson_error_t error;
   uint32_t server_id;

   prefs = mongoc_read_prefs_new (read_mode);
   sd =
      mongoc_topology_select (client->topology, MONGOC_SS_READ, prefs, &error);

   ASSERT_OR_PRINT (sd, error);
   server_id = sd->id;

   mongoc_server_description_destroy (sd);
   mongoc_read_prefs_destroy (prefs);

   return server_id;
}


static void
_test_cursor_hint (bool pooled, bool use_primary)
{
   mock_rs_t *rs;
   mongoc_client_pool_t *pool = NULL;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   bson_t *q = BCON_NEW ("a", BCON_INT32 (1));
   mongoc_cursor_t *cursor;
   uint32_t server_id;
   mongoc_query_flags_t expected_flags;
   const bson_t *doc = NULL;
   future_t *future;
   request_t *request;

   /* wire version WIRE_VERSION_MIN, primary, two secondaries, no arbiters */
   rs = mock_rs_with_autoismaster (WIRE_VERSION_MIN, true, 2, 0);
   mock_rs_run (rs);

   if (pooled) {
      pool =
         test_framework_client_pool_new_from_uri (mock_rs_get_uri (rs), NULL);
      client = mongoc_client_pool_pop (pool);
   } else {
      client = test_framework_client_new_from_uri (mock_rs_get_uri (rs), NULL);
   }

   collection = mongoc_client_get_collection (client, "test", "test");

   cursor = mongoc_collection_find (
      collection, MONGOC_QUERY_NONE, 0, 0, 0, q, NULL, NULL);
   ASSERT_CMPUINT32 ((uint32_t) 0, ==, mongoc_cursor_get_hint (cursor));

   if (use_primary) {
      server_id = server_id_for_read_mode (client, MONGOC_READ_PRIMARY);
      expected_flags = MONGOC_QUERY_NONE;
   } else {
      server_id = server_id_for_read_mode (client, MONGOC_READ_SECONDARY);
      expected_flags = MONGOC_QUERY_SLAVE_OK;
   }

   ASSERT (mongoc_cursor_set_hint (cursor, server_id));
   ASSERT_CMPUINT32 (server_id, ==, mongoc_cursor_get_hint (cursor));

   future = future_cursor_next (cursor, &doc);
   request = mock_rs_receives_query (
      rs, "test.test", expected_flags, 0, 0, "{'a': 1}", NULL);

   if (use_primary) {
      BSON_ASSERT (mock_rs_request_is_to_primary (rs, request));
   } else {
      BSON_ASSERT (mock_rs_request_is_to_secondary (rs, request));
   }

   mock_rs_replies (request, 0, 0, 0, 1, "{'b': 1}");
   BSON_ASSERT (future_get_bool (future));
   ASSERT_MATCH (doc, "{'b': 1}");

   request_destroy (request);
   future_destroy (future);
   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);

   if (pooled) {
      mongoc_client_pool_push (pool, client);
      mongoc_client_pool_destroy (pool);
   } else {
      mongoc_client_destroy (client);
   }

   mock_rs_destroy (rs);
   bson_destroy (q);
}

static void
test_hint_single_secondary (void)
{
   _test_cursor_hint (false, false);
}

static void
test_hint_single_primary (void)
{
   _test_cursor_hint (false, true);
}

static void
test_hint_pooled_secondary (void)
{
   _test_cursor_hint (true, false);
}

static void
test_hint_pooled_primary (void)
{
   _test_cursor_hint (true, true);
}

mongoc_read_mode_t modes[] = {
   MONGOC_READ_PRIMARY,
   MONGOC_READ_PRIMARY_PREFERRED,
   MONGOC_READ_SECONDARY,
   MONGOC_READ_SECONDARY_PREFERRED,
   MONGOC_READ_NEAREST,
};

mongoc_query_flags_t expected_flag[] = {
   MONGOC_QUERY_NONE,
   MONGOC_QUERY_SLAVE_OK,
   MONGOC_QUERY_SLAVE_OK,
   MONGOC_QUERY_SLAVE_OK,
   MONGOC_QUERY_SLAVE_OK,
};

/* test that mongoc_cursor_set_hint sets slaveok for mongos only if read pref
 * is secondaryPreferred. */
static void
test_cursor_hint_mongos (void)
{
   mock_server_t *server;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   size_t i;
   mongoc_read_prefs_t *prefs;
   mongoc_cursor_t *cursor;
   const bson_t *doc = NULL;
   future_t *future;
   request_t *request;

   server = mock_mongos_new (WIRE_VERSION_MIN);
   mock_server_run (server);
   client =
      test_framework_client_new_from_uri (mock_server_get_uri (server), NULL);
   collection = mongoc_client_get_collection (client, "test", "test");

   for (i = 0; i < sizeof (modes) / sizeof (mongoc_read_mode_t); i++) {
      prefs = mongoc_read_prefs_new (modes[i]);
      cursor = mongoc_collection_find (
         collection, MONGOC_QUERY_NONE, 0, 0, 0, tmp_bson (NULL), NULL, prefs);

      ASSERT_CMPUINT32 ((uint32_t) 0, ==, mongoc_cursor_get_hint (cursor));
      ASSERT (mongoc_cursor_set_hint (cursor, 1));
      ASSERT_CMPUINT32 ((uint32_t) 1, ==, mongoc_cursor_get_hint (cursor));

      future = future_cursor_next (cursor, &doc);

      request = mock_server_receives_query (
         server, "test.test", expected_flag[i], 0, 0, "{}", NULL);

      mock_server_replies_simple (request, "{}");
      BSON_ASSERT (future_get_bool (future));

      request_destroy (request);
      future_destroy (future);
      mongoc_cursor_destroy (cursor);
      mongoc_read_prefs_destroy (prefs);
   }

   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   mock_server_destroy (server);
}

static void
test_cursor_hint_mongos_cmd (void)
{
   mock_server_t *server;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_cursor_t *cursor;
   size_t i;
   mongoc_read_prefs_t *prefs;
   const bson_t *doc = NULL;
   future_t *future;
   request_t *request;

   server = mock_mongos_new (WIRE_VERSION_FIND_CMD);
   mock_server_run (server);
   client =
      test_framework_client_new_from_uri (mock_server_get_uri (server), NULL);
   collection = mongoc_client_get_collection (client, "test", "test");

   for (i = 0; i < sizeof (modes) / sizeof (mongoc_read_mode_t); i++) {
      prefs = mongoc_read_prefs_new (modes[i]);
      cursor = mongoc_collection_find (
         collection, MONGOC_QUERY_NONE, 0, 0, 0, tmp_bson (NULL), NULL, prefs);

      ASSERT_CMPUINT32 ((uint32_t) 0, ==, mongoc_cursor_get_hint (cursor));
      ASSERT (mongoc_cursor_set_hint (cursor, 1));
      ASSERT_CMPUINT32 ((uint32_t) 1, ==, mongoc_cursor_get_hint (cursor));

      future = future_cursor_next (cursor, &doc);

      request = mock_server_receives_command (
         server, "test", expected_flag[i], 0, 0, "{'find': 'test'}", NULL);

      mock_server_replies_simple (request,
                                  "{'ok': 1,"
                                  " 'cursor': {"
                                  "    'id': 0,"
                                  "    'ns': 'test.test',"
                                  "    'firstBatch': [{}]}}");

      BSON_ASSERT (future_get_bool (future));

      request_destroy (request);
      future_destroy (future);
      mongoc_cursor_destroy (cursor);
      mongoc_read_prefs_destroy (prefs);
   }

   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   mock_server_destroy (server);
}


/* Tests CDRIVER-562: after calling hello to handshake a new connection we
 * must update topology description with the server response. If not, this test
 * fails under auth with "auth failed" because we use the wrong auth protocol.
 */
static void
_test_cursor_hint_no_warmup (bool pooled)
{
   mongoc_client_pool_t *pool = NULL;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   bson_t *q = tmp_bson (NULL);
   mongoc_cursor_t *cursor;
   const bson_t *doc = NULL;
   bson_error_t error;

   if (pooled) {
      pool = test_framework_new_default_client_pool ();
      client = mongoc_client_pool_pop (pool);
   } else {
      client = test_framework_new_default_client ();
   }

   collection = get_test_collection (client, "test_cursor_hint_no_warmup");
   cursor = mongoc_collection_find (
      collection, MONGOC_QUERY_NONE, 0, 0, 0, q, NULL, NULL);

   /* no chance for topology scan, no server selection */
   ASSERT (mongoc_cursor_set_hint (cursor, 1));
   ASSERT_CMPUINT32 (1, ==, mongoc_cursor_get_hint (cursor));

   mongoc_cursor_next (cursor, &doc);
   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);

   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);

   if (pooled) {
      mongoc_client_pool_push (pool, client);
      mongoc_client_pool_destroy (pool);
   } else {
      mongoc_client_destroy (client);
   }
}

static void
test_hint_no_warmup_single (void)
{
   _test_cursor_hint_no_warmup (false);
}

static void
test_hint_no_warmup_pooled (void)
{
   _test_cursor_hint_no_warmup (true);
}


static void
test_tailable_alive (void)
{
   mongoc_client_t *client;
   mongoc_database_t *database;
   char *collection_name;
   mongoc_collection_t *collection;
   bool r;
   bson_error_t error;
   mongoc_cursor_t *cursor;
   const bson_t *doc;

   client = test_framework_new_default_client ();
   database = mongoc_client_get_database (client, "test");
   collection_name = gen_collection_name ("test");

   collection = mongoc_database_get_collection (database, collection_name);
   mongoc_collection_drop (collection, NULL);
   mongoc_collection_destroy (collection);

   collection = mongoc_database_create_collection (
      database,
      collection_name,
      tmp_bson ("{'capped': true, 'size': 10000}"),
      &error);

   ASSERT_OR_PRINT (collection, error);

   r = mongoc_collection_insert_one (
      collection, tmp_bson ("{}"), NULL, NULL, &error);

   ASSERT_OR_PRINT (r, error);

   /* test mongoc_collection_find and mongoc_collection_find_with_opts */
   cursor = mongoc_collection_find (collection,
                                    MONGOC_QUERY_TAILABLE_CURSOR |
                                       MONGOC_QUERY_AWAIT_DATA,
                                    0,
                                    0,
                                    0,
                                    tmp_bson (NULL),
                                    NULL,
                                    NULL);

   ASSERT (mongoc_cursor_more (cursor));
   ASSERT (mongoc_cursor_next (cursor, &doc));

   /* still alive */
   ASSERT (mongoc_cursor_more (cursor));

   /* no next document, but still alive and could return more in the future
    * see CDRIVER-1530 */
   ASSERT (!mongoc_cursor_next (cursor, &doc));
   ASSERT (mongoc_cursor_more (cursor));

   mongoc_cursor_destroy (cursor);

   cursor = mongoc_collection_find_with_opts (
      collection,
      tmp_bson (NULL),
      tmp_bson ("{'tailable': true, 'awaitData': true}"),
      NULL);

   ASSERT (mongoc_cursor_more (cursor));
   ASSERT (mongoc_cursor_next (cursor, &doc));

   /* still alive */
   ASSERT (mongoc_cursor_more (cursor));

   mongoc_cursor_destroy (cursor);

   mongoc_collection_destroy (collection);
   mongoc_database_destroy (database);
   bson_free (collection_name);
   mongoc_client_destroy (client);
}


typedef struct {
   int64_t skip;
   int64_t limit;
   int64_t batch_size;
   int64_t expected_n_return[3];
   int64_t reply_length[3];
} cursor_n_return_test;


static void
_make_reply_batch (bson_string_t *reply,
                   uint32_t n_docs,
                   bool first_batch,
                   bool finished)
{
   uint32_t j;

   bson_string_append_printf (reply,
                              "{'ok': 1, 'cursor': {"
                              "    'id': %d,"
                              "    'ns': 'db.coll',",
                              finished ? 0 : 123);

   if (first_batch) {
      bson_string_append (reply, "'firstBatch': [{}");
   } else {
      bson_string_append (reply, "'nextBatch': [{}");
   }

   for (j = 1; j < n_docs; j++) {
      bson_string_append (reply, ", {}");
   }

   bson_string_append (reply, "]}}");
}


static void
_test_cursor_n_return_find_cmd (mongoc_cursor_t *cursor,
                                mock_server_t *server,
                                cursor_n_return_test *test)
{
   bson_t find_cmd = BSON_INITIALIZER;
   bson_t getmore_cmd = BSON_INITIALIZER;
   const bson_t *doc;
   request_t *request;
   future_t *future;
   int j;
   int reply_no;
   bson_string_t *reply;
   bool cursor_finished;

   BSON_APPEND_UTF8 (&find_cmd, "find", "coll");
   if (test->skip) {
      BSON_APPEND_INT64 (&find_cmd, "skip", test->skip);
   }
   if (test->limit > 0) {
      BSON_APPEND_INT64 (&find_cmd, "limit", test->limit);
   } else if (test->limit < 0) {
      BSON_APPEND_INT64 (&find_cmd, "limit", -test->limit);
      BSON_APPEND_BOOL (&find_cmd, "singleBatch", true);
   }

   if (test->batch_size) {
      BSON_APPEND_INT64 (&find_cmd, "batchSize", BSON_ABS (test->batch_size));
   }

   future = future_cursor_next (cursor, &doc);
   request =
      mock_server_receives_command (server, "db", MONGOC_QUERY_SLAVE_OK, NULL);

   ASSERT (match_bson (request_get_doc (request, 0), &find_cmd, true));

   reply = bson_string_new (NULL);
   _make_reply_batch (reply, (uint32_t) test->reply_length[0], true, false);
   mock_server_replies_simple (request, reply->str);
   bson_string_free (reply, true);

   ASSERT (future_get_bool (future));
   future_destroy (future);
   request_destroy (request);

   /* advance to the end of the batch */
   for (j = 1; j < test->reply_length[0]; j++) {
      ASSERT (mongoc_cursor_next (cursor, &doc));
   }

   for (reply_no = 1; reply_no < 3; reply_no++) {
      /* expect getMore command, send reply_length[reply_no] docs to client */
      future = future_cursor_next (cursor, &doc);
      request = mock_server_receives_command (
         server, "db", MONGOC_QUERY_SLAVE_OK, NULL);

      bson_reinit (&getmore_cmd);
      BSON_APPEND_INT64 (&getmore_cmd, "getMore", 123);
      if (test->expected_n_return[reply_no] && test->batch_size) {
         BSON_APPEND_INT64 (&getmore_cmd,
                            "batchSize",
                            BSON_ABS (test->expected_n_return[reply_no]));
      } else {
         BSON_APPEND_DOCUMENT (
            &getmore_cmd, "batchSize", tmp_bson ("{'$exists': false}"));
      }

      ASSERT (match_bson (request_get_doc (request, 0), &getmore_cmd, true));

      reply = bson_string_new (NULL);
      cursor_finished = (reply_no == 2);
      _make_reply_batch (reply,
                         (uint32_t) test->reply_length[reply_no],
                         false,
                         cursor_finished);

      mock_server_replies_simple (request, reply->str);
      bson_string_free (reply, true);

      ASSERT (future_get_bool (future));
      future_destroy (future);
      request_destroy (request);

      /* advance to the end of the batch */
      for (j = 1; j < test->reply_length[reply_no]; j++) {
         ASSERT (mongoc_cursor_next (cursor, &doc));
      }
   }

   bson_destroy (&find_cmd);
   bson_destroy (&getmore_cmd);
}


static void
_test_cursor_n_return (bool find_with_opts)
{
   cursor_n_return_test tests[] = {{
                                      0,         /* skip              */
                                      0,         /* limit             */
                                      0,         /* batch_size        */
                                      {0, 0, 0}, /* expected_n_return */
                                      {1, 1, 1}  /* reply_length      */
                                   },
                                   {
                                      7,         /* skip              */
                                      0,         /* limit             */
                                      0,         /* batch_size        */
                                      {0, 0, 0}, /* expected_n_return */
                                      {1, 1, 1}  /* reply_length      */
                                   },
                                   {
                                      0,         /* skip              */
                                      3,         /* limit             */
                                      0,         /* batch_size        */
                                      {3, 2, 1}, /* expected_n_return */
                                      {1, 1, 1}  /* reply_length      */
                                   },
                                   {
                                      0,         /* skip              */
                                      5,         /* limit             */
                                      2,         /* batch_size        */
                                      {2, 2, 1}, /* expected_n_return */
                                      {2, 2, 1}  /* reply_length      */
                                   },
                                   {
                                      0,         /* skip              */
                                      4,         /* limit             */
                                      7,         /* batch_size        */
                                      {4, 2, 1}, /* expected_n_return */
                                      {2, 1, 1}  /* reply_length      */
                                   },
                                   {
                                      0,            /* skip              */
                                      -3,           /* limit             */
                                      1,            /* batch_size        */
                                      {-3, -3, -3}, /* expected_n_return */
                                      {1, 1, 1}     /* reply_length      */
                                   }};

   cursor_n_return_test *test;
   size_t i;
   mock_server_t *server;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   bson_t opts = BSON_INITIALIZER;
   mongoc_cursor_t *cursor;

   server = mock_server_with_autoismaster (WIRE_VERSION_FIND_CMD);

   mock_server_run (server);

   client =
      test_framework_client_new_from_uri (mock_server_get_uri (server), NULL);
   collection = mongoc_client_get_collection (client, "db", "coll");

   for (i = 0; i < sizeof (tests) / sizeof (cursor_n_return_test); i++) {
      test = &tests[i];

      if (find_with_opts) {
         bson_reinit (&opts);

         if (test->skip) {
            BSON_APPEND_INT64 (&opts, "skip", test->skip);
         }

         if (test->limit > 0) {
            BSON_APPEND_INT64 (&opts, "limit", test->limit);
         } else if (test->limit < 0) {
            BSON_APPEND_INT64 (&opts, "limit", -test->limit);
            BSON_APPEND_BOOL (&opts, "singleBatch", true);
         }

         if (test->batch_size) {
            BSON_APPEND_INT64 (&opts, "batchSize", test->batch_size);
         }

         cursor = mongoc_collection_find_with_opts (
            collection, tmp_bson (NULL), &opts, NULL);
      } else {
         cursor = mongoc_collection_find (collection,
                                          MONGOC_QUERY_NONE,
                                          (uint32_t) test->skip,
                                          (uint32_t) test->limit,
                                          (uint32_t) test->batch_size,
                                          tmp_bson (NULL),
                                          NULL,
                                          NULL);
      }

      _test_cursor_n_return_find_cmd (cursor, server, test);

      mongoc_cursor_destroy (cursor);
   }

   bson_destroy (&opts);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   mock_server_destroy (server);
}


static void
test_n_return_find_cmd (void)
{
   _test_cursor_n_return (false);
}


static void
test_n_return_find_cmd_with_opts (void)
{
   _test_cursor_n_return (true);
}


/* mongos can return empty final batch with limit and batchSize, which had
 * caused an abort in the cursor */
static void
test_empty_final_batch_live (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   const bson_t *doc;
   int i;
   bool r;

   client = test_framework_new_default_client ();
   mongoc_client_set_error_api (client, 2);
   collection = get_test_collection (client, "test_empty_final_batch_live");

   mongoc_collection_delete_many (
      collection, tmp_bson ("{}"), NULL, NULL, NULL);
   for (i = 0; i < 3; i++) {
      r = mongoc_collection_insert_one (
         collection, tmp_bson ("{}"), NULL, NULL, &error);

      ASSERT_OR_PRINT (r, error);
   }

   cursor = mongoc_collection_find_with_opts (
      collection,
      tmp_bson ("{}"),
      tmp_bson ("{'limit': 3, 'batchSize': 3}"),
      NULL);

   for (i = 0; i < 3; i++) {
      ASSERT (mongoc_cursor_next (cursor, &doc));
   }

   ASSERT (!mongoc_cursor_next (cursor, &doc));
   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);

   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}


static void
test_empty_final_batch (void)
{
   mock_server_t *server;
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_cursor_t *cursor;
   const bson_t *doc;
   future_t *future;
   request_t *request;
   bson_error_t error;

   server = mock_server_with_autoismaster (WIRE_VERSION_FIND_CMD);
   mock_server_run (server);

   client =
      test_framework_client_new_from_uri (mock_server_get_uri (server), NULL);
   collection = mongoc_client_get_collection (client, "db", "coll");
   cursor = mongoc_collection_find_with_opts (
      collection,
      tmp_bson ("{}"),
      tmp_bson ("{'limit': 1, 'batchSize': 1}"),
      NULL);

   /*
    * one document in first batch
    */
   future = future_cursor_next (cursor, &doc);
   request =
      mock_server_receives_command (server, "db", MONGOC_QUERY_SLAVE_OK, NULL);

   mock_server_replies_to_find (
      request, MONGOC_QUERY_SLAVE_OK, 1234, 0, "db.coll", "{}", true);

   ASSERT (future_get_bool (future));
   future_destroy (future);
   request_destroy (request);

   /*
    * empty batch with nonzero cursor id
    */
   future = future_cursor_next (cursor, &doc);
   request =
      mock_server_receives_command (server, "db", MONGOC_QUERY_SLAVE_OK, NULL);

   mock_server_replies_to_find (
      request, MONGOC_QUERY_SLAVE_OK, 1234, 0, "db.coll", "" /* empty */, true);

   ASSERT (!future_get_bool (future));
   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);
   future_destroy (future);
   request_destroy (request);

   /*
    * final batch, empty with zero cursor id
    */
   future = future_cursor_next (cursor, &doc);
   request =
      mock_server_receives_command (server, "db", MONGOC_QUERY_SLAVE_OK, NULL);

   ASSERT_CMPINT64 (
      bson_lookup_int64 (request_get_doc (request, 0), "batchSize"),
      ==,
      (int64_t) 1);

   mock_server_replies_to_find (request,
                                MONGOC_QUERY_SLAVE_OK,
                                0 /* cursor id */,
                                0,
                                "db.coll",
                                "" /* empty */,
                                true);

   ASSERT (!future_get_bool (future));
   ASSERT_OR_PRINT (!mongoc_cursor_error (cursor, &error), error);

   future_destroy (future);
   request_destroy (request);
   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   mock_server_destroy (server);
}


static void
test_error_document_query (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   const bson_t *doc;
   const bson_t *error_doc;

   client = test_framework_new_default_client ();
   mongoc_client_set_error_api (client, 2);
   collection = get_test_collection (client, "test_error_document_query");
   cursor = mongoc_collection_find_with_opts (
      collection, tmp_bson ("{'x': {'$badOperator': 1}}"), NULL, NULL);

   ASSERT (!mongoc_cursor_next (cursor, &doc));
   ASSERT (mongoc_cursor_error_document (cursor, &error, &error_doc));
   ASSERT_CMPUINT32 (error.domain, ==, MONGOC_ERROR_SERVER);
   ASSERT_CONTAINS (error.message, "$badOperator");
   ASSERT_CMPINT32 (
      bson_lookup_int32 (error_doc, "code"), ==, (int32_t) error.code);

   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}


static void
test_error_document_command (void)
{
   mongoc_client_t *client;
   mongoc_cursor_t *cursor;
   bson_error_t error;
   const bson_t *doc;
   const bson_t *error_doc;

   client = test_framework_new_default_client ();
   mongoc_client_set_error_api (client, 2);
   cursor = mongoc_client_command (client,
                                   "test",
                                   MONGOC_QUERY_NONE,
                                   0,
                                   0,
                                   0,
                                   tmp_bson ("{'foo': 1}"), /* no such cmd */
                                   NULL,
                                   NULL);

   ASSERT (!mongoc_cursor_next (cursor, &doc));
   ASSERT (mongoc_cursor_error_document (cursor, &error, &error_doc));
   ASSERT_CMPUINT32 (error.domain, ==, MONGOC_ERROR_SERVER);
   ASSERT_CONTAINS (error.message, "no such");

   ASSERT_CMPINT32 (
      bson_lookup_int32 (error_doc, "code"), ==, (int32_t) error.code);

   mongoc_cursor_destroy (cursor);
   mongoc_client_destroy (client);
}


static void
test_error_document_getmore (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *collection;
   int i;
   bool r;
   bson_error_t error;
   mongoc_cursor_t *cursor;
   const bson_t *doc;
   const bson_t *error_doc;

   client = test_framework_new_default_client ();
   mongoc_client_set_error_api (client, 2);
   collection = get_test_collection (client, "test_error_document_getmore");
   mongoc_collection_drop (collection, NULL);

   for (i = 0; i < 10; i++) {
      r = mongoc_collection_insert_one (
         collection, tmp_bson ("{'i': %d}", i), NULL, NULL, &error);

      ASSERT_OR_PRINT (r, error);
   }

   cursor = mongoc_collection_find_with_opts (
      collection, tmp_bson ("{}"), tmp_bson ("{'batchSize': 2}"), NULL);

   ASSERT (mongoc_cursor_next (cursor, &doc));

   mongoc_collection_drop (collection, NULL);

   ASSERT (mongoc_cursor_next (cursor, &doc));
   ASSERT (!mongoc_cursor_next (cursor, &doc));
   ASSERT (mongoc_cursor_error_document (cursor, &error, &error_doc));

   /* results vary by server version */
   if (error.domain == MONGOC_ERROR_CURSOR) {
      /* MongoDB 3.0 and older */
      ASSERT_ERROR_CONTAINS (error,
                             MONGOC_ERROR_CURSOR,
                             MONGOC_ERROR_CURSOR_INVALID_CURSOR,
                             "cursor is invalid");
   } else {
      /* MongoDB 3.2+ */
      ASSERT_CMPUINT32 (error.domain, ==, MONGOC_ERROR_SERVER);
      ASSERT_CMPINT32 (
         bson_lookup_int32 (error_doc, "code"), ==, (int32_t) error.code);
   }

   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
}

/* test that an error during constructing a find cursor causes the cursor to
 * be marked as failed, so mongoc_cursor_is_alive and mongoc_cursor_more return
 * false */
static void
test_find_error_is_alive (void)
{
   mongoc_client_t *client;
   mongoc_collection_t *coll;
   mongoc_cursor_t *cursor;
   bson_error_t err;
   const bson_t *bson;
   client = test_framework_new_default_client ();
   coll = mongoc_client_get_collection (client, "test", "test");
   cursor =
      mongoc_collection_find (coll,
                              MONGOC_QUERY_NONE,
                              0,
                              0,
                              0,
                              tmp_bson ("{'$query': {}, 'non_dollar': {}}"),
                              NULL,
                              NULL);
   BSON_ASSERT (mongoc_cursor_error (cursor, &err));
   ASSERT_ERROR_CONTAINS (err,
                          MONGOC_ERROR_CURSOR,
                          MONGOC_ERROR_CURSOR_INVALID_CURSOR,
                          "Cannot mix $query with non-dollar field");
   BSON_ASSERT (!mongoc_cursor_is_alive (cursor));
   BSON_ASSERT (!mongoc_cursor_more (cursor));
   BSON_ASSERT (!mongoc_cursor_next (cursor, &bson));
   mongoc_cursor_destroy (cursor);
   mongoc_collection_destroy (coll);
   mongoc_client_destroy (client);
}


void
test_cursor_install (TestSuite *suite)
{
   test_common_cursor_functions_install (suite);
   TestSuite_AddLive (suite, "/Cursor/limit", test_limit);
   TestSuite_AddLive (suite,
                      ""
                      "/Cursor/kill/live",
                      test_kill_cursor_live);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/kill/single", test_kill_cursors_single);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/kill/pooled", test_kill_cursors_pooled);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/kill/single/cmd", test_kill_cursors_single_cmd);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/kill/pooled/cmd", test_kill_cursors_pooled_cmd);
   TestSuite_AddMockServerTest (suite,
                                "/Cursor/client_kill_cursor/with_primary",
                                test_client_kill_cursor_with_primary);
   TestSuite_AddMockServerTest (suite,
                                "/Cursor/client_kill_cursor/without_primary",
                                test_client_kill_cursor_without_primary);
   TestSuite_AddMockServerTest (
      suite,
      "/Cursor/client_kill_cursor/with_primary/wv4",
      test_client_kill_cursor_with_primary_wire_version_4);
   TestSuite_AddMockServerTest (
      suite,
      "/Cursor/client_kill_cursor/without_primary/wv4",
      test_client_kill_cursor_without_primary_wire_version_4);
   TestSuite_AddLive (
      suite, "/Cursor/empty_collection", test_cursor_empty_collection);
   TestSuite_AddLive (
      suite, "/Cursor/new_from_agg", test_cursor_new_from_aggregate);
   TestSuite_AddLive (suite,
                      "/Cursor/new_from_agg_no_initial",
                      test_cursor_new_from_aggregate_no_initial);
   TestSuite_AddFull (suite,
                      "/Cursor/new_from_find",
                      test_cursor_new_from_find,
                      NULL,
                      NULL,
                      test_framework_skip_if_max_wire_version_less_than_4);
   TestSuite_AddFull (suite,
                      "/Cursor/new_from_find_batches",
                      test_cursor_new_from_find_batches,
                      NULL,
                      NULL,
                      test_framework_skip_if_max_wire_version_less_than_4);
   TestSuite_AddLive (suite, "/Cursor/new_invalid", test_cursor_new_invalid);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/new_tailable_await", test_cursor_new_tailable_await);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/int64_t_maxtimems", test_cursor_int64_t_maxtimems);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/new_ignores_fields", test_cursor_new_ignores_fields);
   TestSuite_AddLive (
      suite, "/Cursor/new_invalid_filter", test_cursor_new_invalid_filter);
   TestSuite_AddLive (
      suite, "/Cursor/new_invalid_opts", test_cursor_new_invalid_opts);
   TestSuite_AddLive (suite, "/Cursor/new_static", test_cursor_new_static);
   TestSuite_AddLive (suite, "/Cursor/hint/errors", test_cursor_hint_errors);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/hint/single/secondary", test_hint_single_secondary);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/hint/single/primary", test_hint_single_primary);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/hint/pooled/secondary", test_hint_pooled_secondary);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/hint/pooled/primary", test_hint_pooled_primary);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/hint/mongos", test_cursor_hint_mongos);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/hint/mongos/cmd", test_cursor_hint_mongos_cmd);
   TestSuite_AddLive (
      suite, "/Cursor/hint/no_warmup/single", test_hint_no_warmup_single);
   TestSuite_AddLive (
      suite, "/Cursor/hint/no_warmup/pooled", test_hint_no_warmup_pooled);
   TestSuite_AddLive (suite, "/Cursor/tailable/alive", test_tailable_alive);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/n_return/find_cmd", test_n_return_find_cmd);
   TestSuite_AddMockServerTest (suite,
                                "/Cursor/n_return/find_cmd/with_opts",
                                test_n_return_find_cmd_with_opts);
   TestSuite_AddLive (
      suite, "/Cursor/empty_final_batch_live", test_empty_final_batch_live);
   TestSuite_AddMockServerTest (
      suite, "/Cursor/empty_final_batch", test_empty_final_batch);
   TestSuite_AddLive (
      suite, "/Cursor/error_document/query", test_error_document_query);
   TestSuite_AddLive (
      suite, "/Cursor/error_document/getmore", test_error_document_getmore);
   TestSuite_AddLive (
      suite, "/Cursor/error_document/command", test_error_document_command);
   TestSuite_AddLive (
      suite, "/Cursor/find_error/is_alive", test_find_error_is_alive);
}
