/*
 * Copyright 1998-2017 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include "internal/cryptlib_int.h"
#include <openssl/safestack.h>

#if     defined(__i386)   || defined(__i386__)   || defined(_M_IX86) || \
        defined(__x86_64) || defined(__x86_64__) || \
        defined(_M_AMD64) || defined(_M_X64)

extern unsigned int OPENSSL_ia32cap_P[4];

# if defined(OPENSSL_CPUID_OBJ) && !defined(OPENSSL_NO_ASM) && !defined(I386_ONLY)
#include <stdio.h>
#  define OPENSSL_CPUID_SETUP
typedef uint64_t IA32CAP;
void OPENSSL_cpuid_setup(void)
{
    static int trigger = 0;
    IA32CAP OPENSSL_ia32_cpuid(unsigned int *);
    IA32CAP vec;
    char *env;

    if (trigger)
        return;

    trigger = 1;
    if ((env = getenv("OPENSSL_ia32cap"))) {
        int off = (env[0] == '~') ? 1 : 0;
#  if defined(_WIN32)
        if (!sscanf(env + off, "%I64i", &vec))
            vec = strtoul(env + off, NULL, 0);
#  else
        if (!sscanf(env + off, "%lli", (long long *)&vec))
            vec = strtoul(env + off, NULL, 0);
#  endif
        if (off)
            vec = OPENSSL_ia32_cpuid(OPENSSL_ia32cap_P) & ~vec;
        else if (env[0] == ':')
            vec = OPENSSL_ia32_cpuid(OPENSSL_ia32cap_P);

        OPENSSL_ia32cap_P[2] = 0;
        if ((env = strchr(env, ':'))) {
            unsigned int vecx;
            env++;
            off = (env[0] == '~') ? 1 : 0;
            vecx = strtoul(env + off, NULL, 0);
            if (off)
                OPENSSL_ia32cap_P[2] &= ~vecx;
            else
                OPENSSL_ia32cap_P[2] = vecx;
        }
    } else
        vec = OPENSSL_ia32_cpuid(OPENSSL_ia32cap_P);

    /*
     * |(1<<10) sets a reserved bit to signal that variable
     * was initialized already... This is to avoid interference
     * with cpuid snippets in ELF .init segment.
     */
    OPENSSL_ia32cap_P[0] = (unsigned int)vec | (1 << 10);
    OPENSSL_ia32cap_P[1] = (unsigned int)(vec >> 32);
}
# else
unsigned int OPENSSL_ia32cap_P[4];
# endif
#endif
int OPENSSL_NONPIC_relocated = 0;
#if !defined(OPENSSL_CPUID_SETUP) && !defined(OPENSSL_CPUID_OBJ)
void OPENSSL_cpuid_setup(void)
{
}
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
# include <tchar.h>
# include <signal.h>
# ifdef __WATCOMC__
#  if defined(_UNICODE) || defined(__UNICODE__)
#   define _vsntprintf _vsnwprintf
#  else
#   define _vsntprintf _vsnprintf
#  endif
# endif
# ifdef _MSC_VER
#  define alloca _alloca
# endif

