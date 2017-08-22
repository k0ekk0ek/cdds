/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to TO_YEAR PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */

#ifdef PIKEOS_POSIX
#include <lwip_config.h>
#endif

#include "os/os.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define os_report_iserror(report) \
        (((report)->reportType >= OS_ERROR && (report)->code != 0))

#define OS_REPORT_TYPE_DEBUG     (1u)
#define OS_REPORT_TYPE_INFO      (1u<<OS_INFO)
#define OS_REPORT_TYPE_WARNING   (1u<<OS_WARNING)
#define OS_REPORT_TYPE_API_INFO  (1u<<OS_API_INFO)
#define OS_REPORT_TYPE_ERROR     (1u<<OS_ERROR)
#define OS_REPORT_TYPE_CRITICAL  (1u<<OS_CRITICAL)
#define OS_REPORT_TYPE_FATAL     (1u<<OS_FATAL)
#define OS_REPORT_TYPE_REPAIRED  (1u<<OS_REPAIRED)
#define OS_REPORT_TYPE_NONE      (1u<<OS_NONE)

#define OS_REPORT_TYPE_FLAG(x)   (1u<<(x))
#define OS_REPORT_IS_ALWAYS(x)      ((x) & (OS_REPORT_TYPE_CRITICAL | OS_REPORT_TYPE_FATAL | OS_REPORT_TYPE_REPAIRED))
#define OS_REPORT_IS_DEPRECATED(x)  ((x) & OS_REPORT_TYPE_API_INFO)
#define OS_REPORT_IS_WARNING(x)     ((x) & OS_REPORT_TYPE_WARNING)
#define OS_REPORT_IS_ERROR(x)    ((x) & (OS_REPORT_TYPE_ERROR | OS_REPORT_TYPE_CRITICAL | OS_REPORT_TYPE_FATAL | OS_REPORT_TYPE_REPAIRED))

typedef struct os_reportStack_s {
    int count;
    unsigned typeset;
    const char *file;
    int lineno;
    const char *signature;
    os_iter *reports;  /* os_reportEventV1 */
} *os_reportStack;

void os_report_append(os_reportStack _this, const os_reportEventV1 report);

static int os_report_fprintf(FILE *file, const char *format, ...);

void os_report_free(os_reportEventV1 report);

static FILE* error_log = NULL;
static FILE* info_log = NULL;

static os_mutex reportMutex;
static os_mutex reportPluginMutex;

static bool inited = false;
//
/**
 * Process global verbosity level for OS_REPORT output. os_reportType
 * values >= this value will be written.
 * This value defaults to OS_INFO, meaning that all types 'above' (i.e.
 * other than) OS_DEBUG will be written and OS_DEBUG will not be.
 */
os_reportType os_reportVerbosity = OS_INFO;

/**
 * Labels corresponding to os_reportType values.
 * @see os_reportType
 */
const char *os_reportTypeText [] = {
        "DEBUG",
        "INFO",
        "WARNING",
        "API_INFO",
        "ERROR",
        "CRITICAL",
        "FATAL",
        "REPAIRED",
        "NONE"
};

enum os_report_logType {
    OS_REPORT_INFO,
    OS_REPORT_ERROR
};

static char * os_report_defaultInfoFileName = "vortex-info.log";
static char * os_report_defaultErrorFileName = "vortex-error.log";
static const char os_env_logdir[] = "VORTEX_LOGPATH";
static const char os_env_infofile[] = "VORTEX_INFOFILE";
static const char os_env_errorfile[] = "VORTEX_ERRORFILE";
static const char os_env_verbosity[] = "VORTEX_VERBOSITY";
static const char os_env_append[] = "VORTEX_LOGAPPEND";
#if defined _WRS_KERNEL
static const char os_default_logdir[] = "/tgtsvr";
#else
static const char os_default_logdir[] = ".";
#endif

static const char
os__report_version[] = OSPL_VERSION_STR;

static const char
os__report_inner_revision[] = OSPL_INNER_REV_STR;

static const char
os__report_outer_revision[] = OSPL_OUTER_REV_STR;

static void
os_reportResetApiInfo (
        os_reportInfo *report);

static void
os_reportSetApiInfo (
        const char *context,
        const char *file,
        int32_t line,
        int32_t code,
        const char *message);

