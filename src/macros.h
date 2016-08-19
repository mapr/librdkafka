/* Copyright (c) 2015 & onwards. MapR Tech, Inc., All rights reserved */

#ifndef STREAMS_MACROS_H_
#define STREAMS_MACROS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The following code block defines STREAMS_API as the tag for exported
 * functions.
 */
#if defined _WIN32 || defined __CYGWIN__
  #ifdef _STREAMS_IMPLEMENTATION_
      #define STREAMS_API __declspec(dllexport)
  #else
    #ifdef USE_STATIC_STREAMS
      #define STREAMS_API
    #else
      #define STREAMS_API __declspec(dllimport)
    #endif
  #endif
#else
  #if __GNUC__ >= 4
    #define STREAMS_API __attribute__ ((visibility ("default")))
  #else
    #define STREAMS_API
  #endif
#endif

#ifdef __cplusplus
}  /* extern "C" */
#endif

/**
 * Utility macro to check validity of API parameters
 */
#define STREAMS_RETURN_IF_INVALID_PARAM(stmt, ...) \
  if (stmt) {           \
    fprintf(stderr, __VA_ARGS__);                \
    return EINVAL;      \
  }


#endif /* STREAMS_MACROS_H_ */
