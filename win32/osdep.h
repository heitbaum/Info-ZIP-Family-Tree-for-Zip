/*
  win32/osdep.h

  Copyright (c) 1990-2004 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2003-May-08 or later
  (the contents of which are also included in zip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/* Automatic setting of the common Microsoft C idenfifier MSC.
 * NOTE: Watcom also defines M_I*86 !
 */
#if defined(_MSC_VER) || (defined(M_I86) && !defined(__WATCOMC__))
#  ifndef MSC
#    define MSC                 /* This should work for older MSC, too!  */
#  endif
#endif

#if defined(__WATCOMC__) && defined(__386__)
#  define WATCOMC_386
#endif

#if (defined(__CYGWIN32__) && !defined(__CYGWIN__))
#  define __CYGWIN__            /* compatibility for CygWin B19 and older */
#endif

/* enable multibyte character set support by default */
#ifndef _MBCS
#  define _MBCS
#endif
#if defined(__CYGWIN__)
#  undef _MBCS
#endif

#ifndef MSDOS
/*
 * Windows 95 (and Windows NT) file systems are (to some extend)
 * extensions of MSDOS. Common features include for example:
 *      FAT or (FAT like) file systems,
 *      '\\' as directory separator in paths,
 *      "\r\n" as record (line) terminator in text files, ...
 */
#  define MSDOS
/* inherit MS-DOS file system etc. stuff */
#endif

#define USE_CASE_MAP
#define PROCNAME(n) (action == ADD || action == UPDATE ? wild(n) : \
                     procname(n, 1))
#define BROKEN_FSEEK
#ifndef __RSXNT__
#  define HAVE_FSEEKABLE
#endif


/* Large File Support
 *
 *  If this is set it is assumed that the port
 *  supports 64-bit file calls.  The types are
 *  defined here.  Any local implementations are
 *  in Win32.c and the protypes for the calls are
 *  in tailor.h.  Note that a port must support
 *  these calls fully or should not set 
 *  LARGE_FILE_SUPPORT.
 */

/* If port has LARGE_FILE_SUPPORT then define here
   to make automatic unless overridden */

/* MS C and VC */
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
# ifndef LARGE_FILE_SUPPORT
#   ifndef NO_LARGE_FILE_SUPPORT
#     define LARGE_FILE_SUPPORT
#   endif
# endif
#endif


#ifdef LARGE_FILE_SUPPORT
  /* 64-bit Large File Support */

  /* Only types and the printf format stuff go here.  Functions
     go in tailor.h until ANSI prototypes are required and OF define
     can go. */

# if (defined(__GNUC__) || defined(ULONG_LONG_MAX))
    /* GNU C */

    /* base type for file offsets and file sizes */
    typedef long long    zoff_t;

    /* 64-bit stat struct */
    typedef struct stat z_stat;

    /* printf format size prefix for zoff_t values */
#   define ZOFF_T_FORMAT_SIZE_PREFIX "ll"

    /* 2004-12-01 SMS. Fancy zofft() macros, et c.*/
    /* printf format size prefix for zoff_t values */
#   define FZOFFT_FMT "ll"
#   define FZOFFT_HEX_WID_VALUE "16"

# elif (defined(__WATCOMC__) && (__WATCOMC__ >= 1100))
    /* WATCOM C */

    /* base type for file offsets and file sizes */
    typedef __int64      zoff_t;

    /* 64-bit stat struct */
    typedef struct stat z_stat;

    /* printf format size prefix for zoff_t values */
#   define ZOFF_T_FORMAT_SIZE_PREFIX "ll"

    /* 2004-12-01 SMS. Fancy zofft() macros, et c.*/
    /* printf format size prefix for zoff_t values */
#   define FZOFFT_FMT "ll"
#   define FZOFFT_HEX_WID_VALUE "16"

# elif (defined(_MSC_VER) && (_MSC_VER >= 1100)) || defined(__MINGW32__)
    /* MS C and VC */

    /* base type for file offsets and file sizes */
    typedef __int64      zoff_t;

    /* 64-bit stat struct */
    typedef struct _stati64 z_stat;

    /* printf format size prefix for zoff_t values */
#   define ZOFF_T_FORMAT_SIZE_PREFIX "I64"

    /* 2004-12-01 SMS. Fancy zofft() macros, et c.*/
    /* printf format size prefix for zoff_t values */
#   define FZOFFT_FMT "I64"
#   define FZOFFT_HEX_WID_VALUE "16"

# elif (defined(__IBMC__) && (__IBMC__ >= 350))
    /* IBM C */

    /* base type for file offsets and file sizes */
    typedef __int64              zoff_t;

    /* 64-bit stat struct */

    /* printf format size prefix for zoff_t values */
