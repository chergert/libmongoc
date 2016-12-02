#ifndef FUTURE_VALUE_H
#define FUTURE_VALUE_H

#include <bson.h>
#include <mongoc.h>


#include "mongoc-server-description.h"
#include "mongoc-topology-private.h"


/**************************************************
 *
 * Generated by build/generate-future-functions.py.
 *
 * DO NOT EDIT THIS FILE.
 *
 *************************************************/


typedef char * char_ptr;
typedef char ** char_ptr_ptr;
typedef const char * const_char_ptr;
typedef bson_error_t * bson_error_ptr;
typedef bson_t * bson_ptr;
typedef const bson_t * const_bson_ptr;
typedef const bson_t ** const_bson_ptr_ptr;
typedef mongoc_bulk_operation_t * mongoc_bulk_operation_ptr;
typedef mongoc_client_t * mongoc_client_ptr;
typedef mongoc_collection_t * mongoc_collection_ptr;
typedef mongoc_cursor_t * mongoc_cursor_ptr;
typedef mongoc_database_t * mongoc_database_ptr;
typedef mongoc_gridfs_file_t * mongoc_gridfs_file_ptr;
typedef mongoc_gridfs_t * mongoc_gridfs_ptr;
typedef mongoc_iovec_t * mongoc_iovec_ptr;
typedef mongoc_index_opt_t * const_mongoc_index_opt_t;
typedef mongoc_server_description_t * mongoc_server_description_ptr;
typedef mongoc_topology_t * mongoc_topology_ptr;
typedef mongoc_write_concern_t * mongoc_write_concern_ptr;
typedef const mongoc_find_and_modify_opts_t * const_mongoc_find_and_modify_opts_ptr;
typedef const mongoc_read_prefs_t * const_mongoc_read_prefs_ptr;
typedef const mongoc_write_concern_t * const_mongoc_write_concern_ptr;

typedef enum {
   future_value_no_type = 0,
   future_value_bool_type,
   future_value_char_ptr_type,
   future_value_char_ptr_ptr_type,
   future_value_int_type,
   future_value_int64_t_type,
   future_value_size_t_type,
   future_value_ssize_t_type,
   future_value_uint32_t_type,
   future_value_const_char_ptr_type,
   future_value_bson_error_ptr_type,
   future_value_bson_ptr_type,
   future_value_const_bson_ptr_type,
   future_value_const_bson_ptr_ptr_type,
   future_value_mongoc_bulk_operation_ptr_type,
   future_value_mongoc_client_ptr_type,
   future_value_mongoc_collection_ptr_type,
   future_value_mongoc_cursor_ptr_type,
   future_value_mongoc_database_ptr_type,
   future_value_mongoc_gridfs_file_ptr_type,
   future_value_mongoc_gridfs_ptr_type,
   future_value_mongoc_insert_flags_t_type,
   future_value_mongoc_iovec_ptr_type,
   future_value_mongoc_query_flags_t_type,
   future_value_const_mongoc_index_opt_t_type,
   future_value_mongoc_server_description_ptr_type,
   future_value_mongoc_ss_optype_t_type,
   future_value_mongoc_topology_ptr_type,
   future_value_mongoc_write_concern_ptr_type,
   future_value_const_mongoc_find_and_modify_opts_ptr_type,
   future_value_const_mongoc_read_prefs_ptr_type,
   future_value_const_mongoc_write_concern_ptr_type,
   future_value_void_type,

} future_value_type_t;

