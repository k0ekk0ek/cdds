#ifndef OS_H
#define OS_H

#include "os_public.h"

#include "os_defs.h"
#include "os_thread.h"
#include "os_sync.h"
#include "os_time.h"
#include "os_atomics.h"
#include "os_socket.h"
#include "os_heap.h"
#include "os_stdlib.h"
#include "os_report.h"
#include "os_init.h"
#include "os_process.h"
#include "os_errno.h"

#if __linux__ == 1
#include "../linux/include/os_platform.h"
#elif __vxworks == 1
#if __RTP__ == 1
#include "../vxworks_rtp/include/os_platform.h"
#else
#include "../vxworks_km/include/os_platform.h"
#endif
#elif __sun == 1
#include "../solaris/include/os_platform.h"
#elif defined(__INTEGRITY)
#include "../integrity/include/os_platform.h"
#elif  __PikeOS__ == 1
#include "../pikeos3/include/os_platform.h"
#elif defined(__QNX__)
#include "../qnx/include/os_platform.h"
#elif defined(_MSC_VER)
#ifdef _WIN32_WCE
#include "../wince/include/os_platform.h"
#else
#include "../win32/include/os_platform.h"
#endif
#elif defined __APPLE__
#include "../darwin/include/os_platform.h"
#elif defined __CYGWIN__
#include "../cygwin/include/os_platform.h"
#else
#error "Platform missing from os.h list"
#endif

#define OSPL_VERSION_STR "aap"
#define OSPL_HOST_STR "noot"
#define OSPL_TARGET_STR "mies"
#define OSPL_INNER_REV_STR "wim"
#define OSPL_OUTER_REV_STR "zus"

#endif