#   define ZOFF_T_FORMAT_SIZE_PREFIX "I64"

    /* 2004-12-01 SMS. Fancy zofft() macros, et c.*/
    /* printf format size prefix for zoff_t values */
#   define FZOFFT_FMT "I64"
#   define FZOFFT_HEX_WID_VALUE "16"

# else
#   undef LARGE_FILE_SUPPORT
# endif

#endif


/* Automatically set ZIP64_SUPPORT if supported */

/* MS C and VC */
#if defined(_MSC_VER) || defined(__MINGW32__)
# ifdef LARGE_FILE_SUPPORT
#   ifndef NO_ZIP64_SUPPORT
#     ifndef ZIP64_SUPPORT
#       define ZIP64_SUPPORT
#     endif
#   endif
# endif
#endif


#ifndef LARGE_FILE_SUPPORT
  /* No Large File Support */

  /* base type for file offsets and file sizes */
  typedef long zoff_t;

  /* stat struct */
  typedef struct stat z_stat;

# ifndef ZOFF_T_FORMAT_SIZE_PREFIX
#   define ZOFF_T_FORMAT_SIZE_PREFIX "l"
# endif

    /* 2004-12-01 SMS. Fancy zofft() macros, et c.*/
    /* printf format size prefix for zoff_t values */
#  define FZOFFT_FMT "l"
#  define FZOFFT_HEX_WID_VALUE "8"

#endif


/* File operations--use "b" for binary if allowed or fixed length 512 on VMS
 *                  use "S" for sequential access on NT to prevent the NT
 *                  file cache eating up memory with large .zip files
 */
#define FOPR "rb"
#define FOPM "r+b"
#define FOPW "wbS"

#if (defined(__CYGWIN__) && !defined(NO_MKTIME))
#  define NO_MKTIME             /* Cygnus' mktime() implementation is buggy */
#endif
#if (!defined(NT_TZBUG_WORKAROUND) && !defined(NO_NT_TZBUG_WORKAROUND))
#  define NT_TZBUG_WORKAROUND
#endif
#if (defined(UTIL) && defined(NT_TZBUG_WORKAROUND))
#  undef NT_TZBUG_WORKAROUND    /* the Zip utilities do not use time-stamps */
#endif
#if !defined(NO_EF_UT_TIME) && !defined(USE_EF_UT_TIME)
#  define USE_EF_UT_TIME
#endif
#if (!defined(NO_NTSD_EAS) && !defined(NTSD_EAS))
#  define NTSD_EAS
#endif

#if (defined(NTSD_EAS) && !defined(ZP_NEED_MEMCOMPR))
#  define ZP_NEED_MEMCOMPR
#endif

#ifdef WINDLL
# ifndef NO_ASM
#   define NO_ASM
# endif
# ifndef MSWIN
#   define MSWIN
# endif
# ifndef REENTRANT
#   define REENTRANT
# endif
#endif /* WINDLL */

/* Enable use of optimized x86 assembler version of longest_match() for
   MSDOS, WIN32 and OS2 per default.  */
#if !defined(NO_ASM) && !defined(ASMV)
#  define ASMV
#endif

#if !defined(__GO32__) && !defined(__EMX__) && !defined(__CYGWIN__)
#  define NO_UNISTD_H
#endif

/* Microsoft C requires additional attributes attached to all RTL function
 * declarations when linking against the CRTL dll.
 */
#ifdef MSC
#  ifdef IZ_IMP
#    undef IZ_IMP
#  endif
#  define IZ_IMP _CRTIMP
#else
# ifndef IZ_IMP
#   define IZ_IMP
# endif
#endif

/* the following definitions are considered as "obsolete" by Microsoft and
 * might be missing in some versions of <windows.h>
 */
#ifndef AnsiToOem
#  define AnsiToOem CharToOemA
#endif
#ifndef OemToAnsi
#  define OemToAnsi OemToCharA
#endif

#if (defined(__RSXNT__) && defined(__CRTRSXNT__))
#  include <crtrsxnt.h>
#endif

/* Get types and stat */
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#ifdef _MBCS
#  if (!defined(__EMX__) && !defined(__MINGW32__) && !defined(__CYGWIN__))
#    include <stdlib.h>
#    include <mbstring.h>
#  endif
#  if (defined(__MINGW32__) && !defined(MB_CUR_MAX))
#    ifdef __MSVCRT__
       IZ_IMP extern int *__p___mb_cur_max(void);
#      define MB_CUR_MAX (*__p___mb_cur_max())
#    else
       IZ_IMP extern int *_imp____mb_cur_max_dll;
#      define MB_CUR_MAX (*_imp____mb_cur_max_dll)
#    endif
#  endif
#  if (defined(__LCC__) && !defined(MB_CUR_MAX))
     IZ_IMP extern int *_imp____mb_cur_max;