typedef struct _future_value_t
{
   future_value_type_t type;
   union {
      bool bool_value;
      char_ptr char_ptr_value;
      char_ptr_ptr char_ptr_ptr_value;
      int int_value;
      int64_t int64_t_value;
      size_t size_t_value;
      ssize_t ssize_t_value;
      uint32_t uint32_t_value;
      const_char_ptr const_char_ptr_value;
      bson_error_ptr bson_error_ptr_value;
      bson_ptr bson_ptr_value;
      const_bson_ptr const_bson_ptr_value;
      const_bson_ptr_ptr const_bson_ptr_ptr_value;
      mongoc_bulk_operation_ptr mongoc_bulk_operation_ptr_value;
      mongoc_client_ptr mongoc_client_ptr_value;
      mongoc_collection_ptr mongoc_collection_ptr_value;
      mongoc_cursor_ptr mongoc_cursor_ptr_value;
      mongoc_database_ptr mongoc_database_ptr_value;
      mongoc_gridfs_file_ptr mongoc_gridfs_file_ptr_value;
      mongoc_gridfs_ptr mongoc_gridfs_ptr_value;
      mongoc_insert_flags_t mongoc_insert_flags_t_value;
      mongoc_iovec_ptr mongoc_iovec_ptr_value;
      mongoc_query_flags_t mongoc_query_flags_t_value;
      const_mongoc_index_opt_t const_mongoc_index_opt_t_value;
      mongoc_server_description_ptr mongoc_server_description_ptr_value;
      mongoc_ss_optype_t mongoc_ss_optype_t_value;
      mongoc_topology_ptr mongoc_topology_ptr_value;
      mongoc_write_concern_ptr mongoc_write_concern_ptr_value;
      const_mongoc_find_and_modify_opts_ptr const_mongoc_find_and_modify_opts_ptr_value;
      const_mongoc_read_prefs_ptr const_mongoc_read_prefs_ptr_value;
      const_mongoc_write_concern_ptr const_mongoc_write_concern_ptr_value;

   };
} future_value_t;

future_value_t *future_value_new ();

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

void future_value_set_void (future_value_t *future_value);

void future_value_get_void (future_value_t *future_value);


void
future_value_set_bool(
   future_value_t *future_value,
   bool value);

bool
future_value_get_bool (
   future_value_t *future_value);

void
future_value_set_char_ptr(
   future_value_t *future_value,
   char_ptr value);

char_ptr
future_value_get_char_ptr (
   future_value_t *future_value);

void
future_value_set_char_ptr_ptr(
   future_value_t *future_value,
   char_ptr_ptr value);

char_ptr_ptr
future_value_get_char_ptr_ptr (
   future_value_t *future_value);

void
future_value_set_int(
   future_value_t *future_value,
   int value);

int
future_value_get_int (
   future_value_t *future_value);

void
future_value_set_int64_t(
   future_value_t *future_value,
   int64_t value);

int64_t
future_value_get_int64_t (
   future_value_t *future_value);

void
future_value_set_size_t(
   future_value_t *future_value,
   size_t value);

size_t
future_value_get_size_t (
   future_value_t *future_value);

void
future_value_set_ssize_t(
   future_value_t *future_value,
   ssize_t value);

ssize_t
future_value_get_ssize_t (
   future_value_t *future_value);

void
future_value_set_uint32_t(
   future_value_t *future_value,
   uint32_t value);

uint32_t
future_value_get_uint32_t (
   future_value_t *future_value);

void
future_value_set_const_char_ptr(
   future_value_t *future_value,
   const_char_ptr value);

const_char_ptr
future_value_get_const_char_ptr (
   future_value_t *future_value);

void
future_value_set_bson_error_ptr(
   future_value_t *future_value,
   bson_error_ptr value);

bson_error_ptr
future_value_get_bson_error_ptr (
   future_value_t *future_value);

void
future_value_set_bson_ptr(
   future_value_t *future_value,
   bson_ptr value);

bson_ptr
future_value_get_bson_ptr (
   future_value_t *future_value);

void
future_value_set_const_bson_ptr(
   future_value_t *future_value,
   const_bson_ptr value);

const_bson_ptr
future_value_get_const_bson_ptr (
   future_value_t *future_value);

void
future_value_set_const_bson_ptr_ptr(
   future_value_t *future_value,
   const_bson_ptr_ptr value);

