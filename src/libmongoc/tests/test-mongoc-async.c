#include <mongoc/mongoc.h>
#include <mongoc/mongoc-client-private.h>

#include "mongoc/mongoc-util-private.h"
#include "mongoc/mongoc-async-private.h"
#include "mongoc/mongoc-async-cmd-private.h"
#include "TestSuite.h"
#include "mock_server/mock-server.h"
#include "mock_server/future-functions.h"
#include "mongoc/mongoc-errno-private.h"
#include "test-libmongoc.h"

#undef MONGOC_LOG_DOMAIN
#define MONGOC_LOG_DOMAIN "async-test"

#define TIMEOUT 10000 /* milliseconds */
#define NSERVERS 10

struct result {
   int32_t server_id;
   bool finished;
};

static mongoc_stream_t *
get_localhost_stream (uint16_t port)
{
   int errcode;
   int r;
   struct sockaddr_in server_addr = {0};
   mongoc_socket_t *conn_sock;
   conn_sock = mongoc_socket_new (AF_INET, SOCK_STREAM, 0);
   BSON_ASSERT (conn_sock);

   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons (port);
   server_addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
   r = mongoc_socket_connect (
      conn_sock, (struct sockaddr *) &server_addr, sizeof (server_addr), 0);

   errcode = mongoc_socket_errno (conn_sock);
   if (!(r == 0 || MONGOC_ERRNO_IS_AGAIN (errcode))) {
      fprintf (stderr,
               "mongoc_socket_connect unexpected return: "
               "%d (errno: %d)\n",
               r,
               errcode);
      fflush (stderr);
      abort ();
   }

   return mongoc_stream_socket_new (conn_sock);
}


static void
test_ismaster_helper (mongoc_async_cmd_t *acmd,
                      mongoc_async_cmd_result_t result,
                      const bson_t *bson,
                      int64_t duration_usec)
{
   struct result *r = (struct result *) acmd->data;
   bson_iter_t iter;
   bson_error_t *error = &acmd->error;

   /* ignore the connected event. */
   if (result == MONGOC_ASYNC_CMD_CONNECTED) {
      return;
   }

   if (result != MONGOC_ASYNC_CMD_SUCCESS) {
      fprintf (stderr, "error: %s\n", error->message);
   }
   ASSERT_CMPINT (result, ==, MONGOC_ASYNC_CMD_SUCCESS);

   BSON_ASSERT (bson_iter_init_find (&iter, bson, "serverId"));
   BSON_ASSERT (BSON_ITER_HOLDS_INT32 (&iter));
   r->server_id = bson_iter_int32 (&iter);
   r->finished = true;
}