#    define MB_CUR_MAX (*_imp____mb_cur_max)
#  endif
#endif

#ifdef __LCC__
#  include <time.h>
#  ifndef tzset
#    define tzset _tzset
#  endif
#  ifndef utime
#    define utime _utime
#  endif
#endif
#ifdef __MINGW32__
   IZ_IMP extern void _tzset(void);     /* this is missing in <time.h> */
#  ifndef tzset
#    define tzset _tzset
#  endif
#endif
#if (defined(__RSXNT__) || defined(__EMX__)) && !defined(tzset)
#  define tzset _tzset
#endif
#ifdef W32_USE_IZ_TIMEZONE
#  ifdef __BORLANDC__
#    define tzname tzname
#    define IZTZ_DEFINESTDGLOBALS
#  endif
#  ifndef tzset
#    define tzset _tzset
#  endif
#  ifndef timezone
#    define timezone _timezone
#  endif
#  ifndef daylight
#    define daylight _daylight
#  endif
#  ifndef tzname
#    define tzname _tzname
#  endif
#  if (!defined(NEED__ISINDST) && !defined(__BORLANDC__))
#    define NEED__ISINDST
#  endif
#  ifdef IZTZ_GETLOCALETZINFO
#    undef IZTZ_GETLOCALETZINFO
#  endif
#  define IZTZ_GETLOCALETZINFO GetPlatformLocalTimezone
#endif /* W32_USE_IZ_TIMEZONE */

#ifdef MATCH
#  undef MATCH
#endif
#define MATCH dosmatch          /* use DOS style wildcard matching */

#ifdef ZCRYPT_INTERNAL
#  ifdef WINDLL
#    define ZCR_SEED2     (unsigned)3141592654L /* use PI as seed pattern */
#  else
#    include <process.h>        /* getpid() declaration for srand seed */
#  endif
#endif

/* Up to now, all versions of Microsoft C runtime libraries lack the support
 * for customized (non-US) switching rules between daylight saving time and
 * standard time in the TZ environment variable string.
 * But non-US timezone rules are correctly supported when timezone information
 * is read from the OS system settings in the Win32 registry.
 * The following work-around deletes any TZ environment setting from
 * the process environment.  This results in a fallback of the RTL time
 * handling code to the (correctly interpretable) OS system settings, read
 * from the registry.
 */
#ifdef USE_EF_UT_TIME
# if (defined(__WATCOMC__) || defined(W32_USE_IZ_TIMEZONE))
#   define iz_w32_prepareTZenv()
# else
#   define iz_w32_prepareTZenv()        putenv("TZ=")
# endif
#endif

/* This patch of stat() is useful for at least three compilers.  It is   */
/* difficult to take a stat() of a root directory under Windows95, so  */
/* zstat_zipwin32() detects that case and fills in suitable values.    */
#ifndef __RSXNT__
#  ifndef W32_STATROOT_FIX
#    define W32_STATROOT_FIX
#  endif
#endif /* !__RSXNT__ */

#if (defined(NT_TZBUG_WORKAROUND) || defined(W32_STATROOT_FIX))
#  define W32_STAT_BANDAID
#  ifdef LARGE_FILE_SUPPORT         /* E. Gordon 9/12/03 */
   int zstat_zipwin32(const char *path, z_stat *buf);
#  else
   int zstat_zipwin32(const char *path, struct stat *buf);
#  endif
#  ifdef SSTAT
#    undef SSTAT
#  endif
#  define SSTAT zstat_zipwin32
#endif /* NT_TZBUG_WORKAROUND || W32_STATROOT_FIX */

int getch_win32(void);

#ifdef __GNUC__
# define IZ_PACKED      __attribute__((packed))
#else
# define IZ_PACKED
#endif

/* for some (all ?) versions of IBM C Set/2 and IBM C Set++ */
#ifndef S_IFMT
#  define S_IFMT 0xF000
#endif /* !S_IFMT */

#ifdef __WATCOMC__
#  include <stdio.h>    /* PATH_MAX is defined here */
#  define NO_MKTEMP

/* Get asm routines to link properly without using "__cdecl": */
#  ifdef __386__
#    ifdef ASMV
#      pragma aux match_init    "_*" parm caller [] modify []
#      pragma aux longest_match "_*" parm caller [] value [eax] \
                                      modify [eax ecx edx]
#    endif
#    if defined(ASM_CRC) && !defined(USE_ZLIB)
#      pragma aux crc32         "_*" parm caller [] value [eax] modify [eax]
#      pragma aux get_crc_table "_*" parm caller [] value [eax] \
                                      modify [eax ecx edx]
#    endif /* ASM_CRC && !USE_ZLIB */
#  endif /* __386__ */
#endif /* __WATCOMC__ */