static FILE * open_socket (char *host, unsigned short port)
{
    FILE * file = NULL;
    struct sockaddr_storage sa;
    os_socket sock;
    char msg[64];
    const char *errstr;

    if ((sock = os_sockNew(AF_INET, SOCK_STREAM)) < 0) {
        errstr = "socket";
        goto err_socket;
    }

    memset((char *)&sa, 0, sizeof(sa));
    (void)os_sockaddrStringToAddress(host, (os_sockaddr *)&sa, true);
    os_sockaddrSetPort((os_sockaddr *)&sa, htons(port));

    if (connect (sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        errstr = "connect";
        goto err_connect;
    }

    file = fdopen ((int)sock, "w"); /* Type casting is done for the warning of possible loss of data for Parameter "sock" with the type of "os_socket" */

    return file;

/* Error handling */
err_connect:
    (void) close((int)sock); /* Type casting is done for the warning of possible loss of data for Parameter "sock" with the type of "os_socket" */
err_socket:
    (void)os_strerror_r(os_getErrno(), msg, sizeof(msg));
    os_report_fprintf(stderr, "%s: %s\n", errstr, msg);

    return NULL;
}

static FILE *
os_open_file (char * file_name)
{
    FILE *logfile=NULL;
    char host[256];
    unsigned short port;
    char *dir, *file, *fmt, *str;
    int ret;
    size_t len;
    os_result res = os_resultSuccess;

    /* OSPL-4002: Only OSPL_INFOFILE and OSPL_ERRORFILE can specify a host:port
                  combination. Since OSPL_LOGPATH and OSPL_INFOFILE/
                  OSPL_ERRORFILE are concatenated we need to strip the prefix
                  from file_name and then check if we should enter tcp mode.
                  This is considered a temporary workaround and will be removed
                  once the work specified in ticket OSPL-4091 is done */

    if (strcmp (file_name, "<stdout>") == 0) {
        logfile = stdout;
    } else if (strcmp (file_name, "<stderr>") == 0) {
        logfile = stderr;
    } else {
        dir = os_getenv (os_env_logdir);
        if (dir == NULL) {
            dir = (char *)os_default_logdir;
        }

        len = strlen (dir) + 2; /* '/' + '\0' */
        str = os_malloc (len);
        if (str != NULL) {
            (void)snprintf (str, len, "%s/", dir);
            dir = os_fileNormalize (str);
            os_free (str);
            if (dir == NULL) {
                res = os_resultFail;
            }
        } else {
            dir = NULL;
            res = os_resultFail;
        }

        if (res != os_resultFail) {
            file = file_name;
            len = strlen (dir);
            if (strncmp (dir, file_name, len) == 0) {
                file = file_name + len;
            }
            os_free (dir);

            fmt = "%255[^:]:%hu";
            ret = sscanf (file, fmt, host, &port);
            file = NULL;

            if (res == os_resultSuccess) {
                if (ret >= 2) {
                    logfile = open_socket (host, port);
                } else {
                    logfile = fopen (file_name, "a");
                }
            }
        }
    }

    return logfile;
}

static void os_close_file (char * file_name, FILE *file)
{
    if (strcmp(file_name, "<stderr>") != 0 && strcmp(file_name, "<stdout>") != 0)
    {
        fclose(file);
    }
}

static os_result
os_configIsTrue(
                const char* configString,
                bool* resultOut)
{
    os_result result = os_resultSuccess;

    if (os_strcasecmp(configString, "FALSE") == 0 ||
        os_strcasecmp(configString, "0")     == 0 ||
        os_strcasecmp(configString, "NO")    == 0)
    {
        *resultOut = false;
    }
    else
    {
        if (os_strcasecmp(configString, "TRUE") == 0 ||
            os_strcasecmp(configString, "1")    == 0 ||
            os_strcasecmp(configString, "YES")  == 0)
        {
            *resultOut = true;
        }
        else
        {
            result = os_resultFail;
        }
    }
    return result;
}

/**
 * Read environment properties. In particular ones that can't be left until
 * there is a requirement to log.
 */
void
os_reportInit(bool forceReInit)
{
    static bool doneOnce = false;
    char *envValue;

    if (!doneOnce || forceReInit)
    {
        if (!doneOnce)
        {
            os_mutexInit(&reportMutex);
            os_mutexInit(&reportPluginMutex);
        }

        doneOnce = true;
        envValue = os_getenv(os_env_verbosity);
        if (envValue != NULL)
        {
            if (os_reportSetVerbosity(envValue) == os_resultFail)
            {
                OS_REPORT_WARNING("os_reportInit", 0,
                        "Cannot parse report verbosity %s value \"%s\","
                        " reporting verbosity remains %s", os_env_verbosity, envValue, os_reportTypeText[os_reportVerbosity]);
            }
        }

        envValue = os_getenv(os_env_append);
        if (envValue != NULL)
        {
            bool shouldAppend;
            if (os_configIsTrue(envValue, &shouldAppend) == os_resultFail)
            {
                OS_REPORT_WARNING("os_reportInit", 0,
                        "Cannot parse report %s value \"%s\","
                        " reporting append mode unchanged", os_env_append, envValue);
            }
            else
            {
                /* Remove log files when not appending. */
                if (!shouldAppend) {
                    os_reportRemoveStaleLogs();
                }
            }
        }
    }
    inited = true;
}

void os_reportExit()
{
    char *name;
    os_reportStack reports;

    reports = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if (reports) {
        os_report_dumpStack(OS_FUNCTION, __FILE__, __LINE__);
        os_iterFree(reports->reports, NULL);
        os_threadMemFree(OS_THREAD_REPORT_STACK);
    }
    inited = false;
    os_mutexDestroy(&reportMutex);

    os_mutexDestroy(&reportPluginMutex);

    if (error_log)
    {
        name = os_reportGetErrorFileName();
        os_close_file(name, error_log);
        os_free (name);
        error_log = NULL;
    }

    if (info_log)
    {
        name = os_reportGetInfoFileName();
        os_close_file(name, info_log);
        os_free (name);
        info_log = NULL;
    }
}

static bool os_report_is_local_file(char * file_name, char **new_name)
{
    char host[256];
    char server_file[256];
    unsigned short port;

    *new_name = file_name;
    return ((sscanf (*new_name, "%255[^:]:%hu", host, &port) != 2
            && sscanf (*new_name, "%255[^:]:%hu:%255[^:]", host, &port, server_file) != 3) ? true : false);
}

#define MAX_FILE_PATH 2048
static char *os_report_createFileNormalize(char *file_path, char *file_dir, char *file_name)
{
    int len;

    len = snprintf(file_path, MAX_FILE_PATH, "%s/%s", file_dir, file_name);
    /* Note bug in glibc < 2.0.6 returns -1 for output truncated */
    if ( len < MAX_FILE_PATH && len > -1 ) {
        return (os_fileNormalize(file_path));
    } else {
        return (file_name);
    }
}

/**
 * Return either a log file path string or a pseudo file name/path value
 * like <stdout> or <stderr>.
 * The result of os_report_file_path must be freed with os_free
 * @param override_variable An environment variable name that may hold a filename
 * or pseudo filename. If this var is set, and is not a pseudo filename,
 * the value of this var will be added to the value of env variable
 * OSPL_LOGPATH (if set or './' if not) to create the log file path.
 * @param default_file If override_variable is not defined in the environment
 * this is the filename used.
 */
static char *
os_report_file_path(char * default_file, char * override_variable, enum os_report_logType type)
{
    char *file_dir;
    char file_path[MAX_FILE_PATH];
    char *file_name = NULL;
    char *full_file_path = NULL;
    FILE *logfile = NULL;
    char *override = NULL;

    if (override_variable != NULL)
    {
        override = os_getenv(override_variable);
        file_name = override;
    }
    if (!file_name)
    {
        file_name = default_file;
    }

    file_dir = os_getenv(os_env_logdir);
    if (!file_dir)
    {
        file_dir = (char *)os_default_logdir;
    }
    else
    {
        /* We just need to check if a file can be written to the directory, we just use the
         * default info file as there will always be one created, if we used the variables
         * passed in we would create an empty error log (which is bad for testing) and we
         * cannot delete it as we open the file with append
         */
        if (type == OS_REPORT_INFO)
        {
            full_file_path = (char*) os_malloc(strlen(file_dir) + 1 + strlen(file_name) + 1 );
            strcpy(full_file_path, file_dir);
            strcat(full_file_path, "/");
            strcat(full_file_path, file_name);
            if (os_report_is_local_file(full_file_path,&full_file_path))
            {
                logfile = fopen (full_file_path, "a");
                if (logfile)
                {
                    fclose(logfile);
                }
            }
        }
        os_free (full_file_path);
    }
    if (strcmp(file_name, "<stderr>") != 0 && strcmp(file_name, "<stdout>") != 0)
    {
        if (os_report_is_local_file(file_name, &file_name))
        {
            return (os_report_createFileNormalize(file_path, file_dir, file_name));
        }
    }
    return os_strdup (file_name);
}

/**
 * Get the destination for logging error reports. Env property OSPL_INFOFILE and
 * OSPL_LOGPATH controls this value.
 * If OSPL_INFOFILE is not set & this process is an OpenSplice service default
 * to logging to a file named ospl-info.log, otherwise
 * use standard out.
 * @see os_report_file_path
 */
char *
os_reportGetInfoFileName()
{
    char* file_name;
    os_reportInit(false);
    file_name = os_report_file_path (os_report_defaultInfoFileName, (char *)os_env_infofile, OS_REPORT_INFO);
    /* @todo dds2881 - Uncomment below & remove above to enable application default error logging to stderr */
    /* file_name = os_report_file_path (os_procIsOpenSpliceService() ? "ospl-info.log" : "<stdout>", os_env_infofile);*/
    return file_name;
}

/**
 * Get the destination for logging error reports. Env property OSPL_ERRORFILE and
 * OSPL_LOGPATH controls this value.
 * If OSPL_ERRORFILE is not set & this process is an OpenSplice service default
 * to logging to a file named ospl-error.log, otherwise
 * use standard error.
 * @see os_report_file_path
 */
char *
os_reportGetErrorFileName()
{
    char* file_name;
    os_reportInit(false);
    file_name = os_report_file_path (os_report_defaultErrorFileName, (char *)os_env_errorfile, OS_REPORT_ERROR);
    /* @todo dds2881 - Uncomment below & remove above to enable application default error logging to stderr */
    /* file_name = os_report_file_path (os_procIsOpenSpliceService() ? "ospl-error.log" : "<stderr>", os_env_errorfile); */
    return file_name;
}

static FILE *
os_open_info_file (void)
{
    char * name;
    FILE * file;

    name = os_reportGetInfoFileName();
    file = os_open_file(name);
    if (!file)
    {
        file = os_open_file("<stdout>");
    }
    os_free (name);
    return file;
}

static FILE *
os_open_error_file (void)
{
    char * name;
    FILE * file;

    name = os_reportGetErrorFileName();
    file = os_open_file(name);
    if (!file)
    {
        file = os_open_file("<stderr>");
    }
    os_free (name);
    return file;
}


void
os_reportDisplayLogLocations()
{
    char * infoFileName;
    char * errorFileName;

    infoFileName = os_reportGetInfoFileName();
    errorFileName = os_reportGetErrorFileName();
    printf ("\nInfo  log : %s\n", infoFileName);
    printf ("Error log : %s\n", errorFileName);
    os_free (infoFileName);
    os_free (errorFileName);
}

static void
os_sectionReport(
        os_reportEventV1 event,
        bool useErrorLog)
{
    os_time ostime;
    FILE *log;

    if (useErrorLog) {
        if ( error_log == NULL )
        {
            error_log = os_open_error_file();
        }
        log = error_log;
    } else {
        if ( info_log == NULL )
        {
            info_log = os_open_info_file();
        }
        log = info_log;
    }

    ostime = os_timeGet();
    os_mutexLock(&reportMutex);
    os_report_fprintf(log,
        "----------------------------------------------------------------------------------------\n"
        "Report      : %s\n"
        "Internals   : %s/%s/%d/%d/%d.%09d\n",
        event->description,
        event->reportContext,
        event->fileName,
        event->lineNo,
        event->code,
        ostime.tv_sec,
        ostime.tv_nsec);
    fflush (log);
    os_mutexUnlock(&reportMutex);
}

static void
os_headerReport(
        os_reportEventV1 event,
        bool useErrorLog)
{
    os_time ostime;
    char node[64];
    char date_time[128];
    FILE *log;

    /* Check error_file is NULL here to keep user loggging */
    /* plugin simple in integrity */
    if (useErrorLog) {
        if ( error_log == NULL )
        {
            error_log = os_open_error_file();
        }
        log = error_log;
    } else {
        if ( info_log == NULL )
        {
            info_log = os_open_info_file();
        }
        log = info_log;
    }

    ostime = os_timeGet();
    ostime = os_timeGet();
    os_ctime_r(&ostime, date_time, sizeof(date_time));

    if (os_gethostname(node, sizeof(node)-1) == os_resultSuccess)
    {
        node[sizeof(node)-1] = '\0';
    }
    else
    {
        strcpy(node, "UnkownNode");
    }

    os_mutexLock(&reportMutex);
    if (useErrorLog) {
        os_report_fprintf(log,
            "========================================================================================\n"
            "Context     : %s\n"
            "Date        : %s\n"
            "Node        : %s\n"
            "Process     : %s\n"
            "Thread      : %s\n"
            "Internals   : %s/%d/%s/%s/%s\n",
            event->description,
            date_time,
            node,
            event->processDesc,
            event->threadDesc,
            event->fileName,
            event->lineNo,
            OSPL_VERSION_STR,
            OSPL_INNER_REV_STR,
            OSPL_OUTER_REV_STR);
    } else {
        os_report_fprintf(log,
            "========================================================================================\n"
            "Report      : %s\n"
            "Context     : %s\n"
            "Date        : %s\n"
            "Node        : %s\n"
            "Process     : %s\n"
            "Thread      : %s\n"
            "Internals   : %s/%d/%s/%s/%s\n",
            os_reportTypeText[event->reportType],
            event->description,
            date_time,
            node,
            event->processDesc,
            event->threadDesc,
            event->fileName,
            event->lineNo,
            OSPL_VERSION_STR,
            OSPL_INNER_REV_STR,
            OSPL_OUTER_REV_STR);
    }
    fflush (log);
    os_mutexUnlock(&reportMutex);
}

static void
os_defaultReport(
        os_reportEventV1 event)
{
    os_time ostime;
    char node[64];
    char date_time[128];
    FILE *log;

    switch (event->reportType) {
    case OS_DEBUG:
    case OS_INFO:
    case OS_WARNING:
        /* Check info_file is NULL here to keep user loggging */
        /* plugin simple in integrity */
        if ( info_log == NULL )
        {
            info_log = os_open_info_file();
        }
        log = info_log;
        break;
    case OS_ERROR:
    case OS_CRITICAL:
    case OS_FATAL:
    case OS_REPAIRED:
    default:
        /* Check error_file is NULL here to keep user loggging */
        /* plugin simple in integrity */
        if ( error_log == NULL )
        {
            error_log = os_open_error_file();
        }
        log = error_log;
        break;
    }

    ostime = os_timeGet();
    os_ctime_r(&ostime, date_time, sizeof(date_time));
    os_gethostname(node, sizeof(node)-1);
    node[sizeof(node)-1] = '\0';

    if (os_gethostname(node, sizeof(node)-1) == os_resultSuccess)
    {
        node[sizeof(node)-1] = '\0';
    }
    else
    {
        strcpy(node, "UnkownNode");
    }

    os_mutexLock(&reportMutex);
    os_report_fprintf (log,
        "========================================================================================\n"
        "Report      : %s\n"
        "Date        : %s\n"
        "Description : %s\n"
        "Node        : %s\n"
        "Process     : %s\n"
        "Thread      : %s\n"
        "Internals   : %s/%s/%s/%s/%s/%d/%d/%d.%09d\n",
        os_reportTypeText[event->reportType],
        date_time,
        event->description,
        node,
        event->processDesc,
        event->threadDesc,
        OSPL_VERSION_STR,
        OSPL_INNER_REV_STR,
        OSPL_OUTER_REV_STR,
        event->reportContext,
        event->fileName,
        event->lineNo,
        event->code,
        ostime.tv_sec,
        ostime.tv_nsec);
    fflush (log);
    os_mutexUnlock(&reportMutex);
}

static char *os_strrchrs (const char *str, const char *chrs, bool inc)
{
    bool eq;
    char *ptr = NULL;
    size_t i, j;

    assert (str != NULL);
    assert (chrs != NULL);

    for (i = 0; str[i] != '\0'; i++) {
        for (j = 0, eq = false; chrs[j] != '\0' && eq == false; j++) {
            if (str[i] == chrs[j]) {
                eq = true;
            }
        }
        if (eq == inc) {
            ptr = (char *)str + i;
        }
    }

    return ptr;
}

void
os_report_noargs(
        os_reportType type,
        const char *context,
        const char *path,
        int32_t line,
        int32_t code,
        const char *message)
{
    char *file;
    char procid[256], thrid[64], tmp[2];
    os_reportStack stack;

    struct os_reportEventV1_s report = { OS_REPORT_EVENT_V1, /* version */
            OS_NONE, /* reportType */
            NULL, /* reportContext */
            NULL, /* fileName */
            0, /* lineNo */
            0, /* code */
            NULL, /* description */
            NULL, /* threadDesc */
            NULL /* processDesc */
    };

    if (inited == false) {
        return;
    }

    if (type < os_reportVerbosity) {
        /* This level / type of report is below the process output suppression threshold. */
        return;
    }

    if ((file = os_strrchrs (path, os_fileSep(), true)) == NULL) {
        file = (char *)path;
    } else {
        file++;
    }

    /* Only figure out process and thread identities if the user requested an
       entry in the default log file or registered a typed report plugin. */
    os_procNamePid (procid, sizeof (procid));
    os_threadFigureIdentity (thrid, sizeof (thrid));

    report.reportType = type;
    report.reportContext = (char *)context;
    report.fileName = (char *)file;
    report.lineNo = line;
    report.code = code;
    report.description = (char *)message;
    report.threadDesc = thrid;
    report.processDesc = procid;

    stack = (os_reportStack)os_threadMemGet(OS_THREAD_REPORT_STACK);
    if (stack && stack->count) {
        if (report.reportType != OS_NONE) {
            os_report_append (stack, &report);
        }

    } else {
        os_defaultReport (&report);

        if (os_report_iserror (&report)) {
            os_reportSetApiInfo (context, file, line, code, message);
        }
    }
}

void
os_report(
        os_reportType type,
        const char *context,
        const char *path,
        int32_t line,
        int32_t code,
        const char *format,
        ...)
{
    char buf[OS_REPORT_BUFLEN];
    va_list args;

    if (inited == false) {
        return;
    }

    if (type < os_reportVerbosity) {
        return;
    }

    va_start (args, format);
    (void)os_vsnprintf (buf, sizeof(buf), format, args);
    va_end (args);

    os_report_noargs (type, context, path, line, code, buf);
}

static void
os_reportResetApiInfo (
        os_reportInfo *report)
{
    assert (report != NULL);

    os_free (report->reportContext);
    os_free (report->sourceLine);
    os_free (report->description);
    (void)memset (report, 0, sizeof (os_reportInfo));
}

static char *os_strndup(const char *s, size_t max)
{
    size_t sz = strlen(s) + 1;
    char *copy;
    assert (max >= 1);
    if (sz > max) {
        sz = max;
    }
    copy = os_malloc(sz);
    memcpy(copy, s, sz);
    copy[sz-1] = 0;
    return copy;
}

static void
os_reportSetApiInfo (
        const char *context,
        const char *file,
        int32_t line,
        int32_t code,
        const char *message)
{
    const char *format = NULL;
    char point[512];
    os_reportInfo *report;

    report = (os_reportInfo *)os_threadMemGet(OS_THREAD_API_INFO);
    if (report == NULL) {
        report = (os_reportInfo *)os_threadMemMalloc(OS_THREAD_API_INFO, sizeof(os_reportInfo));
        if (report) {
            memset(report, 0, sizeof(os_reportInfo));
        }
    }
    if (report != NULL) {
        os_reportResetApiInfo (report);

        if (context != NULL) {
            report->reportContext = os_strdup (context);
        }

        if (file != NULL && line > 0) {
            format = "%s:%d";
        } else if (file != NULL) {
            format = "%s";
        } else if (line > 0) {
            file = "";
            format = "%d";
        }

        if (format != NULL) {
            (void)snprintf (point, sizeof (point), format, file, line);
            report->sourceLine = os_strdup (point);
        }

        report->reportCode = code;

        if (message != NULL) {
            report->description = os_strndup (message, OS_REPORT_BUFLEN + 1);
        }
    }
}

os_reportInfo *
os_reportGetApiInfo(void)
{
    return (os_reportInfo *)os_threadMemGet(OS_THREAD_API_INFO);
}

void
os_reportClearApiInfo(void)
{
    os_reportInfo *report;

    report = (os_reportInfo *)os_threadMemGet(OS_THREAD_API_INFO);
    if (report != NULL) {
        os_reportResetApiInfo (report);
        os_threadMemFree(OS_THREAD_API_INFO);
    }
}

/**
 * Overrides the current minimum output level to be reported from
 * this process.
 * @param newVerbosity String holding either an integer value corresponding
 * to an acceptable (in range) log verbosity or a string verbosity 'name'
 * like 'ERROR' or 'warning' or 'DEBUG' or somesuch.
 * @return os_resultFail if the string contains neither of the above;
 * os_resultSuccess otherwise.
 */
os_result
os_reportSetVerbosity(
        const char* newVerbosity)
{
    long verbosityInt;
    os_result result = os_resultFail;
    verbosityInt = strtol(newVerbosity, NULL, 0);

    os_reportInit(false);
    if (verbosityInt == 0 && strcmp("0", newVerbosity))
    {
        /* Conversion from int failed. See if it's one of the string forms. */
        while (verbosityInt < (long) (sizeof(os_reportTypeText) / sizeof(os_reportTypeText[0])))
        {
            if (os_strcasecmp(newVerbosity, os_reportTypeText[verbosityInt]) == 0)
            {
                break;
            }
            ++verbosityInt;
        }
    }
    if (verbosityInt >= 0 && verbosityInt < (long) (sizeof(os_reportTypeText) / sizeof(os_reportTypeText[0])))
    {
        /* OS_API_INFO label is kept for backwards compatibility. */
        if (OS_API_INFO == (os_reportType)verbosityInt) {
            os_reportVerbosity = OS_ERROR;
        } else {
            os_reportVerbosity = (os_reportType)verbosityInt;
        }
        result = os_resultSuccess;
    }

    return result;
}

/**
 * Remove possible existing log files to start cleanly.
 */
void
os_reportRemoveStaleLogs()
{
    static bool alreadyDeleted = false;
    char * name;

    os_reportInit(false);

    if (!alreadyDeleted) {
        /* TODO: Only a single process or spliced (as 1st process) is allowed to
         * delete the log files. */
        /* Remove ospl-info.log and ospl-error.log.
         * Ignore the result because it is possible that they don't exist yet. */
        name = os_reportGetInfoFileName();
        (void)os_remove(name);
        os_free(name);
        name = os_reportGetErrorFileName();
        (void)os_remove(name);
        os_free(name);
        alreadyDeleted = true;
    }
}

/*****************************************
 * Report-stack related functions
 *****************************************/
void
os_report_stack()
{
    os_reportStack _this;

    if (inited == false) {
        return;
    }
    _this = (os_reportStack)os_threadMemGet(OS_THREAD_REPORT_STACK);
    if (!_this) {
        /* Report stack does not exist yet, so create it */
        _this = os_threadMemMalloc(OS_THREAD_REPORT_STACK, sizeof(struct os_reportStack_s));
        if (_this) {
            _this->count = 1;
            _this->typeset = 0;
            _this->file = NULL;
            _this->lineno = 0;
            _this->signature = NULL;
            _this->reports = os_iterNew();
        } else {
            OS_REPORT_ERROR("os_report_stack", 0,
                    "Failed to initialize report stack (could not allocate thread-specific memory)");
        }
    } else {
        /* Use previously created report stack */
        if (_this->count == 0) {
            _this->file = NULL;
            _this->lineno = 0;
            _this->signature = NULL;
        }
        _this->count++;

    }
}

bool
os_report_get_context(
        const char **file,
        int *lineno,
        const char **signature)
{
    bool result = false;
    os_reportStack _this;

    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if ((_this) && (_this->count) && (_this->file)) {
        *file = _this->file;
        *lineno = _this->lineno;
        *signature = _this->signature;
        result = true;
    }
    return result;
}


void
os_report_stack_free(
        void)
{
    os_reportStack _this;
    os_reportEventV1 report;

    _this = (os_reportStack)os_threadMemGet(OS_THREAD_REPORT_STACK);
    if (_this) {
        while((report = os_iterTake(_this->reports, -1))) {
            os_report_free(report);
        }
        os_iterFree(_this->reports, NULL);
        os_threadMemFree(OS_THREAD_REPORT_STACK);
    }
}

struct os__reportBuffer {
    char *str;
    size_t len;
    size_t off;
};

static void
os_report_stack_unwind(
        os_reportStack _this,
        bool valid,
        const char *context,
        const char *path,
        const int line)
{
    static const char description[] = "Operation failed.";
    struct os_reportEventV1_s header;
    os_reportEventV1 report;
    struct os__reportBuffer buf = { NULL, 0, 0 };
    os_iter *tempList;
    char *file;
    char tmp[2];
    int32_t code = 0;
    bool update = true;
    os_reportType filter = OS_NONE;
    bool useErrorLog;
    os_reportType reportType = OS_ERROR;

    if (!valid) {
        if (OS_REPORT_IS_ALWAYS(_this->typeset)) {
            valid = true;
        } else if (OS_REPORT_IS_DEPRECATED(_this->typeset)) {
            filter = OS_API_INFO;
            reportType = OS_API_INFO;
        } else {
            filter = OS_NONE;
        }

        if (filter != OS_NONE) {
            tempList = os_iterNew();
            if (tempList != NULL) {
                while ((report = os_iterTake(_this->reports, -1))) {
                    if (report->reportType == filter) {
                        (void)os_iterAppend(tempList, report);
                    } else {
                        os_report_free(report);
                    }
                }
                while ((report = os_iterTake(tempList, -1))) {
                    os_iterAppend(_this->reports, report);
                }
                os_free(tempList);
            }
            valid = true;
        }
    }

    useErrorLog = OS_REPORT_IS_ERROR(_this->typeset);

    _this->typeset = 0;

    if (valid) {
        char proc[256], procid[256];
        char thr[64], thrid[64];
        os_procId pid;
        uintmax_t tid;

        assert (context != NULL);
        assert (path != NULL);

        if ((file = os_strrchrs (path, os_fileSep(), true)) == NULL) {
            file = (char *)path;
        } else {
            file++;
        }

        pid = os_procIdSelf ();
        tid = os_threadIdToInteger (os_threadIdSelf ());

        os_procNamePid (procid, sizeof (procid));
        os_procName (proc, sizeof (proc));
        os_threadFigureIdentity (thrid, sizeof (thrid));
        os_threadGetThreadName (thr, sizeof (thr));

        header.reportType = reportType;
        header.description = (char *)context;
        header.processDesc = procid;
        header.threadDesc = thrid;
        header.fileName = file;
        header.lineNo = line;

        os_headerReport (&header, useErrorLog);
    }

    while ((report = os_iterTake(_this->reports, -1))) {
        if (valid) {
            if (update && os_report_iserror (report)) {
                os_reportSetApiInfo (
                        report->reportContext,
                        report->fileName,
                        report->lineNo,
                        report->code,
                        report->description);
                update = false;
            }
            os_sectionReport (report, useErrorLog);
        }
        os_report_free(report);
    }

    os_free (buf.str);
}

void
os_report_dumpStack(
        const char *context,
        const char *path,
        const int line)
{
    os_reportStack _this;

    if (inited == false) {
        return;
    }
    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if ((_this) && (_this->count > 0)) {
        os_report_stack_unwind(_this, true, context, path, line);
    }
}

bool
os_report_flush_required(void)
{
    os_reportStack _this;
    bool flush = false;

    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if (_this) {
        if (OS_REPORT_IS_ALWAYS(_this->typeset)     ||
                OS_REPORT_IS_WARNING(_this->typeset)    ||
                OS_REPORT_IS_DEPRECATED(_this->typeset)) {
            flush = true;
        }
    }

    return flush;
}

void
os_report_flush(
        bool valid,
        const char *context,
        const char *path,
        const int line)
{
    os_reportStack _this;

    if (inited == false) {
        return;
    }
    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if ((_this) && (_this->count)) {
        if (_this->count == 1) {
            os_report_stack_unwind(_this, valid, context, path, line);
            _this->file = NULL;
            _this->signature = NULL;
            _this->lineno = 0;
        }
        _this->count--;
    }
}

void
os_report_flush_context(
        bool valid,
        os_report_context_callback callback,
        void *arg)
{
    os_reportStack _this;
    char buffer[1024];
    const char *context = NULL;

    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if ((_this) && (_this->count)) {
        if (_this->count == 1) {
            if ((char *)callback) {
                context = callback(_this->signature, buffer, sizeof(buffer), arg);
            }
            if (context == NULL) {
                context = _this->signature;
            }
            os_report_stack_unwind(_this, valid, context, _this->file, _this->lineno);
            _this->file = NULL;
            _this->signature = NULL;
            _this->lineno = 0;
        }
        _this->count--;

    }
}

void
os_report_flush_context_unconditional(
        os_report_context_callback callback,
        void *arg)
{
    os_reportStack _this;
    char buffer[1024];
    const char *context = NULL;

    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if ((_this) && (_this->count)) {
        if ((char *)callback) {
            context = callback(_this->signature, buffer, sizeof(buffer), arg);
        }
        if (context == NULL) {
            context = _this->signature;
        }
        os_report_stack_unwind(_this, true, context, _this->file, _this->lineno);
        _this->file = NULL;
        _this->signature = NULL;
        _this->lineno = 0;
        _this->count = 0;
    }
}

void
os_report_flush_unconditional(
        bool valid,
        const char *context,
        const char *path,
        const int line)
{
    os_reportStack _this;

    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if ((_this) && (_this->count)) {
        os_report_stack_unwind(_this, valid, context, path, line);
        _this->file = NULL;
        _this->signature = NULL;
        _this->lineno = 0;
        _this->count = 0;
    }
}

os_reportEventV1
os_report_read(
        int32_t index)
{
    os_reportEventV1 report = NULL;
    os_reportStack _this;

    _this = os_threadMemGet(OS_THREAD_REPORT_STACK);
    if (_this) {
        if (index < 0) {
            report = NULL;
        } else {
            report = (os_reportEventV1)os_iterObject(_this->reports, (uint32_t) index);
        }
    } else {
        OS_REPORT_ERROR("os_report_read", 0,
                "Failed to retrieve report administration from thread-specific memory");
    }
    return report;
}

#define OS__STRDUP(str) (str != NULL ? os_strdup(str) : os_strdup("NULL"))

void
os_report_append(
        os_reportStack _this,
        const os_reportEventV1 report)
{
    os_reportEventV1 copy;
    assert(report);

    copy = os_malloc(sizeof(*copy));
    if (copy) {
        copy->code = report->code;
        copy->description = OS__STRDUP(report->description);
        copy->fileName = OS__STRDUP(report->fileName);
        copy->lineNo = report->lineNo;
        copy->processDesc = OS__STRDUP(report->processDesc);
        copy->reportContext = OS__STRDUP(report->reportContext);
        copy->reportType = report->reportType;
        copy->threadDesc = OS__STRDUP(report->threadDesc);
        copy->version = report->version;
        _this->typeset |= OS_REPORT_TYPE_FLAG(report->reportType);
        os_iterAppend(_this->reports, copy);
    } else {
        os_report_fprintf(stderr, "Failed to allocate %d bytes for log report!", (int)sizeof(*copy));
        os_report_fprintf(stderr, "Report: %s\n", report->description);
    }
}

void
os_report_free(os_reportEventV1 report)
{
    os_free(report->description);
    os_free(report->fileName);
    os_free(report->processDesc);
    os_free(report->reportContext);
    os_free(report->threadDesc);
    os_free(report);
}

static int
os_report_fprintf(FILE *file,
        const char *format,
        ...)
{
    int BytesWritten = 0;
    va_list args;
    va_start(args, format);
    BytesWritten = os_vfprintfnosigpipe(file, format, args);
    va_end(args);
    if (BytesWritten == -1) {
        /* error occured ?, try to write to stdout. (also with no sigpipe,
         * stdout can also give broken pipe)
         */
        va_start(args, format);
        (void) os_vfprintfnosigpipe(stdout, format, args);
        va_end(args);
    }
    return BytesWritten;
}
