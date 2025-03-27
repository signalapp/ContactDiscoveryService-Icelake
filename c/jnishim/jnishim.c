// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#define _GNU_SOURCE

#include <jni.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include "untrust/cds_u.h"
#include "util/util.h"

#include <openenclave/host.h>
#include <openenclave/trace.h>

typedef struct {
  oe_enclave_t* enc;
  size_t num_shards;
  pthread_t* tids;
  atomic_int running_shard_threads;
} cdsi_enclave_t;

// Utility function to shrink a java NIO Buffer.
static void limit_buffer(JNIEnv* env, jobject buffer, size_t limit) {
  jclass cls = (*env)->GetObjectClass(env, buffer);
  jmethodID mid = (*env)->GetMethodID(env, cls, "limit", "(I)Ljava/nio/Buffer;");
  (*env)->CallObjectMethod(env, buffer, mid, limit);
}

// Utility function to throw error_t error.
static void throw_error(JNIEnv* env, error_t code) {
  jclass c = (*env)->FindClass(env, "org/signal/cdsi/enclave/CdsiEnclaveException");
  jmethodID constructor = (*env)->GetMethodID(env, c, "<init>", "(I)V");
  jobject exception = (*env)->NewObject(env, c, constructor, code);
  (*env)->Throw(env, exception);
}

// Utility function to throw openenclave error.
static void throw_oe_error(JNIEnv* env, const char* func, oe_result_t code) {
  jclass c = (*env)->FindClass(env, "org/signal/cdsi/enclave/OpenEnclaveException");
  jmethodID constructor = (*env)->GetMethodID(env, c, "<init>", "(Ljava/lang/String;Ljava/lang/String;I)V");
  jobject exception = (*env)->NewObject(env, c, constructor, (*env)->NewStringUTF(env, func), (*env)->NewStringUTF(env, oe_result_str(code)), code);
  (*env)->Throw(env, exception);
}

////////////////////////////////////////////////////////////////////////////////
// Thread setup functions

struct thread_fn_args
{
    cdsi_enclave_t *enclave;
    size_t shard_id;
    JavaVM *jvm;
};

void* shard_thread_fn(void *input) {
    int retval = 0;
    cdsi_enclave_t *cdsi_enclave = ((struct thread_fn_args *)input)->enclave;
    oe_enclave_t *oe_enclave = cdsi_enclave->enc;
    size_t shard_id = ((struct thread_fn_args *)input)->shard_id;
    JavaVM *jvm = ((struct thread_fn_args *)input)->jvm;
    JNIEnv *env;

    atomic_fetch_add(&cdsi_enclave->running_shard_threads, 1);

    TEST_LOG("shard_thread_fn(%p, %lu) before call enclave", oe_enclave, shard_id);
    oe_result_t result = enclave_run_shard(oe_enclave, &retval, shard_id);
    LOG_WARN("Returned from enclave_run_shard on shard %zu with oe_result %d", shard_id, result);

    atomic_fetch_add(&cdsi_enclave->running_shard_threads, -1);

    return 0;
}

pthread_t run_shard(JNIEnv* env, cdsi_enclave_t *enclave, size_t shard_id) {
    pthread_t tid;

    JavaVM *jvm;
    CHECK(0 == (*env)->GetJavaVM(env, &jvm));

    struct thread_fn_args args = {.enclave = enclave, .shard_id = shard_id, .jvm = jvm};
    struct thread_fn_args *thread_input = calloc(1, sizeof args);
    CHECK(thread_input != NULL);
    *thread_input = args;

    /* create the threads; may be any number, in general */
    CHECK(0 == pthread_create(&tid, NULL, &shard_thread_fn, thread_input));

    char name_buf[16];
    CHECK(snprintf(name_buf, sizeof(name_buf), "shard-%lu", shard_id) > 0);
    CHECK(0 == pthread_setname_np(tid, name_buf));

    return tid;
}

