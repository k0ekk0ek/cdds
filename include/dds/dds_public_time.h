/** @file
 *
 * @brief DDS C Time support API
 *
 * @todo add copyright header?
 *
 * This header file defines the public API of the in the
 * VortexDDS C language binding.
 */
#ifndef DDS_TIME_H
#define DDS_TIME_H

#include "os/os_public.h"

#if defined (__cplusplus)
extern "C" {
#endif

/* TODO: Set dllexport/dllimport for other supporting compilers too; e.g. clang, gcc using CMake generate export header. */
#if defined (_WIN32)
  #if defined(vddsc_EXPORTS)
    #define DDS_EXPORT extern __declspec (dllexport)
  #else
    #define DDS_EXPORT extern __declspec (dllimport)
  #endif
#else
  #define DDS_EXPORT
#endif

/*
  Times are represented using a 64-bit signed integer, encoding
  nanoseconds since the epoch. Considering the nature of these
  systems, one would best use TAI, International Atomic Time, rather
  than something UTC, but availability may be limited.

  Valid times are non-negative and times up to 2**63-2 can be
  represented. 2**63-1 is defined to represent, essentially, "never".
  This is good enough for a couple of centuries.
*/

/** Absolute Time definition */
typedef int64_t dds_time_t;

/** Relative Time definition */
typedef int64_t dds_duration_t;

/** @name Macro definition for time units in nanoseconds.
  @{**/
#define DDS_NSECS_IN_SEC 1000000000LL
#define DDS_NSECS_IN_MSEC 1000000LL
#define DDS_NSECS_IN_USEC 1000LL
/** @}*/

/** @name Infinite timeout for indicate absolute time */
#define DDS_NEVER ((dds_time_t) INT64_MAX)

/** @name Infinite timeout for relative time */
#define DDS_INFINITY ((dds_duration_t) INT64_MAX)

/** @name Macro definition for time conversion from nanoseconds
  @{**/
#define DDS_SECS(n) ((n) * DDS_NSECS_IN_SEC)
#define DDS_MSECS(n) ((n) * DDS_NSECS_IN_MSEC)
#define DDS_USECS(n) ((n) * DDS_NSECS_IN_USEC)
/** @}*/

/**
 * Description : This operation returns the current time (in nanoseconds)
 *
 * Arguments :
 *   -# Returns current time
 */
DDS_EXPORT dds_time_t dds_time (void);

/**
 * Description : This operation blocks the calling thread until the relative time
 * n has elapsed
 *
 * Arguments :
 *   -# n Relative Time to block a thread
 */
DDS_EXPORT void dds_sleepfor (dds_duration_t n);

/**
 * Description : This operation blocks the calling thread until the absolute time
 * n has elapsed
 *
 * Arguments :
 *   -# n absolute Time to block a thread
 */
DDS_EXPORT void dds_sleepuntil (dds_time_t n);

#undef DDS_EXPORT

#if defined (__cplusplus)
}
#endif
#endif
