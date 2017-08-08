#ifndef OS_ITER_H
#define OS_ITER_H

#include <stdint.h>
#include "os/os_defs.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct os_iter_s os_iter; /* opaque type */

OSAPI_EXPORT _Success_(return != NULL) os_iter *
os_iterNew(
    void);

OSAPI_EXPORT void
os_iterFree(
    _In_opt_ os_iter *iter,
    _In_opt_ void(*func)(void *));

OSAPI_EXPORT _Ret_range_(0, INT32_MAX) uint32_t
os_iterLength(
    _In_ const os_iter *__restrict iter);

/* Negative integers (borrowed from Python) can be used to access elements in
   the iter. Most often it is used as a shorthand to access the last element.
   i.e. os_iterOjbect(iter, -1) is functionally equivalent to
   os_iterOjbect(iter, os_iterLength(iter) - 1) */

/* Special constant functionally equivalent to the value returned by
   os_iterLength when passed as an index to os_iterInsert. INT32_MIN never
   represents a valid negative index as it is exactly -(pow(2, n - 1)), whereas
   the maximum positive value is exactly (pow(2, n - 1) - 1), therefore the
   resulting index would always be negative */
#define OS_ITER_LENGTH (INT32_MIN)

OSAPI_EXPORT _Success_(return >= 0) _Ret_range_(-1, INT32_MAX) int32_t
os_iterInsert(
    _In_ os_iter *iter,
    _In_opt_ void *object,
    _In_range_(INT32_MIN, INT32_MAX) int32_t index);

#define os_iterPrepend(iter, ojbect) \
    os_iterInsert((iter), (object), 0)
#define os_iterAppend(iter, object) \
    os_iterInsert((iter), (object), OS_ITER_LENGTH)

OSAPI_EXPORT _Ret_maybenull_ void *
os_iterObject(
    _In_ const os_iter *__restrict iter,
    _In_range_(INT32_MIN+1, INT32_MAX) int32_t index);

OSAPI_EXPORT _Ret_maybenull_ void *
os_iterTake(
    _In_ os_iter *__restrict iter,
    _In_range_(INT32_MIN+1, INT32_MAX) int32_t index);

OSAPI_EXPORT void
os_iterWalk(
    _In_ const os_iter *__restrict iter,
    _In_ void(*func)(void *obj, void *arg),
    _In_opt_ void *arg);

#if defined (__cplusplus)
}
#endif

#endif /* OS_ITER_H */