////////////////////////////////////////////////////////////////////////////////
// Logging callback

void host_log_slf4j(
    void* context,
    bool is_enclave,
    const struct tm* t,
    long int usecs,
    oe_log_level_t level,
    uint64_t host_thread_id,
    const char* message) {

  slf4j_log_level_t slf4j_level;

  switch (level) {
    case OE_LOG_LEVEL_WARNING:
      slf4j_level = SLF4J_LEVEL_WARN;
      break;

    case OE_LOG_LEVEL_INFO:
      slf4j_level = SLF4J_LEVEL_INFO;
      break;

    case OE_LOG_LEVEL_VERBOSE:
      slf4j_level = SLF4J_LEVEL_DEBUG;
      break;

    case OE_LOG_LEVEL_MAX:
      slf4j_level = SLF4J_LEVEL_TRACE;
      break;

    default:
      slf4j_level = SLF4J_LEVEL_ERROR;
  }

  slf4j_log(slf4j_level, message);
}

////////////////////////////////////////////////////////////////////////////////
// JNI glue functions to call enclave functions with passed-in buffers.

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv* env;
  CHECK(JNI_OK == (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_1));

  log_init(vm);

  return JNI_VERSION_1_1;
}

void JNI_OnUnload(JavaVM *vm, void *reserved) {
  JNIEnv* env;
  CHECK(JNI_OK == (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_1));

  log_shutdown();
}

JNIEXPORT jlong JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeEnclaveInit
  (JNIEnv *env, jclass c, jlong available_bytes, jdouble load_factor, jint num_shards, jstring path, jboolean simulate) {
  size_t stash_overflow_size = 100;
  uint32_t flags = 0;
  if (simulate) {
    flags |= OE_ENCLAVE_FLAG_SIMULATE;
  }
  char* enclave_function_name;
  cdsi_enclave_t* cdsi_enclave = calloc(1, sizeof(*cdsi_enclave));
  cdsi_enclave->tids = calloc(num_shards, sizeof(cdsi_enclave->tids[0]));
  cdsi_enclave->num_shards = num_shards;
  atomic_init(&cdsi_enclave->running_shard_threads, 0);

  // Set log callback before doing anything else with the enclave,
  // so that all logged actions including during enclave initiation
  // are correctly caught by our logging.
  CHECK(OE_OK == oe_log_set_callback(NULL, host_log_slf4j));

  const char* enclave_path = (*env)->GetStringUTFChars(env, path, 0);
  oe_result_t result = OE_OK;
  LOG_DEBUG("Loading enclave at %s", enclave_path);
  if (OE_OK != (result = oe_create_cds_enclave(
        enclave_path, OE_ENCLAVE_TYPE_AUTO, flags, NULL, 0, &cdsi_enclave->enc))) {
    (*env)->ReleaseStringUTFChars(env, path, enclave_path);
    enclave_function_name = "oe_create_cds_enclave";
    goto error;
    return 0;
  }
  (*env)->ReleaseStringUTFChars(env, path, enclave_path);
  int retval = 0;
  TEST_LOG("Initializing enclave...");
  if (OE_OK != (result = enclave_init(cdsi_enclave->enc, &retval, available_bytes, load_factor, num_shards, stash_overflow_size))) {
    enclave_function_name = "enclave_init";
    goto error;
  } else if (retval != 0) {
    goto error;
  }

  // get array for thread IDs
  for (int i = 0; i < num_shards; i++) {
    CHECK(cdsi_enclave->tids[i] == 0);
    cdsi_enclave->tids[i] = run_shard(env, cdsi_enclave, i);
  }
  TEST_LOG("%p: EnclaveInit success", cdsi_enclave->enc);
  LOG_INFO("Initialized enclave with %d shards and %zu bytes of EPC memory", num_shards, available_bytes);
  return (uint64_t) cdsi_enclave;

error:
  free(cdsi_enclave->tids);
  free(cdsi_enclave);
  if(result != OE_OK) {
    throw_oe_error(env, enclave_function_name, result);
  } else if (retval != 0) {
    throw_error(env, retval);
  }
  return 0;
}

JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeLoadData
  (JNIEnv *env, jclass c, jlong enc, jobject data) {
  size_t data_size = (*env)->GetDirectBufferCapacity(env, data);
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p: LoadData: %ld", cdsi_enc->enc, data_size);
  int retval = 0;
  oe_result_t result;
  if (OE_OK != (result = enclave_load_pb(cdsi_enc->enc, &retval, data_size, (*env)->GetDirectBufferAddress(env, data)))) {
    throw_oe_error(env, "enclave_load_pb", result);
    return;
  } else if (retval != 0) {
    throw_error(env, retval);
    return;
  }
  TEST_LOG("%p: LoadData success", cdsi_enc->enc);
}

JNIEXPORT jlong JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeClientCreate
  (JNIEnv *env, jclass c, jlong enc, jobject out) {
  uint64_t new_client_id = 0;
  int retval = 0;
  size_t out_size = (*env)->GetDirectBufferCapacity(env, out);
  size_t actual_out_size = 0;
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p: ClientCreate: %ld", cdsi_enc->enc, out_size);
  oe_result_t oe_result = enclave_new_client(
      cdsi_enc->enc,
      &retval,
      &new_client_id,
      out_size,
      (*env)->GetDirectBufferAddress(env, out),
      &actual_out_size);
  if (oe_result != OE_OK) {
    throw_oe_error(env, "enclave_new_client", oe_result);
    return 0;
  } else if (retval != 0) {
    TEST_LOG("enclave_new_client returned %d", retval);
    throw_error(env, retval);
    return 0;
  }
  limit_buffer(env, out, actual_out_size);
  TEST_LOG("%p,%p: ClientCreate success: %ld", cdsi_enc->enc, (void*)new_client_id, actual_out_size);
  return new_client_id;
}

JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeClientHandshake
  (JNIEnv *env, jclass c, jlong enc, jlong cli, jobject in, jobject out) {
  size_t in_size = (*env)->GetDirectBufferCapacity(env, in);
  size_t out_size = (*env)->GetDirectBufferCapacity(env, out);
  size_t actual_out_size = 0;
  int retval = 0;
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p,%p: Handshake", cdsi_enc->enc, (void*)cli);
  oe_result_t result;
  if (OE_OK != (result = enclave_handshake(
        cdsi_enc->enc,
        &retval,
        cli,
        in_size,
        (*env)->GetDirectBufferAddress(env, in),
        out_size,
        (*env)->GetDirectBufferAddress(env, out),
        &actual_out_size
      ))) {
    throw_oe_error(env, "enclave_handshake", result);
    return;
  } else if (retval != 0) {
    TEST_LOG("%p,%p: Handshake error: %d", cdsi_enc->enc, (void*)cli, retval);
    throw_error(env, retval);
    return;
  }
  limit_buffer(env, out, actual_out_size);
  TEST_LOG("%p,%p: Handshake success", cdsi_enc->enc, (void*)cli);
}