static void
test_ismaster_impl (bool with_ssl)
{
   mock_server_t *servers[NSERVERS];
   mongoc_async_t *async;
   mongoc_stream_t *sock_streams[NSERVERS];
   mongoc_async_cmd_setup_t setup = NULL;
   void *setup_ctx = NULL;
   uint16_t ports[NSERVERS];
   struct result results[NSERVERS];
   int i;
   int offset;
   int server_id;
   bson_t q = BSON_INITIALIZER;
   future_t *future;
   request_t *request;
   char *reply;

#ifdef MONGOC_ENABLE_SSL
   mongoc_ssl_opt_t sopt = {0};
   mongoc_ssl_opt_t copt = {0};
#endif

   if (!TestSuite_CheckMockServerAllowed ()) {
      return;
   }

   BSON_ASSERT (bson_append_int32 (&q, "isMaster", 8, 1));

   for (i = 0; i < NSERVERS; i++) {
      servers[i] = mock_server_new ();

#ifdef MONGOC_ENABLE_SSL
      if (with_ssl) {
         sopt.weak_cert_validation = true;
         sopt.pem_file = CERT_SERVER;
         sopt.ca_file = CERT_CA;

         mock_server_set_ssl_opts (servers[i], &sopt);
      }
#endif

      ports[i] = mock_server_run (servers[i]);
   }

   async = mongoc_async_new ();

   for (i = 0; i < NSERVERS; i++) {
      sock_streams[i] = get_localhost_stream (ports[i]);

#ifdef MONGOC_ENABLE_SSL
      if (with_ssl) {
         copt.ca_file = CERT_CA;
         copt.weak_cert_validation = 1;

         sock_streams[i] = mongoc_stream_tls_new_with_hostname (
            sock_streams[i], NULL, &copt, 1);
         setup = mongoc_async_cmd_tls_setup;
         setup_ctx = (void *) "127.0.0.1";
      }
#endif

      results[i].finished = false;

      mongoc_async_cmd_new (async,
                            sock_streams[i],
                            false,
                            NULL /* dns result, n/a. */,
                            NULL, /* initiator. */
                            0,    /* initiate delay. */
                            setup,
                            setup_ctx,
                            "admin",
                            &q,
                            &test_ismaster_helper,
                            (void *) &results[i],
                            TIMEOUT);
   }

   future = future_async_run (async);

   /* start in the middle - prove scanner handles replies in any order */
   offset = NSERVERS / 2;
   for (i = 0; i < NSERVERS; i++) {
      server_id = (i + offset) % NSERVERS;
      request = mock_server_receives_command (
         servers[server_id], "admin", MONGOC_QUERY_SLAVE_OK, NULL);

      /* use "serverId" field to distinguish among responses */
      reply = bson_strdup_printf ("{'ok': 1,"
                                  " 'ismaster': true,"
                                  " 'minWireVersion': 0,"
                                  " 'maxWireVersion': 1000,"
                                  " 'serverId': %d}",
                                  server_id);

      mock_server_replies_simple (request, reply);
      bson_free (reply);
      request_destroy (request);
   }

   BSON_ASSERT (future_wait (future));

   for (i = 0; i < NSERVERS; i++) {
      if (!results[i].finished) {
         fprintf (stderr, "command %d not finished\n", i);
         abort ();
      }

      ASSERT_CMPINT (i, ==, results[i].server_id);
   }

   mongoc_async_destroy (async);
   future_destroy (future);
   bson_destroy (&q);

   for (i = 0; i < NSERVERS; i++) {
      mock_server_destroy (servers[i]);
      mongoc_stream_destroy (sock_streams[i]);
   }
}

static void
test_ismaster (void)
{
   test_ismaster_impl (false);
}


#if defined(MONGOC_ENABLE_SSL_OPENSSL)
static void
test_ismaster_ssl (void)
{
   test_ismaster_impl (true);
}
#else

static void
test_large_ismaster_helper (mongoc_async_cmd_t *acmd,
                            mongoc_async_cmd_result_t result,
                            const bson_t *bson,
                            int64_t duration_usec)
{
   bson_iter_t iter;
   bson_error_t *error = &acmd->error;

   /* ignore the connected event. */
   if (result == MONGOC_ASYNC_CMD_CONNECTED) {
      return;
   }

   if (result != MONGOC_ASYNC_CMD_SUCCESS) {
      fprintf (stderr, "error: %s\n", error->message);
   }
   ASSERT_CMPINT (result, ==, MONGOC_ASYNC_CMD_SUCCESS);

   ASSERT_HAS_FIELD(bson, "ismaster");
   BSON_ASSERT (bson_iter_init_find (&iter, bson, "ismaster"));
   BSON_ASSERT (BSON_ITER_HOLDS_BOOL (&iter) && bson_iter_bool (&iter));
}