# if defined(_WIN32_WINNT) && _WIN32_WINNT>=0x0333
int OPENSSL_isservice(void)
{
    HWINSTA h;
    DWORD len;
    WCHAR *name;
    static union {
        void *p;
        FARPROC f;
    } _OPENSSL_isservice = {
        NULL
    };

    if (_OPENSSL_isservice.p == NULL) {
        HANDLE mod = GetModuleHandle(NULL);
        if (mod != NULL)
            _OPENSSL_isservice.f = GetProcAddress(mod, "_OPENSSL_isservice");
        if (_OPENSSL_isservice.p == NULL)
            _OPENSSL_isservice.p = (void *)-1;
    }

    if (_OPENSSL_isservice.p != (void *)-1)
        return (*_OPENSSL_isservice.f) ();

    h = GetProcessWindowStation();
    if (h == NULL)
        return -1;

    if (GetUserObjectInformationW(h, UOI_NAME, NULL, 0, &len) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return -1;

    if (len > 512)
        return -1;              /* paranoia */
    len++, len &= ~1;           /* paranoia */
    name = (WCHAR *)alloca(len + sizeof(WCHAR));
    if (!GetUserObjectInformationW(h, UOI_NAME, name, len, &len))
        return -1;

    len++, len &= ~1;           /* paranoia */
    name[len / sizeof(WCHAR)] = L'\0'; /* paranoia */
#  if 1
    /*
     * This doesn't cover "interactive" services [working with real
     * WinSta0's] nor programs started non-interactively by Task Scheduler
     * [those are working with SAWinSta].
     */
    if (wcsstr(name, L"Service-0x"))
        return 1;
#  else
    /* This covers all non-interactive programs such as services. */
    if (!wcsstr(name, L"WinSta0"))
        return 1;
#  endif
    else
        return 0;
}
# else
int OPENSSL_isservice(void)
{
    return 0;
}
# endif

void OPENSSL_showfatal(const char *fmta, ...)
{
    va_list ap;
    TCHAR buf[256];
    const TCHAR *fmt;
    /*
     * First check if it's a console application, in which case the
     * error message would be printed to standard error.
     * Windows CE does not have a concept of a console application,
     * so we need to guard the check.
     */
# ifdef STD_ERROR_HANDLE
    HANDLE h;

    if ((h = GetStdHandle(STD_ERROR_HANDLE)) != NULL &&
        GetFileType(h) != FILE_TYPE_UNKNOWN) {
        /* must be console application */
        int len;
        DWORD out;

        va_start(ap, fmta);
        len = _vsnprintf((char *)buf, sizeof(buf), fmta, ap);
        WriteFile(h, buf, len < 0 ? sizeof(buf) : (DWORD) len, &out, NULL);
        va_end(ap);
        return;
    }
# endif

    if (sizeof(TCHAR) == sizeof(char))
        fmt = (const TCHAR *)fmta;
    else
        do {
            int keepgoing;
            size_t len_0 = strlen(fmta) + 1, i;
            WCHAR *fmtw;

            fmtw = (WCHAR *)alloca(len_0 * sizeof(WCHAR));
            if (fmtw == NULL) {
                fmt = (const TCHAR *)L"no stack?";
                break;
            }
            if (!MultiByteToWideChar(CP_ACP, 0, fmta, len_0, fmtw, len_0))
                for (i = 0; i < len_0; i++)
                    fmtw[i] = (WCHAR)fmta[i];
            for (i = 0; i < len_0; i++) {
                if (fmtw[i] == L'%')
                    do {
                        keepgoing = 0;
                        switch (fmtw[i + 1]) {
                        case L'0':
                        case L'1':
                        case L'2':
                        case L'3':
                        case L'4':
                        case L'5':
                        case L'6':
                        case L'7':
                        case L'8':
                        case L'9':
                        case L'.':
                        case L'*':
                        case L'-':
                            i++;
                            keepgoing = 1;
                            break;
                        case L's':
                            fmtw[i + 1] = L'S';
                            break;
                        case L'S':
                            fmtw[i + 1] = L's';
                            break;
                        case L'c':
                            fmtw[i + 1] = L'C';
                            break;
                        case L'C':
                            fmtw[i + 1] = L'c';
                            break;
                        }
                    } while (keepgoing);
            }
            fmt = (const TCHAR *)fmtw;
        } while (0);

    va_start(ap, fmta);
    _vsntprintf(buf, OSSL_NELEM(buf) - 1, fmt, ap);
    buf[OSSL_NELEM(buf) - 1] = _T('\0');
    va_end(ap);

# if defined(_WIN32_WINNT) && _WIN32_WINNT>=0x0333
    /* this -------------v--- guards NT-specific calls */
    if (check_winnt() && OPENSSL_isservice() > 0) {
        HANDLE hEventLog = RegisterEventSource(NULL, _T("OpenSSL"));

        if (hEventLog != NULL) {
            const TCHAR *pmsg = buf;

            if (!ReportEvent(hEventLog, EVENTLOG_ERROR_TYPE, 0, 0, NULL,
                             1, 0, &pmsg, NULL)) {
#if defined(DEBUG)
                /*
                 * We are in a situation where we tried to report a critical
                 * error and this failed for some reason. As a last resort,
                 * in debug builds, send output to the debugger or any other
                 * tool like DebugView which can monitor the output.
                 */
                OutputDebugString(pmsg);
#endif
            }

            (void)DeregisterEventSource(hEventLog);
        }
    } else
# endif
        MessageBox(NULL, buf, _T("OpenSSL: FATAL"), MB_OK | MB_ICONERROR);
}
#else
void OPENSSL_showfatal(const char *fmta, ...)
{
#ifndef OPENSSL_NO_STDIO
    va_list ap;

    va_start(ap, fmta);
    vfprintf(stderr, fmta, ap);
    va_end(ap);
#endif
}

int OPENSSL_isservice(void)
{
    return 0;
}
#endif

void OPENSSL_die(const char *message, const char *file, int line)
{
    OPENSSL_showfatal("%s:%d: OpenSSL internal error: %s\n",
                      file, line, message);
#if !defined(_WIN32) || defined(__CYGWIN__)
    abort();
#else
    /*
     * Win32 abort() customarily shows a dialog, but we just did that...
     */
# if !defined(_WIN32_WCE)
    raise(SIGABRT);
# endif
    _exit(3);
#endif
}

#if !defined(OPENSSL_CPUID_OBJ)
/*
 * The volatile is used to to ensure that the compiler generates code that reads
 * all values from the array and doesn't try to optimize this away. The standard
 * doesn't actually require this behavior if the original data pointed to is
 * not volatile, but compilers do this in practice anyway.
 *
 * There are also assembler versions of this function.
 */
# undef CRYPTO_memcmp
int CRYPTO_memcmp(const void * in_a, const void * in_b, size_t len)
{
    size_t i;
    const volatile unsigned char *a = in_a;
    const volatile unsigned char *b = in_b;
    unsigned char x = 0;

    for (i = 0; i < len; i++)
        x |= a[i] ^ b[i];

    return x;
}
#endif