JNIEXPORT jint JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeClientRate
  (JNIEnv *env, jclass c, jlong enc, jlong cli, jobject in,
   jobject out, jobject old_token_hash, jobject new_token_hash) {
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p,%p: Rate", cdsi_enc->enc, (void*)cli);
  size_t in_size = (*env)->GetDirectBufferCapacity(env, in);
  size_t out_size = (*env)->GetDirectBufferCapacity(env, out);
  size_t actual_out_size = 0;
  size_t old_token_hash_size = (*env)->GetDirectBufferCapacity(env, old_token_hash);
  size_t actual_old_token_hash_size = 0;
  size_t new_token_hash_size = (*env)->GetDirectBufferCapacity(env, new_token_hash);
  size_t actual_new_token_hash_size = 0;
  int retval = 0;
  size_t computed_request_size = 0;
  oe_result_t result;
  if (OE_OK != (result = enclave_rate_limit(
        cdsi_enc->enc,
        &retval,
        cli,
        in_size,
        (*env)->GetDirectBufferAddress(env, in),
        out_size,
        (*env)->GetDirectBufferAddress(env, out),
        &actual_out_size,
        old_token_hash_size,
        (*env)->GetDirectBufferAddress(env, old_token_hash),
        &actual_old_token_hash_size,
        new_token_hash_size,
        (*env)->GetDirectBufferAddress(env, new_token_hash),
        &actual_new_token_hash_size,
        &computed_request_size
      ))) {
    throw_oe_error(env, "enclave_rate_limit", result);
    return -1;
  } else if (retval != 0) {
    TEST_LOG("%p,%p: Rate error: %d", cdsi_enc->enc, (void*)cli, retval);
    throw_error(env, retval);
    return -1;
  }
  limit_buffer(env, out, actual_out_size);
  limit_buffer(env, old_token_hash, actual_old_token_hash_size);
  limit_buffer(env, new_token_hash, actual_new_token_hash_size);
  TEST_LOG("%p,%p: Rate success: %ld", cdsi_enc->enc, (void*)cli, computed_request_size);
  return computed_request_size;
}

JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeClientRun
  (JNIEnv *env, jclass c, jlong enc, jlong cli, jint permits, jobject in, jobject out) {
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p,%p: Run", cdsi_enc->enc, (void*)cli);
  size_t in_size = (*env)->GetDirectBufferCapacity(env, in);
  size_t out_size = (*env)->GetDirectBufferCapacity(env, out);
  size_t actual_out_size = 0;
  TEST_LOG("in_size=%ld,out_size=%ld\n", in_size, out_size);
  int retval = 0;
  oe_result_t oe_result = enclave_run(
    cdsi_enc->enc,
    &retval,
    cli,
    permits,
    in_size,
    (*env)->GetDirectBufferAddress(env, in),
    out_size,
    (*env)->GetDirectBufferAddress(env, out),
    &actual_out_size
  );
  if (oe_result != OE_OK) {
    throw_oe_error(env, "enclave_run", oe_result);
    return;
  } else if (retval != 0) {
    TEST_LOG("%p,%p: Run error: %d", cdsi_enc->enc, (void*)cli, retval);
    throw_error(env, retval);
    return;
  }
  limit_buffer(env, out, actual_out_size);
  TEST_LOG("%p,%p: Run success", cdsi_enc->enc, (void*)cli);
}

JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeClientRetryResponse
  (JNIEnv *env, jclass c, jlong enc, jlong cli, jint retry_after_secs, jobject out) {
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p,%p: Retry response", cdsi_enc->enc, (void*)cli);
  size_t out_size = (*env)->GetDirectBufferCapacity(env, out);
  size_t actual_out_size = 0;

  int retval = 0;
  oe_result_t oe_result = enclave_retry_response(
    cdsi_enc->enc,
    &retval,
    cli,
    retry_after_secs,
    out_size,
    (*env)->GetDirectBufferAddress(env, out),
    &actual_out_size
  );
  if (oe_result != OE_OK) {
    throw_oe_error(env, "enclave_retry_response", oe_result);
    return;
  } else if (retval != 0) {
    TEST_LOG("%p,%p: Run error: %d", cdsi_enc->enc, (void*)cli, retval);
    throw_error(env, retval);
    return;
  }
  limit_buffer(env, out, actual_out_size);
  TEST_LOG("%p,%p: Retry response success", (void*)enc, (void*)cli);
}

JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeClientClose
  (JNIEnv *env, jclass c, jlong enc, jlong cli) {
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p,%p: Close", cdsi_enc->enc, (void*)cli);
  int retval = 0;
  oe_result_t oe_result = enclave_close_client(cdsi_enc->enc, &retval, cli);
  if (oe_result != OE_OK) {
    throw_oe_error(env, "enclave_close_client", oe_result);
    return;
  } else if (retval != 0) {
    TEST_LOG("%p,%p: Close error: %d", cdsi_enc->enc, (void*)cli, retval);
    throw_error(env, retval);
    return;
  }
  TEST_LOG("%p,%p: Close success", cdsi_enc->enc, (void*)cli);
}

JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeEnclaveAttest
  (JNIEnv *env, jclass c, jlong enc) {
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p: Attest", cdsi_enc->enc);
  LOG_DEBUG("Renewing enclave attestation");
  int retval = 0;
  oe_result_t result;
  if (OE_OK != (result = enclave_attest(cdsi_enc->enc, &retval))) {
    throw_oe_error(env, "enclave_attest", result);
    return;
  } else if (retval != 0) {
    TEST_LOG("%p: Attest error: %d", cdsi_enc->enc, retval);
    throw_error(env, retval);
    return;
  }
  TEST_LOG("%p: Attest success", cdsi_enc->enc);
}


JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeEnclaveClose
  (JNIEnv *env, jclass c, jlong enc) {
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p: Close", cdsi_enc->enc);
  LOG_DEBUG("Closing enclave");
  oe_result_t result=OE_OK;
  int retval=0;
  char* enclave_function_name;

  if(OE_OK != (result=enclave_stop_shards(cdsi_enc->enc, &retval))) {
    TEST_LOG("enclave_stop_shards result: %d (%s)", result, oe_result_str(result));
    enclave_function_name = "enclave_stop_shards";
    goto finish;
  } else if (retval != 0) {
    TEST_LOG("%p: enclave_stop_shards error: %d", cdsi_enc->enc, retval);
    goto finish;
  }

  for (size_t i = 0; i < cdsi_enc->num_shards; ++i)
  {
      pthread_join(cdsi_enc->tids[i], 0);
  }


  if(OE_OK != (result=oe_terminate_enclave(cdsi_enc->enc))) {
    TEST_LOG("oe_terminate_enclave result: %d (%s)", result, oe_result_str(result));
    enclave_function_name = "oe_terminate_enclave";
    goto finish;
  }
finish:
  free(cdsi_enc->tids);
  free(cdsi_enc);
  if(result != OE_OK) {
    throw_oe_error(env, enclave_function_name, result);
  } else if (retval != 0) {
    throw_error(env, retval);
    return;
  }

  LOG_INFO("Closed enclave");
}


JNIEXPORT void JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeEnclaveTableStatistics
  (JNIEnv *env, jclass c, jlong enc, jobject out) {
  cdsi_enclave_t* cdsi_enc = (cdsi_enclave_t*)enc;
  TEST_LOG("%p: Table Statistics", cdsi_enc->enc);
  size_t out_size = (*env)->GetDirectBufferCapacity(env, out);
  size_t actual_out_size = 0;
  TEST_LOG("out_size=%ld\n", out_size);
  int retval = 0;
  oe_result_t oe_result = enclave_table_statistics(
    cdsi_enc->enc,
    &retval,
    out_size,
    (*env)->GetDirectBufferAddress(env, out),
    &actual_out_size
  );
  if (oe_result != OE_OK) {
    throw_oe_error(env, "enclave_table_statistics", oe_result);
    return;
  } else if (retval != 0) {
    TEST_LOG("%p: Table Statistics error: %d", cdsi_enc->enc, retval);
    throw_error(env, retval);
    return;
  }
  limit_buffer(env, out, actual_out_size);
  TEST_LOG("%p: Table Statistics success", cdsi_enc->enc);
  return;
}

JNIEXPORT jint JNICALL Java_org_signal_cdsi_enclave_Enclave_nativeGetRunningShardThreadCount
  (JNIEnv *env, jclass c, jlong enc) {

  return atomic_load(&((cdsi_enclave_t*)enc)->running_shard_threads);
}