static void
test_large_ismaster (void *ctx)
{
   mongoc_async_t *async;
   mongoc_stream_t *sock_stream;
   int i = 0;
   bson_t q = BSON_INITIALIZER;

#ifdef MONGOC_ENABLE_SSL
   mongoc_ssl_opt_t ssl_opts;
#endif

   /* Inflate the size of the isMaster message with other fields to ~1MB.
    * mongod should ignore them, but this tests that CDRIVER-2483 is fixed.
    */
   BSON_ASSERT (bson_append_int32 (&q, "isMaster", 8, 1));
   while (q.len < 1024 * 1024) {
      char buf[11];
      bson_snprintf (buf, sizeof (buf), "key_%06d", i++);
      BSON_APPEND_INT32 (&q, buf, 0);
   }

   sock_stream = get_localhost_stream (test_framework_get_port ());

#ifdef MONGOC_ENABLE_SSL
   if (test_framework_get_ssl ()) {
      ssl_opts = *test_framework_get_ssl_opts ();
      sock_stream =
         mongoc_stream_tls_new_with_hostname (sock_stream, NULL, &ssl_opts, 1);
   }
#endif

   async = mongoc_async_new ();
   mongoc_async_cmd_new (async,
                         sock_stream,
                         false, /* is setup done. */
                         NULL /* dns result, n/a. */,
                         NULL, /* initiator. */
                         0,    /* initiate delay. */
#ifdef MONGOC_ENABLE_SSL
                         test_framework_get_ssl () ? mongoc_async_cmd_tls_setup
                                                   : NULL,
#else
                         NULL,
#endif
                         NULL,
                         "admin",
                         &q,
                         &test_large_ismaster_helper,
                         NULL,
                         TIMEOUT);

   mongoc_async_run (async);
   mongoc_async_destroy (async);
   mongoc_stream_destroy (sock_stream);
   bson_destroy (&q);
}
#endif

typedef struct _stream_with_result_t {
   mongoc_stream_t *stream;
   bool finished;
} stream_with_result_t;

static void
test_ismaster_delay_callback (mongoc_async_cmd_t *acmd,
                              mongoc_async_cmd_result_t result,
                              const bson_t *bson,
                              int64_t duration_usec)
{
   ((stream_with_result_t *) acmd->data)->finished = true;
}

static mongoc_stream_t *
test_ismaster_delay_initializer (mongoc_async_cmd_t *acmd)
{
   return ((stream_with_result_t *) acmd->data)->stream;
}

static void
test_ismaster_delay ()
{
   /* test that a delayed cmd works. */
   mock_server_t *server = mock_server_with_autoismaster (WIRE_VERSION_MAX);
   mongoc_async_t *async = mongoc_async_new ();
   bson_t ismaster_cmd = BSON_INITIALIZER;
   stream_with_result_t stream_with_result = {0};
   int64_t start = bson_get_monotonic_time ();

   mock_server_run (server);

   stream_with_result.stream =
      get_localhost_stream (mock_server_get_port (server));
   stream_with_result.finished = false;

   BSON_ASSERT (bson_append_int32 (&ismaster_cmd, "isMaster", 8, 1));
   mongoc_async_cmd_new (async,
                         NULL,  /* stream, initialized after delay. */
                         false, /* is setup done. */
                         NULL,  /* dns result. */
                         test_ismaster_delay_initializer,
                         100,  /* delay 100ms. */
                         NULL, /* setup function. */
                         NULL, /* setup ctx. */
                         "admin",
                         &ismaster_cmd,
                         &test_ismaster_delay_callback,
                         &stream_with_result,
                         TIMEOUT);

   mongoc_async_run (async);

   /* it should have taken at least 100ms to finish. */
   ASSERT_CMPINT64 (
      bson_get_monotonic_time () - start, >, (int64_t) (100 * 1000));
   BSON_ASSERT (stream_with_result.finished);

   bson_destroy (&ismaster_cmd);
   mongoc_stream_destroy (stream_with_result.stream);
   mongoc_async_destroy (async);
   mock_server_destroy (server);
}

void
test_async_install (TestSuite *suite)
{
   TestSuite_AddMockServerTest (suite, "/Async/ismaster", test_ismaster);
#if defined(MONGOC_ENABLE_SSL_OPENSSL)
   TestSuite_AddMockServerTest (
      suite, "/Async/ismaster_ssl", test_ismaster_ssl);
#else
   /* Skip this test on OpenSSL since was having issues connecting. */
   /* Skip on Windows until CDRIVER-3519 is resolved. */
   TestSuite_AddFull (suite,
                      "/Async/large_ismaster",
                      test_large_ismaster,
                      NULL /* dtor */,
                      NULL /* ctx */,
                      test_framework_skip_if_not_single,
                      test_framework_skip_if_windows);
#endif
   TestSuite_AddMockServerTest (suite, "/Async/delay", test_ismaster_delay);
}