const_bson_ptr_ptr
future_value_get_const_bson_ptr_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_bulk_operation_ptr(
   future_value_t *future_value,
   mongoc_bulk_operation_ptr value);

mongoc_bulk_operation_ptr
future_value_get_mongoc_bulk_operation_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_client_ptr(
   future_value_t *future_value,
   mongoc_client_ptr value);

mongoc_client_ptr
future_value_get_mongoc_client_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_collection_ptr(
   future_value_t *future_value,
   mongoc_collection_ptr value);

mongoc_collection_ptr
future_value_get_mongoc_collection_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_cursor_ptr(
   future_value_t *future_value,
   mongoc_cursor_ptr value);

mongoc_cursor_ptr
future_value_get_mongoc_cursor_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_database_ptr(
   future_value_t *future_value,
   mongoc_database_ptr value);

mongoc_database_ptr
future_value_get_mongoc_database_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_gridfs_file_ptr(
   future_value_t *future_value,
   mongoc_gridfs_file_ptr value);

mongoc_gridfs_file_ptr
future_value_get_mongoc_gridfs_file_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_gridfs_ptr(
   future_value_t *future_value,
   mongoc_gridfs_ptr value);

mongoc_gridfs_ptr
future_value_get_mongoc_gridfs_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_insert_flags_t(
   future_value_t *future_value,
   mongoc_insert_flags_t value);

mongoc_insert_flags_t
future_value_get_mongoc_insert_flags_t (
   future_value_t *future_value);

void
future_value_set_mongoc_iovec_ptr(
   future_value_t *future_value,
   mongoc_iovec_ptr value);

mongoc_iovec_ptr
future_value_get_mongoc_iovec_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_query_flags_t(
   future_value_t *future_value,
   mongoc_query_flags_t value);

mongoc_query_flags_t
future_value_get_mongoc_query_flags_t (
   future_value_t *future_value);

void
future_value_set_const_mongoc_index_opt_t(
   future_value_t *future_value,
   const_mongoc_index_opt_t value);

const_mongoc_index_opt_t
future_value_get_const_mongoc_index_opt_t (
   future_value_t *future_value);

void
future_value_set_mongoc_server_description_ptr(
   future_value_t *future_value,
   mongoc_server_description_ptr value);

mongoc_server_description_ptr
future_value_get_mongoc_server_description_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_ss_optype_t(
   future_value_t *future_value,
   mongoc_ss_optype_t value);

mongoc_ss_optype_t
future_value_get_mongoc_ss_optype_t (
   future_value_t *future_value);

void
future_value_set_mongoc_topology_ptr(
   future_value_t *future_value,
   mongoc_topology_ptr value);

mongoc_topology_ptr
future_value_get_mongoc_topology_ptr (
   future_value_t *future_value);

void
future_value_set_mongoc_write_concern_ptr(
   future_value_t *future_value,
   mongoc_write_concern_ptr value);

mongoc_write_concern_ptr
future_value_get_mongoc_write_concern_ptr (
   future_value_t *future_value);

void
future_value_set_const_mongoc_find_and_modify_opts_ptr(
   future_value_t *future_value,
   const_mongoc_find_and_modify_opts_ptr value);

const_mongoc_find_and_modify_opts_ptr
future_value_get_const_mongoc_find_and_modify_opts_ptr (
   future_value_t *future_value);

void
future_value_set_const_mongoc_read_prefs_ptr(
   future_value_t *future_value,
   const_mongoc_read_prefs_ptr value);

const_mongoc_read_prefs_ptr
future_value_get_const_mongoc_read_prefs_ptr (
   future_value_t *future_value);

void
future_value_set_const_mongoc_write_concern_ptr(
   future_value_t *future_value,
   const_mongoc_write_concern_ptr value);

const_mongoc_write_concern_ptr
future_value_get_const_mongoc_write_concern_ptr (
   future_value_t *future_value);


#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif /* FUTURE_VALUE_H */
