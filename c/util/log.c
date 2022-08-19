// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "util.h"
#include "../untrust/cds_u.h"

static JavaVM* jvm = NULL;

static jobject logger = NULL;

static jmethodID trace_method = NULL;
static jmethodID debug_method = NULL;
static jmethodID info_method = NULL;
static jmethodID warn_method = NULL;
static jmethodID error_method = NULL;

void log_init(JavaVM* vm) {
  jvm = vm;

  JNIEnv *env;
  CHECK(JNI_OK == (*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_1));

  jclass logger_factory_class = (*env)->FindClass(env, "org/slf4j/LoggerFactory");
  CHECK(logger_factory_class != NULL);

  jmethodID get_logger_method = (*env)->GetStaticMethodID(env, logger_factory_class, "getLogger", "(Ljava/lang/String;)Lorg/slf4j/Logger;");
  CHECK(get_logger_method != NULL);

  jstring logger_name = (*env)->NewStringUTF(env, "org.signal.cdsi.enclave.Enclave.jni");
  jobject logger_local = (*env)->CallStaticObjectMethod(env, logger_factory_class, get_logger_method, logger_name);
  (*env)->ReleaseStringUTFChars(env, logger_name, NULL);

  CHECK(logger_local != NULL);

  logger = (*env)->NewGlobalRef(env, logger_local);

  jclass logger_class = (*env)->FindClass(env, "org/slf4j/Logger");
  trace_method = (*env)->GetMethodID(env, logger_class, "trace", "(Ljava/lang/String;)V");
  debug_method = (*env)->GetMethodID(env, logger_class, "debug", "(Ljava/lang/String;)V");
  info_method = (*env)->GetMethodID(env, logger_class, "info", "(Ljava/lang/String;)V");
  warn_method = (*env)->GetMethodID(env, logger_class, "warn", "(Ljava/lang/String;)V");
  error_method = (*env)->GetMethodID(env, logger_class, "error", "(Ljava/lang/String;)V");

  CHECK(trace_method != NULL);
  CHECK(debug_method != NULL);
  CHECK(info_method != NULL);
  CHECK(warn_method != NULL);
  CHECK(error_method != NULL);
}

void log_shutdown() {
  if (logger != NULL && jvm != NULL) {
    JNIEnv *env;
    CHECK(JNI_OK == (*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_1));

    (*env)->DeleteGlobalRef(env, logger);
    jvm = NULL;
  }
}

void slf4j_log(uint32_t level, const char* message) {
  if (logger == NULL) {
    return;
  }

  JNIEnv *env;

  jint get_env_result = (*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_1);
  bool should_detach_thread = false;

  jint attach_thread_result = JNI_OK;
  if (get_env_result == JNI_EDETACHED) {
    if (JNI_OK != (attach_thread_result = (*jvm)->AttachCurrentThread(jvm, (void **)&env, NULL))) {
      fprintf(stderr, "JNI detatched and not able to reattach: %d", attach_thread_result);
      return;
    }
    should_detach_thread = true;
  } else {
    CHECK(JNI_OK == get_env_result);
  }

  jmethodID log_method;

  switch (level) {
    case SLF4J_LEVEL_TRACE:
      log_method = trace_method;
      break;

    case SLF4J_LEVEL_DEBUG:
      log_method = debug_method;
      break;

    case SLF4J_LEVEL_INFO:
      log_method = info_method;
      break;

    case SLF4J_LEVEL_WARN:
      log_method = warn_method;
      break;

    default:
      log_method = error_method;
      break;
  }

  jstring log_message = (*env)->NewStringUTF(env, message);
  (*env)->CallObjectMethod(env, logger, log_method, log_message);
  (*env)->ReleaseStringUTFChars(env, log_message, NULL);

  if (should_detach_thread) {
    CHECK(JNI_OK == (*jvm)->DetachCurrentThread(jvm));
  }
}
