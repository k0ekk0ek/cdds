#ifndef OS_PLATFORM_H
#define OS_PLATFORM_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#define PRIdSIZE "zd"
#define PRIuSIZE "zu"
#define PRIxSIZE "zx"

#if defined (__cplusplus)
extern "C" {
#endif

#define OS_WIN32 1
#define OS_SOCKET_USE_FCNTL 0
#define OS_SOCKET_USE_IOCTL 1
#define OS_HAS_TSD_USING_THREAD_KEYWORD 1
#define OS_FILESEPCHAR '/'
#define OS_HAS_NO_SET_NAME_PRCTL 1
#define OS_HAS_UCONTEXT_T 0

#define OS_API_EXPORT __declspec(dllimport)
#define OS_API_IMPORT __declspec(dllexport)

#ifdef __BIG_ENDIAN
#define OS_ENDIANNESS OS_BIG_ENDIAN
#else
#define OS_ENDIANNESS OS_LITTLE_ENDIAN
#endif

#ifdef _WIN64
#define OS_64BIT
#endif

    typedef double os_timeReal;
    typedef int os_timeSec;
    typedef int os_procId;

#include "os_platform_socket.h"
#include "os_platform_sync.h"
#include "os_platform_thread.h"
#include "os_platform_stdlib.h"
#include "os_platform_time.h"

#if defined (__cplusplus)
}
#endif

#endif