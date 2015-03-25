/*
  zip.c - Zip 3

  Copyright (c) 1990-2015 Info-ZIP.  All rights reserved.

  See the accompanying file LICENSE, version 2009-Jan-2 or later
  (the contents of which are also included in zip.h) for terms of use.
  If, for some reason, all these files are missing, the Info-ZIP license
  also may be found at:  ftp://ftp.info-zip.org/pub/infozip/license.html
*/
/*
 *  zip.c by Mark Adler.
 */
#define __ZIP_C

#include "zip.h"
#include <time.h>       /* for tzset() declaration */
#if defined(WIN32) || defined(WINDLL)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif
#ifdef ZIP_DLL_LIB
# include <setjmp.h>
#endif /* def ZIP_DLL_LIB */
#ifdef WINDLL
# include "windll/windll.h"
#endif
#define DEFCPYRT        /* main module: enable copyright string defines! */
#include "revision.h"
#include "crc32.h"
#include "crypt.h"
#include "ttyio.h"
#include <ctype.h>
#include <errno.h>

#ifdef WIN32
/* for locale, codepage support */
# include <locale.h>
# include <mbctype.h>
#endif

/* for getcwd, chdir */
#ifdef CHANGE_DIRECTORY
# ifdef WIN32
#  include <direct.h>
# endif
# ifdef UNIX
#  include <unistd.h>
# endif
#endif

#ifdef MACOS
# include "macglob.h"
  extern MacZipGlobals MacZip;
  extern int error_level;
#endif

#if (defined(MSDOS) && !defined(__GO32__)) || defined(__human68k__)
# include <process.h>
# if (!defined(P_WAIT) && defined(_P_WAIT))
#  define P_WAIT _P_WAIT
# endif
#endif

#if defined( UNIX) && defined( __APPLE__)
# include "unix/macosx.h"
#endif /* defined( UNIX) && defined( __APPLE__) */

#ifdef VMS
# include <stsdef.h>
# include "vms/vmsmunch.h"
# include "vms/vms.h"
extern void globals_dummy( void);
#endif /* def VMS */

#include <signal.h>
#include <stdio.h>

#ifdef UNICODE_TEST
# ifdef WIN32
#  include <direct.h>
# endif
#endif

#ifdef BZIP2_SUPPORT
# ifdef BZIP2_USEBZIP2DIR
#  include "bzip2/bzlib.h"
# else /* def BZIP2_USEBZIP2DIR */

   /* If IZ_BZIP2 is defined as the location of the bzip2 files, then
    * we assume that this location has been added to include path.  For
    * Unix, this is done by the unix/configure script.
    * If the OS includes support for a bzip2 library, then we assume
    * that the bzip2 header file is also found naturally.
    */

#  include "bzlib.h"
# endif /* def BZIP2_USEBZIP2DIR [else] */
#endif /* def BZIP2_SUPPORT [else] */


/* Local option flags */
#ifndef DELETE
# define DELETE 0
#endif
#define ADD     1
#define UPDATE  2
#define FRESHEN 3
#define ARCHIVE 4
local int action = ADD; /* one of ADD, UPDATE, FRESHEN, DELETE, or ARCHIVE */
                        /* comadd (edit entry comments) now global to support -st */
local int zipedit = 0;  /* 1=edit zip comment (and not "all file comments") */
local int latest = 0;   /* 1=set zip file time to time of latest file */
local int test = 0;     /* 1=test zip file with unzip -t */
local char *unzip_path = NULL; /* where to find "unzip" for archive testing */
local int tempdir = 0;  /* 1=use temp directory (-b) */
local int junk_sfx = 0; /* 1=junk the sfx prefix */

#if defined(AMIGA) || defined(MACOS)
local int filenotes = 0;    /* 1=take comments from AmigaDOS/MACOS filenotes */
#endif

#ifdef EBCDIC
int aflag = FT_EBCDIC_TXT;  /* Convert EBCDIC to ASCII or stay EBCDIC ? */
#endif
#ifdef CMS_MVS
int bflag = 0;              /* Use text mode as default */
#endif

#ifdef QDOS
char _version[] = VERSION;
#endif

#ifdef ZIP_DLL_LIB
jmp_buf zipdll_error_return;
# ifdef ZIP64_SUPPORT
  unsigned long low, high; /* returning 64 bit values for systems without an _int64 */
  uzoff_t filesize64;
# endif /* def ZIP64_SUPPORT */
#endif /* def ZIP_DLL_LIB */

#ifdef IZ_CRYPT_ANY
/* Pointer to crc_table, needed in crypt.c */
# if (!defined(USE_ZLIB) || defined(USE_OWN_CRCTAB))
ZCONST ulg near *crc_32_tab;
# else
/* 2012-05-31 SMS.
 * Zlib 1.2.7 changed the type of *get_crc_table() from uLongf to
 * z_crc_t (to get a 32-bit type on systems with a 64-bit long).  To
 * avoid complaints about mismatched (int-long) pointers (such as
 * %CC-W-PTRMISMATCH on VMS, for example), we need to match the type
 * zlib uses.  At zlib version 1.2.7, the only indicator available to
 * CPP seems to be the Z_U4 macro.
 */
#  ifdef Z_U4
ZCONST z_crc_t *crc_32_tab;
#  else /* def Z_U4 */
ZCONST uLongf *crc_32_tab;
#  endif /* def Z_U4 [else] */
# endif
#endif /* def IZ_CRYPT_ANY */

#ifdef IZ_CRYPT_AES_WG
# include "aes_wg/aes.h"
# include "aes_wg/aesopt.h"
# include "aes_wg/iz_aes_wg.h"
#endif

#ifdef IZ_CRYPT_AES_WG_NEW
# include "aesnew/ccm.h"
#endif

#if defined( LZMA_SUPPORT) || defined( PPMD_SUPPORT)
/* Some ports can't handle file names with leading numbers,
 * hence 7zVersion.h is now SzVersion.h.
 */
# include "szip/SzVersion.h"
#endif /* defined( LZMA_SUPPORT) || defined( PPMD_SUPPORT) */

/* Local functions */

local void freeup  OF((void));
local int  finish  OF((int));
#ifndef MACOS
# ifndef ZIP_DLL_LIB
local void handler OF((int));
# endif /* ndef ZIP_DLL_LIB */
local void license OF((void));
# ifndef VMSCLI
local void help    OF((void));
local void help_extended OF((void));
# endif /* ndef VMSCLI */
#endif /* ndef MACOS */

#ifdef ENABLE_USER_PROGRESS
# ifdef VMS
#  define USER_PROGRESS_CLASS extern
# else /* def VMS */
#  define USER_PROGRESS_CLASS local
int show_pid;
# endif /* def VMS [else] */
USER_PROGRESS_CLASS void user_progress OF((int));
#endif /* def ENABLE_USER_PROGRESS */

/* prereading of arguments is not supported in new command
   line interpreter get_option() so read filters as arguments
   are processed and convert to expected array later */
local int add_filter OF((int flag, char *pattern));
local int filterlist_to_patterns OF((void));
/* not used
 local int get_filters OF((int argc, char **argv));
*/

/* list to store file arguments */
local long add_name OF((char *filearg, int verbatim));


local int DisplayRunningStats OF((void));
local int BlankRunningStats OF((void));

local void version_info OF((void));

# if !defined(WINDLL) && !defined(MACOS)
local void zipstdout OF((void));
# endif /* !WINDLL && !MACOS */

#ifndef ZIP_DLL_LIB
local int check_unzip_version OF((char *unzippath));
local void check_zipfile OF((char *zipname, char *zippath));
#endif /* ndef ZIP_DLL_LIB */

/* structure used by add_filter to store filters */
struct filterlist_struct {
  char flag;
  char *pattern;
  struct filterlist_struct *next;
};
struct filterlist_struct *filterlist = NULL;  /* start of list */
struct filterlist_struct *lastfilter = NULL;  /* last filter in list */

/* structure used by add_filearg to store file arguments */
struct filelist_struct {
  char *name;
  int verbatim;
  struct filelist_struct *next;
};
long filearg_count = 0;
struct filelist_struct *filelist = NULL;  /* start of list */
struct filelist_struct *lastfile = NULL;  /* last file in list */

/* used by incremental archive */
long apath_count = 0;
struct filelist_struct *apath_list = NULL;  /* start of list */
struct filelist_struct *last_apath = NULL;  /* last apath in list */


local void freeup()
/* Free all allocations in the 'found' list, the 'zfiles' list and
   the 'patterns' list.  Also free up any globals that were allocated
   and close any open files. */
{
  struct flist far *f;  /* steps through found list */
  struct zlist far *z;  /* pointer to next entry in zfiles list */
  int j;

  for (f = found; f != NULL; f = fexpel(f))
    ;
  while (zfiles != NULL)
  {
    z = zfiles->nxt;
    if (zfiles->zname && zfiles->zname != zfiles->name)
      free((zvoid *)(zfiles->zname));
    if (zfiles->name)
      free((zvoid *)(zfiles->name));
    if (zfiles->iname)
      free((zvoid *)(zfiles->iname));
    if (zfiles->cext && zfiles->cextra && zfiles->cextra != zfiles->extra)
      free((zvoid *)(zfiles->cextra));
    if (zfiles->ext && zfiles->extra)
      free((zvoid *)(zfiles->extra));
    if (zfiles->com && zfiles->comment)
      free((zvoid *)(zfiles->comment));
    if (zfiles->oname)
      free((zvoid *)(zfiles->oname));
#ifdef UNICODE_SUPPORT
    if (zfiles->uname)
      free((zvoid *)(zfiles->uname));
    if (zfiles->zuname)
      free((zvoid *)(zfiles->zuname));
    if (zfiles->ouname)
      free((zvoid *)(zfiles->ouname));
# ifdef WIN32
    if (zfiles->namew)
      free((zvoid *)(zfiles->namew));
    if (zfiles->inamew)
      free((zvoid *)(zfiles->inamew));
    if (zfiles->znamew)
      free((zvoid *)(zfiles->znamew));
# endif
#endif
    farfree((zvoid far *)zfiles);
    zfiles = z;
    zcount--;
  }

  if (patterns != NULL) {
    while (pcount-- > 0) {
      if (patterns[pcount].zname != NULL)
        free((zvoid *)(patterns[pcount].zname));
    }
    free((zvoid *)patterns);
    patterns = NULL;
  }

  /* free up any globals */
  if (path_prefix) {
    free(path_prefix);
    path_prefix = NULL;
  }
  if (tempath != NULL)
  {
    free((zvoid *)tempath);
    tempath = NULL;
  }
  if (zipfile != NULL)
  {
    free((zvoid *)zipfile);
    zipfile = NULL;
  }
  if (in_path != NULL)
  {
    free((zvoid *)in_path);
    in_path = NULL;
  }
  if (out_path != NULL)
  {
    free((zvoid *)out_path);
    out_path = NULL;
  }
  if (zcomment != NULL)
  {
    free((zvoid *)zcomment);
    zcomment = NULL;
  }
  if (key != NULL) {
    free((zvoid *)key);
    key = NULL;
  }

  /* free any suffix lists */
  for (j = 0; mthd_lvl[j].method >= 0; j++)
  {
    if (mthd_lvl[j].suffixes)
      free(mthd_lvl[j].suffixes);
  }

  /* close any open files */
  if (in_file != NULL)
  {
    fclose(in_file);
    in_file = NULL;
  }

#ifdef CHANGE_DIRECTORY
  /* change dir */
  if (working_dir) {
# if 0
    /* return to startup directory
       This is not needed on Windows and Unix as the current directory
       of the zip process does not impact the caller. */
    if (startup_dir) {
      if (CHDIR(startup_dir)) {
        zprintf("changing to dir: %s\n  %s", startup_dir, strerror(errno));
      }
    }
# endif
    free(working_dir);
    working_dir = NULL;
  }
  if (startup_dir) {
    free(startup_dir);
    startup_dir = NULL;
  }
#endif

  /* If args still has args, free them */
  if (args) {
    free_args(args);
  }

#ifdef VMSCLI
  if (argv_cli != NULL)
  {
    /* Free VMS CLI argv[]. */
    if (argv_cli[0] != NULL)
      free(argv_cli[0]);
    free(argv_cli);
  }
#endif /* def VMSCLI */

  /* close logfile */
  if (logfile) {
    fclose(logfile);
  }
}

local int finish(e)
int e;                  /* exit code */
/* Process -o and -m options (if specified), free up malloc'ed stuff, and
   exit with the code e. */
{
  int r;                /* return value from trash() */
  ulg t;                /* latest time in zip file */
  struct zlist far *z;  /* pointer into zfile list */

  /* If latest, set time to zip file to latest file in zip file */
  if (latest && zipfile && strcmp(zipfile, "-"))
  {
    diag("changing time of zip file to time of latest file in it");
    /* find latest time in zip file */
    if (zfiles == NULL)
       zipwarn("zip file is empty, can't make it as old as latest entry", "");
    else {
      t = 0;
      for (z = zfiles; z != NULL; z = z->nxt)
        /* Ignore directories in time comparisons */
#ifdef USE_EF_UT_TIME
        if (z->iname[z->nam-1] != (char)0x2f)   /* ascii '/' */
        {
          iztimes z_utim;
          ulg z_tim;

          z_tim = ((get_ef_ut_ztime(z, &z_utim) & EB_UT_FL_MTIME) ?
                   unix2dostime(&z_utim.mtime) : z->tim);
          if (t < z_tim)
            t = z_tim;
        }
#else /* !USE_EF_UT_TIME */
        if (z->iname[z->nam-1] != (char)0x2f    /* ascii '/' */
            && t < z->tim)
          t = z->tim;
#endif /* ?USE_EF_UT_TIME */
      /* set modified time of zip file to that time */
      if (t != 0)
        stamp(zipfile, t);
      else
        zipwarn(
         "zip file has only directories, can't make it as old as latest entry",
         "");
    }
  }

  /* If dispose, delete all files in the zfiles list that are marked */
  if (dispose)
  {
    diag("deleting files that were added to zip file");
    if ((r = trash()) != ZE_OK)
      ZIPERR(r, "was deleting moved files and directories");
  }

/* SMSd. */ /* Bracket option, above, with the same #if/macro? */
  /* display execution time if -pt */
#ifdef ENABLE_ENTRY_TIMING
  if (performance_time) {
    uzoff_t delta;
    double secs;

    current_time = get_time_in_usec();
    delta = current_time - start_time;
    secs = (double)delta / 1000000;
    sprintf(errbuf, "(Zip took %8.3f secs)", secs);
    zipmessage(errbuf, "");
  }
#endif

  /* Done!  (Almost.) */
  freeup();
  return e;
}


/* show_env() - Display Zip-related environment variables.
 *
 * This is used by ziperr() and version_info().
 */
/* List of variables to check - port dependent */
  static ZCONST char *zipenv_names[] = {
# ifndef VMS
#  ifndef RISCOS
    "ZIP"
#  else /* RISCOS */
    "Zip$Options"
#  endif /* ?RISCOS */
# else /* VMS */
    "ZIP_OPTS"
# endif /* ?VMS */
    ,"ZIPOPT"
# ifdef AZTEC_C
    ,     /* extremely lame compiler bug workaround */
# endif
# ifndef __RSXNT__
#  ifdef __EMX__
    ,"EMX"
    ,"EMXOPT"
#  endif
#  if (defined(__GO32__) && (!defined(__DJGPP__) || __DJGPP__ < 2))
    ,"GO32"
    ,"GO32TMP"
#  endif
#  if (defined(__DJGPP__) && __DJGPP__ >= 2)
    ,"TMPDIR"
#  endif
# endif /* !__RSXNT__ */
# ifdef RISCOS
    ,"Zip$Exts"
# endif
  };

void show_env(non_null_only)
 int non_null_only;
{
  int heading = 0;
  int i;
  char *envptr;

  for (i = 0; i < sizeof(zipenv_names) / sizeof(char *); i++)
  {
    envptr = getenv(zipenv_names[i]);
    if ((non_null_only == 0) || (envptr != (char *)NULL))
    {
      if (heading == 0) {
        zipmessage_nl("Zip environment options:", 1);
        heading = 1;
      }
      sprintf(errbuf, "%16s:  %s", zipenv_names[i],
       ((envptr == (char *)NULL || *envptr == 0) ? "[none]" : envptr));
      zipmessage_nl(errbuf, 1);
    }
  }
  if ((non_null_only == 0) && (i == 0))
  {
      zipmessage_nl("        [none]", 1);
      zipmessage_nl("", 1);
  }
}


void ziperr(c, h)
int c;                  /* error code from the ZE_ class */
ZCONST char *h;         /* message about how it happened */
/* Issue a message for the error, clean up files and memory, and exit. */
{
#ifndef ZIP_DLL_LIB
# ifndef MACOS
  static int error_level = 0;
# endif

  if (error_level++ > 0)
     /* avoid recursive ziperr() printouts (his should never happen) */
     EXIT(ZE_LOGIC);  /* ziperr recursion is an internal logic error! */
#endif /* !ZIP_DLL_LIB */

  if (mesg_line_started) {
    zfprintf(mesg, "\n");
    mesg_line_started = 0;
  }
  if (logfile && logfile_line_started) {
    zfprintf(logfile, "\n");
    logfile_line_started = 0;
  }
  if (h != NULL) {
    if (PERR(c))
      zfprintf(mesg, "zip I/O error: %s", strerror(errno));
      /* perror("zip I/O error"); */
    fflush(mesg);
    zfprintf(mesg, "\nzip error: %s (%s)\n", ZIPERRORS(c), h);

#if defined(ZIPLIB) || defined(ZIPDLL)
  /* LIB and DLL error callback */
  if (*lpZipUserFunctions->error != NULL) {
    if (PERR(c)) {
      sprintf(errbuf, "zip I/O error: %s", strerror(errno));
      (*lpZipUserFunctions->error)(errbuf);
    }
    sprintf(errbuf, "zip error: %s (%s)", ZIPERRORS(c), h);
    (*lpZipUserFunctions->error)(errbuf);
  }
#endif

    /* Show non-null option environment variables after a syntax error. */
    if (c == ZE_PARMS) {
      show_env(1);
    }

#ifdef DOS
    check_for_windows("Zip");
#endif
    if (logfile) {
      if (PERR(c))
        zfprintf(logfile, "zip I/O error: %s\n", strerror(errno));
      zfprintf(logfile, "\nzip error: %s (%s)\n", ZIPERRORS(c), h);
      logfile_line_started = 0;
    }
  }
  if (tempzip != NULL)
  {
    if (tempzip != zipfile) {
      if (current_local_file)
        fclose(current_local_file);
      if (y != current_local_file && y != NULL)
        fclose(y);
#ifndef DEBUG
      destroy(tempzip);
#endif
      free((zvoid *)tempzip);
      tempzip = NULL;
    } else {
      /* -g option, attempt to restore the old file */

      /* zip64 support 09/05/2003 R.Nausedat */
      uzoff_t k = 0;                        /* keep count for end header */
      uzoff_t cb = cenbeg;                  /* get start of central */

      struct zlist far *z;  /* steps through zfiles linked list */

      zfprintf(mesg, "attempting to restore %s to its previous state\n",
         zipfile);
      if (logfile)
        zfprintf(logfile, "attempting to restore %s to its previous state\n",
           zipfile);

      zfseeko(y, cenbeg, SEEK_SET);

      tempzn = cenbeg;
      for (z = zfiles; z != NULL; z = z->nxt)
      {
        putcentral(z);
        tempzn += 4 + CENHEAD + z->nam + z->cext + z->com;
        k++;
      }
      putend(k, tempzn - cb, cb, zcomlen, zcomment);
      fclose(y);
      y = NULL;
    }
  }

  freeup();
#ifdef ZIP_DLL_LIB
  longjmp(zipdll_error_return, c);
#else
  EXIT(c);
#endif
}


void error(h)
  ZCONST char *h;
/* Internal error, should never happen */
{
  ziperr(ZE_LOGIC, h);
}

#if (!defined(MACOS) && !defined(ZIP_DLL_LIB) && !defined(NO_EXCEPT_SIGNALS))
local void handler(s)
int s;                  /* signal number (ignored) */
/* Upon getting a user interrupt, turn echo back on for tty and abort
   cleanly using ziperr(). */
{
# if defined(AMIGA) && defined(__SASC)
   _abort();
# else /* defined(AMIGA) && defined(__SASC) [else] */
#  if !defined(MSDOS) && !defined(__human68k__) && !defined(RISCOS)
  echon();
  putc('\n', mesg);
#  endif /* !MSDOS */
# endif /* defined(AMIGA) && defined(__SASC) [else] */
  ziperr(ZE_ABORT, "aborting");
  s++;                                  /* keep some compilers happy */
}
#endif /* !defined(MACOS) && !defined(ZIP_DLL_LIB) && !defined(NO_EXCEPT_SIGNALS) */


void zipmessage_nl(a, nl)
ZCONST char *a;     /* message string to output */
int nl;             /* 1 = add nl to end */
/* If nl false, print a message to mesg without new line.
   If nl true, print and add new line.
   If logfile is open then also write message to log file. */
{
  if (noisy) {
    if (a && strlen(a)) {
      zfprintf(mesg, "%s", a);
      mesg_line_started = 1;
    }
    if (nl) {
      if (mesg_line_started) {
        zfprintf(mesg, "\n");
        mesg_line_started = 0;
      }
    } else if (a && strlen(a)) {
      mesg_line_started = 1;
    }
    fflush(mesg);
  }
  if (logfile) {
    if (a && strlen(a)) {
      zfprintf(logfile, "%s", a);
      logfile_line_started = 1;
    }
    if (nl) {
      if (logfile_line_started) {
        zfprintf(logfile, "\n");
        logfile_line_started = 0;
      }
    } else if (a && strlen(a)) {
      logfile_line_started = 1;
    }
    fflush(logfile);
  }
}


void zipmessage(a, b)
ZCONST char *a, *b;     /* message strings juxtaposed in output */
/* Print a message to mesg and flush.  Also write to log file if
   open.  Write new line first if current line has output already. */
{
  if (noisy) {
    if (mesg_line_started)
      zfprintf(mesg, "\n");
    zfprintf(mesg, "%s%s\n", a, b);
    mesg_line_started = 0;
    fflush(mesg);
  }
  if (logfile) {
    if (logfile_line_started)
      zfprintf(logfile, "\n");
    zfprintf(logfile, "%s%s\n", a, b);
    logfile_line_started = 0;
    fflush(logfile);
  }
}

/* Print a warning message to mesg (usually stderr) and return,
 * with or without indentation.
 */
void zipwarn_i(indent, a, b)
int indent;
ZCONST char *a, *b;     /* message strings juxtaposed in output */
/* Print a warning message to mesg (usually stderr) and return. */
{
  char *prefix;
  char *warning;

  if (indent)
    prefix = "      ";
  else
    prefix = "";

  if (a == NULL)
  {
    a = "";
    warning = "            ";
  }
  else
  {
    warning = "zip warning:";
  }

  if (mesg_line_started)
    zfprintf(mesg, "\n");
  zfprintf(mesg, "%s%s %s%s\n", prefix, warning, a, b);
  mesg_line_started = 0;
#ifndef WINDLL
  fflush(mesg);
#endif

  if (logfile) {
    if (logfile_line_started)
      zfprintf(logfile, "\n");
    zfprintf(logfile, "%s%s %s%s\n", prefix, warning, a, b);
    logfile_line_started = 0;
    fflush(logfile);
  }

#ifdef WINDLL
  if (*lpZipUserFunctions->error != NULL) {
    char buf[6000];

    sprintf(buf, "%s%s %s%s\n", prefix, warning, a, b);
    (*lpZipUserFunctions->error)(buf);
  }
#endif
}

/* Print a warning message to mesg (usually stderr) and return. */

void zipwarn(a, b)
ZCONST char *a, *b;     /* message strings juxtaposed in output */
{
  zipwarn_i(0, a, b);
}

/* zipwarn_indent(): zipwarn(), with message indented. */

void zipwarn_indent(a, b)
ZCONST char *a, *b;
{
    zipwarn_i( 1, a, b);
}

local void license()
/* Print license information to stdout. */
{
  extent i;             /* counter for copyright array */

  for (i = 0; i < sizeof(swlicense)/sizeof(char *); i++)
    zprintf("%s\n", swlicense[i]);
}

# ifdef VMSCLI
void help()
# else /* def VMSCLI */
local void help()
# endif /* def VMSCLI [else] */
/* Print help (along with license info) to stdout. */
{
  extent i;             /* counter for help array */

  /* help array */
  static ZCONST char *text[] = {
# ifdef VMS
"Zip %s (%s). Usage: zip == \"$ disk:[dir]zip.exe\"",
# else /* def VMS */
"Zip %s (%s). Usage:",
# endif /* def VMS [else] */
# ifdef MACOS
"zip [-options] [-b fm] [-t mmddyyyy] [-n suffixes] [zipfile list] [-xi list]",
"  The default action is to add or replace zipfile entries from list.",
" ",
"  -f   freshen: only changed files  -u   update: only changed or new files",
"  -d   delete entries in zipfile    -m   move into zipfile (delete OS files)",
"  -r   recurse into directories     -j   junk (don't record) directory names",
"  -0   store only                   -l   convert LF to CR LF (-ll CR LF to LF)",
"  -1   compress faster              -9   compress better",
"  -q   quiet operation              -v   verbose operation/print version info",
"  -c   add one-line comments        -z   add zipfile comment",
"                                    -o   make zipfile as old as latest entry",
"  -F   fix zipfile (-FF try harder) -D   do not add directory entries",
"  -T   test zipfile integrity       -X   eXclude eXtra file attributes",
#  ifdef IZ_CRYPT_ANY
"  -e   encrypt                      -n   don't compress these suffixes"
#  else /* def IZ_CRYPT_ANY [else] */
"  -h   show this help               -n   don't compress these suffixes"
#  endif /* def IZ_CRYPT_ANY [else] */
," -hh  show more help",
"  Macintosh specific:",
"  -jj  record Fullpath (+ Volname)  -N store finder-comments as comments",
"  -df  zip only datafork of a file  -S include finder invisible/system files"
# else /* def MACOS [else] */
#  ifdef VM_CMS
"zip [-options] [-b fm] [-t mmddyyyy] [-n suffixes] [zipfile list] [-xi list]",
#  else /* def VM_CMS [else] */
"zip [-options] [-b path] [-t mmddyyyy] [-n suffixes] [zipfile list] [-xi list]",
#  endif /* def VM_CMS [else] */
"  The default action is to add or replace zipfile entries from list, which",
"  can include the special name - to compress standard input.",
"  If zipfile and list are omitted, zip compresses stdin to stdout.",
"  -f   freshen: only changed files  -u   update: only changed or new files",
"  -d   delete entries in zipfile    -m   move into zipfile (delete OS files)",
"  -r   recurse into directories     -j   junk (don't record) directory names",
#  ifdef THEOS
"  -0   store only                   -l   convert CR to CR LF (-ll CR LF to CR)",
#  else /* def THEOS [else] */
"  -0   store only                   -l   convert LF to CR LF (-ll CR LF to LF)",
#  endif /* def THEOS [else] */
"  -1   compress faster              -9   compress better",
"  -q   quiet operation              -v   verbose operation/print version info",
"  -c   add one-line comments        -z   add zipfile comment",
"  -@   read names from stdin        -o   make zipfile as old as latest entry",
"  -x   exclude the following names  -i   include only the following names",
#  ifdef EBCDIC
#   ifdef CMS_MVS
"  -a   translate to ASCII           -B   force binary read (text is default)",
#   else  /* !CMS_MVS [else] */
"  -a   translate to ASCII",
"  -aa  Handle all files as ASCII text files, EBCDIC/ASCII conversions.",
#   endif /* ?CMS_MVS [else] */
#  endif /* EBCDIC */
#  ifdef TANDEM
"                                    -Bn  set Enscribe formatting options",
#  endif /* def TANDEM */
"  -F   fix zipfile (-FF try harder) -D   do not add directory entries",
"  -A   adjust self-extracting exe   -J   junk zipfile prefix (unzipsfx)",
"  -T   test zipfile integrity       -X   eXclude eXtra file attributes",
#  ifdef VMS
"  -C   preserve case of file names  -C-  down-case all file names",
"  -C2  preserve case of ODS2 names  -C2- down-case ODS2 file names* (*=default)",
"  -C5  preserve case of ODS5 names* -C5- down-case ODS5 file names",
"  -V   save VMS file attributes (-VV also save allocated blocks past EOF)",
"  -w   store file version numbers\
   -ww  store file version numbers as \".nnn\"",
#  endif /* def VMS */
#  ifdef NTSD_EAS
"  -!   use privileges (if granted) to obtain all aspects of WinNT security",
#  endif /* NTSD_EAS */
#  ifdef OS2
"  -E   use the .LONGNAME Extended attribute (if found) as filename",
#  endif /* OS2 */
#  ifdef VMS
#   ifdef SYMLINKS
"  -vn  preserve all VMS file names  -y   store (don't follow) symlinks",
#   else /* def SYMLINKS [else] */
"  -vn  preserve all VMS file names",
#   endif /* def SYMLINKS [else] */
#  else /* def VMS [else] */
#   ifdef SYMLINKS
"  -y   store symbolic links as the link instead of the referenced file",
#   endif /* def SYMLINKS */
#  endif /* def VMS [else] */
/*
"  -R   PKZIP recursion (see manual)",
*/
#  if defined(MSDOS) || defined(OS2)
"  -$   include volume label         -S   include system and hidden files",
#  endif /* defined(MSDOS) || defined(OS2) */
#  ifdef AMIGA
#   ifdef IZ_CRYPT_ANY
"  -N   store filenotes as comments  -e   encrypt",
"  -h   show this help               -n   don't compress these suffixes"
#   else /* def IZ_CRYPT_ANY [else] */
"  -N   store filenotes as comments  -n   don't compress these suffixes"
#   endif /* def IZ_CRYPT_ANY [else] */
#  else /* def AMIGA [else] */
#   ifdef IZ_CRYPT_ANY
"  -e   encrypt                      -n   don't compress these suffixes"
#   else /* def IZ_CRYPT_ANY [else] */
"  -h   show this help               -n   don't compress these suffixes"
#   endif /* def IZ_CRYPT_ANY [else] */
#  endif /* def AMIGA [else] */
#  ifdef RISCOS
,"  -hh  show more help               -I   don't scan thru Image files"
#  else /* def RISCOS [else] */
#   if defined(UNIX) && defined(__APPLE__)
,"  -as  sequester AppleDouble files  -df  save Mac data fork only"
#   endif /* defined(UNIX) && defined(__APPLE__) */
,"  -hh  show more help"
#  endif /* def RISCOS [else] */
# endif /* def MACOS [else] */
# ifdef VMS
,"  (Must quote upper-case options, like \"-V\", unless SET PROC/PARSE=EXTEND)"
# endif /* def VMS */
,"  "
  };

  for (i = 0; i < sizeof(copyright)/sizeof(char *); i++)
  {
    zprintf(copyright[i], "zip");
    zprintf("\n");
  }
  for (i = 0; i < sizeof(text)/sizeof(char *); i++)
  {
    zprintf(text[i], VERSION, REVDATE);
    zprintf("\n");
  }
}

# ifdef VMSCLI
void help_extended()
# else /* def VMSCLI [else] */
local void help_extended()
# endif /* def VMSCLI [else] */
/* Print extended help to stdout. */
{
  extent i;             /* counter for help array */

  /* help array */
  static ZCONST char *text[] = {
"",
"Extended Help for Zip",
"",
"See the Zip Manual for more detailed help",
"",
"",
"Zip stores files in zip archives.  The default action is to add or replace",
"zipfile entries.",
"",
"Basic command line:",
"  zip options archive_name file file ...",
"",
"Some examples:",
"  Add file.txt to z.zip (create z if needed):      zip z file.txt",
"  Zip all files in current dir:                    zip z *",
"  Zip files in current dir and subdirs also:       zip -r z .",
"",
"Basic modes:",
" External modes (selects files from file system):",
"        add      - add new files/update existing files in archive (default)",
"  -u    update   - add new files/update existing files only if later date",
"  -f    freshen  - update files in archive only (no files added)",
"  -FS   filesync - update if date or size changed, delete if no OS match",
" Internal modes (selects entries in archive):",
"  -d    delete   - delete files from archive (see below)",
"  -U    copy     - select files in archive to copy (use with --out)",
"",
"Basic options:",
"  -r      recurse into directories (see Recursion below)",
"  -m      after archive created, delete original files (move into archive)",
"  -j      junk directory names (store just file names)",
"  -p      include relative dir path (deprecated) - use -j- instead (default)",
"  -q      quiet operation",
"  -v      verbose operation (just \"zip -v\" shows version information)",
"  -c      prompt for one-line comment for each entry (see Comments below)",
"  -z      prompt for comment for archive (end with just \".\" line or EOF)",
"  -o      make zipfile as old as latest entry",
"",
"",
"Syntax:",
"  The full command line syntax is:",
"",
"    zip [-shortopts ...] [--longopt ...] [zipfile [path path ...]] [-xi list]",
"",
"  Any number of short option and long option arguments are allowed",
"  (within limits) as well as any number of path arguments for files",
"  to zip up.  If zipfile exists, the archive is read in.  If zipfile",
"  is \"-\", stream to stdout.  If any path is \"-\", zip stdin.",
"",
"Options and Values:",
"  For short options that take values, use -ovalue or -o value or -o=value",
"  For long option values, use either --longoption=value or --longoption value",
"  For example:",
"    zip -ds 10 --temp-dir=path zipfile path1 path2 --exclude pattern pattern",
"  Avoid -ovalue (no space between) to avoid confusion",
"  With this release, optional values are supported for some options.  These",
"   optional values must be preceeded by \"=\" to avoid ambiguities.  E.g.:",
"      -9=deflate",
"",
"Two-character options:",
"  Be aware of 2-character options.  For example:",
"    -d -s is (delete, split size) while -ds is (dot size)",
"  Usually better to break short options across multiple arguments by function",
"    zip -r -dbdcds 10m -lilalf logfile archive input_directory -ll",
"  If combined options are not doing what you expect:",
"   - break up options to one per argument",
"   - use -sc to see what parsed command line looks like, incl shell expansion",
"   - use -so to check for 2-char options you might have unknowingly specified",
"   - use -sf to see files to operate on (includes -@, -@@)",
"",
"Verbatim args:",
"  All args after just \"--\" arg are read verbatim as paths and not options.",
"    zip zipfile path path ... -- verbatimpath verbatimpath ...",
"  For example:",
"    zip z -- \"-leaddashpath\" \"a[path].c\" \"@justfile\" \"path*withwild\"",
"  You may still have to escape or quote arguments to avoid shell expansion.",
"",
"Wildcards:",
"  Internally zip supports the following wildcards:",
"    ?       (or %% or #, depending on OS) matches any single character",
"    *       matches any number of characters, including zero",
"    [list]  matches char in list (regex), can do range [ac-f], all but [!bf]",
"  Set -RE (regex) to use [list] matching.  If use, must escape [ as [[].",

"  For shells that expand wildcards, escape (\\* or \"*\") so zip can recurse",
"    zip zipfile -r . -i \"*.h\"",
"    zip files_ending_with_number -RE \"foo[0-9].c\"",
"  On Unix, can use shell to process wildcards:",
"    zip files_ending_with_number foo[0-9].c",
"    zip zipfile * -i \"*.h\"",
"  but filters such as -i, -x, and -R should always be escaped.",
"",
"  Normally * crosses dir bounds in path, e.g. 'a*b' can match 'ac/db'.  If",
"  -ws option used, * does not cross dir bounds but ** does.",
"",
"  Use -nw to disable wildcards.  You may need to escape or quote to avoid",
"  shell expansion.",
"",
"Verbose/Version:",
"  -v        either enable verbose mode or show version",
"  The short option -v, when the only option, shows version info. This use",
"  can be forced by using long form --version.  Otherwise -v tells Zip to be",
"  verbose, providing additional info about what is going on.  (On VMS,",
"  additional levels of verboseness are enabled using -vv and -vvv.)  This",
"  info tends towards the debugging level and is probably not useful to",
"  average user.  Long option is --verbose.",
"",
"Input file lists:",
"  -@         read names (paths) to zip from stdin (one name per line)",
"  -@@ fpath  open file at file path fpath and read names (one name per line)",
"",
"Argument files:",
"  An argument file is a text file (default extension .zag) that contains",
"  white space separated arguments for Zip.  When Zip encounters @argfile, it",
"  tries to open file \"argfile\" and insert any args found into the command",
"  line at that location.",
"",
"  For example, if file myfiles.zag contains",
"    file1.txt  file2.txt  -ll",
"  then",
"    zip  myzipfile  @myfiles",
"  would create the command line",
"    zip  -ll  myzipfile  file1.txt  file2.txt",
"",
"  Arg files can contain most any valid option or argument.  Inserted contents",
"  not evaluated until complete command line built.  Enclose args with spaces",
"  in \"double quotes\".  Use -sc to see final command line.  Comments start",
"  with # arg (no non-space around it):",
"    file1.txt  file2.txt  -ll  #  my files, and convert to Unix line ends",
"  Directives must start at left edge (excluding white space).  Currently",
"  just one, #echo (no space between # and echo), that outputs message:",
"    #echo   Starting arg file 1",
"  Max depth of 4, so @1 can call @2 can call @3 can call @4.",
"",
"  -AF- will turn off processing arg files, so @file is just arg, but must",
"  appear before any arg files on command line.",
"",
"Include and Exclude:",
"  -i pattern pattern ...   include files that match a pattern",
"  -x pattern pattern ...   exclude files that match a pattern",
"  Patterns are paths with optional wildcards and match entire paths as",
"  stored in archive.  For example, aa/bb/* will match aa/bb/file.c,",
"  aa/bb/cc/file.txt, and so on.  Also, a*b.c will match ab.c, a/b.c, and",
"  ab/cd/efb.c.  (But see -ws to not match across slashes.)  Exclude and",
"  include lists end at next option, @, or end of line.",
"    zip -x pattern pattern @ zipfile path path ...",
"",
"  Note that include (-i), exclude (-x) and recurse current (-R) patterns",
"  are filters.  For -i and -x Zip will traverse directory trees given on",
"  command line.  For -R, directory tree starting at current directory is",
"  traversed.  Then -i, -x, and -R filters are applied to remove entries",
"  from list.  To save time, be specific when listing directories to search",
"  when using -i and -x.  For large current directory trees, consider using",
"  -r with targeted subdirectories instead of -R.",
"",
"Case matching:",
"  On most OS the case of patterns must match the case in the archive, unless",
"  the -ic option used.",
"  -ic       ignore case of archive entries",
"  This option not available on case-sensitive file systems.  On others, case",
"  ignored when matching files on file system but matching against archive",
"  entries remains case sensitive for modes -f (freshen), -U (archive copy),",
"  and -d (delete) because archive paths are always case sensitive.  With",
"  -ic, all matching ignores case, but possible multiple archive entries",
"  that differ only in case will match.",
"",
"End Of Line Translation (text files only):",
"  -l        change CR or LF (depending on OS) line end to CR LF (Unix->Win)",
"  -ll       change CR LF to CR or LF (depending on OS) line end (Win->Unix)",
"  If first buffer read from file contains binary the translation is skipped",
"  (This check generally reliable, but when there's no binary in first couple",
"  K bytes of file, this check can fail and binary file might get corrupted.)",
"",
"Recursion:",
"  -r        recurse paths, include files in subdirs:  zip -r a path path ...",
"  -R        recurse current dir and match patterns:   zip -R a ptn ptn ...",
"  Path root in archive starts at current dir, so if /a/b/c/file and",
"   current dir is /a/b, 'zip -r archive .' puts c/file in archive",
"  Use -i and -x with either to include or exclude paths",
"",
"Dates and date filtering:",
"  -t date   exclude before (include files modified on this date and later)",
"  -tt date  include before (include files modified before date)",
"  Can use both at same time to set inclusive date range (both -t and -tt",
"  are true).",
"",
"  Dates are mmddyyyy or yyyy-mm-dd.",
"  As of Zip 3.1, dates can include times.  The new optional format is:",
"    [date][:time]",
"  where a time value always starts with colon (:).  If no date, today",
"  assumed.  (Be careful when working near date boundaries where current date",
"  changes - probably best to always include date.)",
"  Time format:",
"    :HH:MM[:SS]",
"  where seconds optional.  24 hour format, so 15:10 is 3:10 PM.",
"  Examples:",
"    -t 2014-01-17            Include files modified on/after Jan 17, 2014",
"    -tt 01172014:07:08       Include files before 7:08 AM on Jan 17, 2014",
"    -tt 2014-01-17:07:08:09  Include files before 7:08:09 on Jan 17, 2014",
"    -t :07:08                Include files after 7:08 AM today",
"",
"  Be aware of time and date differences when Zip archives are moved between",
"  time zones.  Also changes in daylight saving time status.  Use of",
"  Universal Time extra field, when available, mitigates effects to some",
"  extent.  (See Zip Manual for more on this.)",
"",
"  -tn prevents storage of univeral time; -X prevents storage of most extra",
"   fields, including universal time.",
"",
"Deletion, File Sync:",
"  -d        delete files",
"  Delete archive entries matching internal archive paths in list",
"    zip archive -d pattern pattern ...",
"  Can use -t and -tt to select files in archive, but NOT -x or -i, so",
"    zip archive -d \"*\" -t 2005-12-27",
"  deletes all files from archive.zip with date of 27 Dec 2005 and later.",
"  Note the * (escape as \"*\" on Unix) to select all files in archive.",
"  -@ and -@@ can be used to provide a list of files to delete.  For example:",
"    zip foo -d -@@ filestodelete",
"  deletes all files listed in filestodelete, one file per line, from foo.",
"",
"  -FS       file sync",
"  Similar to update, but files updated if date or size of entry does not",
"  match file on OS.  Also deletes entry from archive if no matching file",
"  on OS.",
"    zip archive_to_update -FS -r dir_used_before",
"  Result generally same as creating new archive, but unchanged entries",
"  are copied instead of being read and compressed so can be faster.",
"      WARNING:  -FS deletes entries so make backup copy of archive first",
"",
"Compression:",
"  Compression method:",
"    -Z cm   set global compression method to cm:",
"              store   - store without compression, same as option -0",
"              deflate - original zip deflate, same as -1 to -9 (default)",
"              bzip2   - use bzip2 compression (need modern unzip)",
"              lzma    - use LZMA compression (need modern unzip)",
"              ppmd    - use PPMd compression (need modern unzip)",
"",
"    bzip2, LZMA, and PPMd are optional and may not be enabled.",
"",
"  Compression level:",
"    -0        store files (no compression)",
"    -1 to -9  compress fastest to compress best (default is 6)",
"",
"  Usually -Z and -0 .. -9 are sufficient for most needs.",
"",
"  Suffixes to not compress:",
"    -n suffix1:suffix2:...",
"    For example:",
"      -n .Z:.zip:.zoo:.arc:.lzh:.arj",
"    stores without compression files that end with these extensions.",
"  Default suffix list (don't compress):",
"    .Z:.zip:.zipx:.zoo:.arc:.lzh:.lha:.arj:.gz:.tgz:.tbz2:.tlz:.7z:.xz:.cab:",
"    .bz2:.lzma:.rz:.pea:.zz:.rar",
"  If this list works for you, you may not need -n.",
"",
"  Now can control level to use with particular method, and level/method to",
"  use with particular suffix (type of file).",
"",
"  Compression level to use for particular compression methods:",
"      -L=methodlist",
"    where L is compression level (1, 2, ..., 9), and methodlist is list of",
"    compression methods in form:",
"      cm1:cm2:...",
"    Examples: -4=deflate  -8=bzip2:lzma:ppmd",
"      Default to level 4 for deflate.  Default to 8 for bzip2, LZMA, PPMd.",
"      After above:",
"        \"zip archive file\" would use deflate at level 4.",
"        \"zip archive file -Z bzip2\" would use level 8 bzip2 compression.",
"    The \"=\" is required to associate method with level.  No spaces.",
"",
"  Compression method/level to use with particular file name suffixes:",
"      -n suffixlist",
"    where suffixlist is in the form:",
"      .ext1:.ext2:.ext3:...",
"    Files whose names end with suffix in list will be stored instead of",
"    compressed.  Note that list of just \":\" disables default list and",
"    forces compression of all extensions, including .zip files.",
"      zip -9 -n : compressall.zip *",
"    A more advanced form is now supported:",
"      -n cm=suffixlist",
"    where cm is a compression method (such as bzip2), which specifies use",
"    of compression method cm when one of those suffixes found.",
"      zip -n lzma=.txt:.log archive *",
"    uses default deflate on most files, but used LZMA on .txt/.log files",
"",
"    Specifying a store (-n list) or compression (-n method=list) list",
"    replaces old list with new list.  But suffix * includes current list.",
"    For example:",
"      zip foo.zip test1.doc test2.zip  -n \"*:.doc\"  -n lzma=.zip",
"    adds .doc to the current STORE list and changes .zip from STORE to",
"    LZMA compression, deleting any previous LZMA list.  * must be escaped",
"    on systems like Unix where the shell would expand it.",
"",
"    A yet more advanced form:",
"      -n cm-L=suffixlist",
"    where L is one of (1 through 9 or -), which specifies use of method cm",
"    at level L when one of those suffixes found.  (\"-\" specifies default",
"    level.)  Multiple -n can be used and are processed in order:",
"      -n deflate=.txt  -n lzma-9=.c:.h  -n ppmd=.txt",
"    which first sets the DEFLATE list to .txt, then sets the LZMA list to",
"    .c and .h and the default compression level to 9 when LZMA is used,",
"    then sets the PPMD list to .txt which pulls .txt from the DEFLATE list.",
"      zip foo test.txt  -n lzma-9=.c:.h  -n lzma-2=.txt  archive *",
"    sets LZMA list to .c:.h (compressing these using level 9 LZMA), then",
"    sets the LZMA list to .txt at level 2, wiping out the effects of the",
"    first LZMA -n.  Each compression method can accept only one set of",
"    suffixes and one level to use (if specified).  * can be used to add",
"    lists, but the last level specified is the one used.",
"      zip  foo  test1.zip test2.txt test3.Z  -n lzma=.zip  -n lzma=*:.Z",
"    compresses test2.txt using DEFLATE, and test1.zip and test3.Z with LZMA.",
"",
"    If no level is specified, the default level of 6 is used.",
"",
"  -ss      list the current compression mappings (suffix lists), and exit.",
"",
"Encryption:",
"  -e        use encryption, prompt for password (default if -Y used)",
"  -P pswd   use encryption, password is pswd (NOT SECURE!  Many OS allow",
"              seeing what others type on command line.  See manual.)",
"",
"  Default is original traditional (weak) PKZip 2.0 encryption, unless -Y",
"  is used to set another encryption method.",
"",
"  -Y em     set encryption method (TRADITIONAL, AES128, AES192, or AES256)",
"              (Zip uses WinZip AES encryption.  Other strong encryption",
"              methods may be added in future.)",
"",
"  For example:",
"    zip myarchive filetosecure.txt -Y AES2",
"  compresses using deflate and encrypts using AES256 file filetosecure.txt",
"  and adds it to myarchive.  Zip will prompt for password.",
"",
"  Min password lengths in chars:  16 (AES128), 20 (AES192), 24 (AES256).",
"  (No minimum for TRADITIONAL zip encryption.)  The longer and more varied",
"  the password the better.  We suggest length of passwords should be at least",
"  22 chars (AES128), 33 chars (AES192), and 44 chars (AES256) and include",
"  mix of lowercase, uppercase, numeric, and other characters.  Strength of",
"  encryption directly dependent on length and variedness of password.  To",
"  fully take advantage of AES encryption strength selected, passwords should",
"  be much longer and approach length of encryption key used.  Even using",
"  AES256, entries using simple short passwords are probably easy to crack.",
"",
"  -pn       allow non-ANSI characters in password.  Default is to restrict",
"              passwords to printable 7-bit ANSI characters for portability.",
"  -ps       allow shorter passwords than normally permitted.",
"",
"Splits (archives created as set of split files):",
"  -s ssize  create split archive with splits of size ssize, where ssize nm",
"              n number and m multiplier (kmgt, default m), 100k -> 100 kB",
"  -sp       pause after each split closed to allow changing disks",
"      WARNING:  Archives created with -sp use data descriptors and should",
"                work with most unzips but may not work with some",
"  -sb       ring bell when pause",
"  -sv       be verbose about creating splits",
"      Split archives CANNOT be updated, but see --out and Copy Mode below",
"",
"Using --out (output to new archive):",
"  --out oa  output to new archive oa (- for stdout)",
"  Instead of updating input archive, create new output archive oa.  Result",
"  same as without --out but in new archive.  Input archive unchanged.",
"      WARNING:  --out ALWAYS overwrites any existing output file",
"  For example, to create new_archive like old_archive but add newfile1",
"  and newfile2:",
"    zip old_archive newfile1 newfile2 --out new_archive",
"  Cannot update split archive, so use --out to out new archive:",
"    zip in_split_archive newfile1 newfile2 --out out_split_archive",
"  If input is split, output will default to same split size",
"  Use -s=0 or -s- to turn off splitting to convert split to single file:",
"    zip in_split_archive -s 0 --out out_single_file_archive",
"      WARNING:  If overwriting old split archive but need less splits,",
"                old splits not overwritten are not needed but remain",
"",
"Copy Mode (copying from archive to archive):",
"  -U        (also --copy) select entries in archive to copy (reverse delete)",
"  Copy Mode copies entries from old to new archive with --out and is used by",
"  zip when --out and either no input files or -U (--copy) used.",
"    zip inarchive --copy pattern pattern ... --out outarchive",
"  To copy only files matching *.c into new archive, excluding foo.c:",
"    zip old_archive --copy \"*.c\" --out new_archive -x foo.c",
"  Wildcards must be escaped if shell would process them.",
"  If no input files and --out, copy all entries in old archive:",
"    zip old_archive --out new_archive",
"",
"Comments:",
"  A zip archive can include comment for each entry as well as comment for",
"  entire archive (the archive comment):",
"  -c        prompt for one-line entry comment for each entry",
"  -z        prompt for archive comment (end with just \".\" line or EOF)",
"  Zip only accepts one-line comment for each file (ends at end of line).",
"  Archive comment can span multiple lines and ends when line with just \".\"",
"  is entered or when EOF detected.  As of Zip 3.1, can view existing comments",
"  and keep/remove/replace them.  For more complex comment operations,",
"  consider using ZipNote.",
"",
"Streaming and FIFOs:",
"  prog1 | zip -ll z -      zip output of prog1 to zipfile z, converting CR LF",
"  zip - -R \"*.c\" | prog2   zip *.c files in current dir and stream to prog2 ",
"  prog1 | zip | prog2      zip in pipe with no in or out acts like zip - -",
"  If Zip is Zip64 enabled, streaming stdin creates Zip64 archives by default",
"   that need PKZip 4.5 unzipper like UnZip 6.0",
"  WARNING:  Some archives created with streaming use data descriptors and",
"            should work with most unzips but may not work with some",
"  Can use --force-zip64- (-fz-) to turn off Zip64 if input known not to",
"  be large (< 4 GiB):",
"    prog_with_small_output | zip archive --force-zip64-",
"",
"  As of Zip 3.1, file attributes and comments can be included in local",
"  headers so entries in resulting zip file can be fully extracted using",
"  streaming unzip.  As new information adds slight amount to each entry,",
"  in this beta this feature needs to be enabled using -st (--stream)",
"  option.  In release may be enabled by default, except when -c used to set",
"  entry comments.",
"",
"  Normally when -c used to add comments for each entry added or updated,",
"  Zip asks for new comments after zip operation done.  When -c and -st used",
"  together, Zip asks for each comment as each entry processed so comments",
"  can be included in local headers, pausing while waiting for user input.",
"  For large archives, may be easier to first create archive without -st, then",
"  update archive without -c and with -st to add streaming:",
"    zip -st NoStreamArchive --out StreamArchive",
"",
"  If something like:",
"    zip - test.txt > outfile1.zip",
"  is used, Zip usually can seek in output file and standard archive is",
"  created.  However, if something like:",
"    zip - test.txt >> outfile2.zip",
"  used (append to output file), Zip will try to append zipfile to",
"  any existing contents in outfile2.zip.  On Unix where >> does not allow",
"  seeking before end of file, stream archive created.  On Windows where",
"  seeking before end of file allowed, standard archive is created.",
"",
"  Zip can read Unix FIFO (named pipes).  Off by default to prevent zip",
"  from stopping unexpectedly on unfed pipe, use -FI to enable:",
"    zip -FI archive fifo",
"",
"Dots, counts:",
"  Any console output may slow performance of Zip.  That said, there are",
"  times when additional output information is worth performance impact or",
"  when actual completion time not critical.  Below options can provide",
"  useful information in those cases.  If speed important but some indication",
"  of activity still needed, check out -dg option (which can be used with log",
"  file to save more detailed information).",
"",
"  -db       display running count of bytes processed and bytes to go",
"              (uncompressed size, except delete and copy show stored size)",
"  -dc       display running count of entries done and entries to go",
"  -dd       display dots every 10 MiB (or dot size) while processing files",
"  -de       display estimated time to go",
"  -dg       display dots globally for archive instead of for each file",
"    zip -qdgds 100m   will turn off most output except dots every 100 MiB",
"  -dr       display estimated zipping rate in bytes/sec",
"  -ds siz   each dot is siz processed where siz is nm as splits (0 no dots)",
"  -dt       display time Zip started zipping entry in day/hr:min:sec format",
"  -du       display original uncompressed size for each entry as added",
"  -dv       display volume (disk) number in format in_disk>out_disk",
"  Dot size is approximate, especially for dot sizes less than 1 MiB.",
"  Dot options don't apply to Scanning files dots (dot/2sec) (-q turns off).",
"  Options -de and -dr do not display for first few entries.",
"",
"Logging:",
"  -lf path  open file at path as logfile (overwrite existing file)",
"             Without -li, only end summary and any errors reported.",
"",
"            If path is \"-\" send log output to stdout, replacing normal",
"            output (implies -q).  Cannot use with -la or -v.",
"",
"    zip -lF -q -dg -ds 1g -r archive.zip foo",
"             will zip up directory foo, displaying just dots every 1 GiB",
"             and an end summary.",
"",
"  -la       append to existing logfile",
"  -lF       open log file named ARCHIVE.log, where ARCHIVE is the name of",
"             the output archive.  Shortcut for -lf ARCHIVE.log.",
"  -li       include info messages (default just warnings and errors)",
"  -lu       log names using UTF-8.  Instead of character set converted or",
"             escaped paths, put file paths in log as UTF-8.  Need an",
"             application that can understand UTF-8 to accurately read the",
"             log file, such as Notepad on Windows XP.  Since most consoles",
"             are not UTF-8 aware, sending log output to stdout to see",
"             UTF-8 probably won't do it.  See -UN=ShowUTF8 below.",
"",
"Testing archives:",
"  -T        test completed temp archive with unzip (in spawned process)",
"             before committing updates.  If zip given password, it gets",
"             passed to unzip.  Uses default \"unzip\" on system.",
"  -TT cmd   use command cmd instead of 'unzip -tqq' to test archive.  (If",
"             cmd includes spaces, put in quotes.)  On Unix, to use unzip in",
"             current directory, could use:",
"               zip archive file1 file2 -T -TT \"./unzip -tqq\"",
"             In cmd, {} replaced by temp archive path, else temp path",
"             appended, and {p} replaced by password if one provided to zip.",
"             Return code checked for success (0 on Unix).  Note that",
"             Zip spawns new process for UnZip, passing passwords on that",
"             command line which might be viewable by others.",
"",
"Fixing archives:",
"  -F        attempt to fix a mostly intact archive (try this first)",
"  -FF       try to salvage what can (may get more but less reliable)",
"  Fix options copy entries from potentially bad archive to new archive.",
"  -F tries to read archive normally and copy only intact entries, while",
"  -FF tries to salvage what can and may result in incomplete entries.",
"  Must use --out option to specify output archive:",
"    zip -F bad.zip --out fixed.zip",
"  As -FF only does one pass, may need to run -F on result to fix offsets.",
"  Use -v (verbose) with -FF to see details:",
"    zip reallybad.zip -FF -v --out fixed.zip",
"  Currently neither option fixes bad entries, as from text mode ftp get.",
"",
"Difference mode:",
"  -DF       (also --dif) only include files that have changed or are",
"             new as compared to the input archive",
"",
"  Difference mode can be used to create differential backups.  For example:",
"    zip --dif full_backup.zip -r somedir --out diff.zip",
"  will store all new files, as well as any files in full_backup.zip where",
"  either file time or size have changed from that in full_backup.zip,",
"  in new diff.zip.  Output archive not excluded automatically if exists,",
"  so either use -x to exclude it or put outside of what is being zipped.",
"",
"Backup modes:",
"  A recurring backup cycle can be set up using these options:",
"  -BT type   backup type (one of FULL, DIFF, or INCR)",
"  -BD bdir   backup dir (see below)",
"  -BN bname  backup name (see below)",
"  -BC cdir   backup control file dir (see below)",
"  -BL ldir   backup log dir (see below)",
"  This mode creates control file that is written to bdir/bname (unless cdir",
"  set, in which case written to cdir/bname).  This control file tracks",
"  archives currently in backup set.  bdir/bname also used as destination of",
"  created archives, so no output archive is specified, rather this mode",
"  creates output name by adding mode (full, diff, or incr) and date/time",
"  stamp to bdir/bname.  This allows recurring backups to be generated without",
"  altering command line to specify new archive names to avoid overwriting old",
"  backups.",
"  The backup types are:",
"    FULL - create a normal zip archive, but also create a control file.",
"    DIFF - create a --diff archive against the FULL archive listed in",
"             control file, and update control file to list both FULL and DIFF",
"             archives.  A DIFF backup set consists of just FULL and DIFF",
"             archives, as these two archives capture all files.",
"    INCR - create a --diff archive against FULL and any DIFF or INCR",
"             archives in control file, and update control file.  An INCR",
"             backup set consists of FULL archive and any INCR (and DIFF)",
"             archives created to that point, capturing just new and changed",
"             files each time an INCR backup is run.",
"  A FULL backup clears any DIFF and INCR entries from control file, hence",
"  starting new backup set.  A DIFF backup clears out any INCR archives listed",
"  in control file.  INCR just adds new incremental archive to list of",
"  archives in control file.",
"",
"  -BD bdir is dir backup archive goes in.  Must be given.",
"",
"  -BN bname is name of backup.  Gets prepended to type and date/time stamp.",
"  Also name of control file.  Must be given.",
"",
"  If -BC cdir given, control file written and maintained there.  Otherwise",
"  control file written to backup dir given by -BD.  Control file has",
"  file extension .zbc (Zip backup control).",
"",
"  If -BL given without value, a log file is written with same path as",
"  output archive, except .zip replaced by .log.  If -BT=ldir used,",
"  logfile written to log dir specified, but name of log will be same as",
"  archive, except .zip replaced by .log.  If you want list of files in",
"  log, include -li option.",
"",
"  The following command lines can be used to perform weekly full and daily",
"  incremental backups:",
"    zip -BT=full -BD=mybackups -BN=mybackupset -BL -li -r path_to_back_up",
"    zip -BT=incr -BD=mybackups -BN=mybackupset -BL -li -r path_to_back_up",
"  Full backups could be scheduled to run weekly and incr daily.",
"  Full backup will have a name such as:",
"    mybackups/mybackupset_full_FULLDATETIMESTAMP.zip",
"  and incrementals will have names such as:",
"    mybackups/mybackupset_full_FULLDATETIMESTAMP_incr_INCRDATETIMESTAMP.zip",
"  Backup mode not supported on systems that don't allow long enough names.",
"  It is recommended that bdir be outside of what is being backed up, either",
"  a separate drive, path, or other destination, or use -x to exclude backup",
"  directory and everything in it.  Otherwise each backup may get included",
"  in later backups, creating exponential growth.",
"",
"  Avoid putting multiple backups or control files in same directory to",
"  prevent name collisions, unless -BN names unique for each backup set.",
"",
"  See Manual for more information.",
"",
"DOS Archive bit (Windows only):",
"  -AS       include only files with DOS Archive bit set",
"  -AC       after archive created, clear archive bit of included files",
"      WARNING: Once archive bits are cleared they are cleared.",
"               Use -T to test the archive before bits are cleared.",
"               Can also use -sf to save file list before zipping files.",
"",
"Show files:",
"  -sf       show files to operate on, and exit (-sf- logfile only)",
"  -sF       add info to -sf listing, currently only -sF=usize",
"  List files that will be operated on.  Can list matching files on",
"  file system, matching entries in input archive, or both.  -sf can",
"  also be used to just list contents of archive.  For example:",
"     zip -sf -sF usize foo.zip",
"  will list each path in archive foo.zip followed by uncompressed size",
"  (rounded to 2 sig figs) in parentheses.",
"",
"  -su       as -sf but show escaped UTF-8 Unicode names also if exist",
"  -sU       as -sf but show escaped UTF-8 Unicode names instead",
"  Any character not in current locale escaped as #Uxxxx, where x is hex",
"  digit, if 16-bit code is sufficient, or #Lxxxxxx if 24-bits needed.",
"  If add -UN=e, Zip escapes all non-ASCII characters.",
"",
"Unicode:",
"  If compiled with Unicode support, Zip stores UTF-8 path of entries.",
"  This is backward compatible.  Unicode paths allow better conversion",
"  of entry names between different character sets.",
"",
"  New Unicode extra field includes checksum to verify Unicode path",
"  goes with standard path for that entry (as utilities like ZipNote",
"  can rename entries).  If these do not match, use below options to",
"  set what Zip does:",
"      -UN=Quit     - if mismatch, exit with error",
"      -UN=Warn     - if mismatch, warn, ignore UTF-8 (default)",
"      -UN=Ignore   - if mismatch, quietly ignore UTF-8",
"      -UN=No       - ignore any UTF-8 paths, use standard paths for all",
"  An exception to -UN=N are entries with new UTF-8 bit set (instead",
"  of using extra fields).  These entries typically come from systems where",
"  UTF-8 is native character set.  These are always handled as Unicode.",
"",
"  Normally Zip escapes all chars outside current char set, but leaves",
"  as is supported chars, which may not be OK in path names.  -UN=Escape",
"  escapes any character not ASCII:",
"    zip -sU -UN=e archive",
"  Can use either normal path or escaped Unicode path on command line",
"  to match files in archive.",
"",
"      -UN=UTF8     - store UTF-8 in main path and comment fields",
"      -UN=LOCAL    - store UTF-8 in extra fields (backward compatible)",
"  Zip now stores UTF-8 in entry path and comment fields on systems.",
"  This is the default for most other major zip utilities such as most",
"  modern Unix.  The default used to be to store UTF-8 in new extra fields",
"  (with escaped versions of entry path and comment in main fields for",
"  backward compatibility).  Option -UN=LOCAL will for new entries revert",
"  back to storing UTF-8 in extra fields.",
"",
"      -UN=SHOWUTF8 - output file names to console as UTF-8",
"  This option tells Zip to display UTF-8 directly on the console.  For",
"  UTF-8 native ports, this is the default.  For others, the port must be",
"  able to display UTF-8 characters to see them.  On Windows, setting a",
"  command prompt to Lucida Console and code page 65001 allows displaying",
"  some additional character sets, but (in Windows 7) not Asian like",
"  Japanese (get boxes instead).",
"",
"  A modern UnZip (such as UnZip 6.00 or later) is now needed to properly",
"  process archives created by Zip 3.1 that contain anything but 7-bit ANSI",
"  characters.  If the zip archive is destined for an old unzip, use",
"  -UN=LOCAL to ensure readability of paths using Unicode.",
"",
"  Only UTF-8 comments on UTF-8 native systems supported.  UTF-8 comments",
"  for other systems planned in next release.",
"",
"Symlinks and Mount Points",
"    -y  Store symlinks",
"  If -y, instead of quietly following symlinks, don't follow but store link",
"  and target.  Has been supported on Unix and other ports for awhile.",
"  Zip 3.1 now includes support for Windows symlinks, both file and",
"  directory.  UnZip 6.10 or later is needed to list and extract Windows",
"  symlinks.",
"    -yy Store/skip mount points",
"  Windows now includes various reparse point objects.  By default Zip just",
"  follows most quietly.  With -yy, these are skipped, except for mount",
"  points, which are saved as a directory in archive.  With -yy- (negated -yy)",
"  all reparse point objects (including those to offline storage) are",
"  followed.  Support for storing and restoring mount points is coming soon.",
"",
"Windows long paths:",
"  This version of Zip now includes ability to read and store long",
"  paths on Windows OS.  This requires UNICODE_SUPPORT be enabled.  A",
"  long path is one over MAX_PATH length (260 characters).  This feature",
"  is enabled using",
"    -wl (--windows-long-paths)",
"  option.  Previously long paths were ignored and so not included in the",
"  resulting archive.  This is still default without -wl.",
"",
"  Be warned that currently Windows shell can't read zip archives that",
"  include paths greater than 260 characters, and this may be true for",
"  other utilities as well.  DO NOT USE THIS FEATURE unless you have",
"  way to read and process resulting archive.  It looks like 7-Zip partially",
"  supports long paths, for instance.",
"",
"Security/ACLs/Extended Attributes:",
"  -!  - use any privileges to gain additional access (Windows NT)",
"  -!! - do not include security information (Windows NT)",
"  -X  - will leave out most extra fields, including security info",
"  -EA - sets how to handle extended attributes (NOT YET IMPLEMENTED)",
"",
"Self extractor:",
"  -A        Adjust offsets - a self extractor is created by prepending",
"             extractor executable to archive, but internal offsets are",
"             then off.  Use -A to fix offsets.",
"  -J        Junk sfx - removes prepended extractor executable from",
"             self extractor, leaving a plain zip archive.",
"",
"EBCDIC (MVS, z/OS, CMS, z/VM):",
"  -a        Translate from EBCDIC to ASCII",
"  -aa       Handle all files as text files, do EBCDIC/ASCII conversions",
"",
"Modifying paths:",
"  Currently there are six options that allow modifying paths as they are",
"  being stored in archive.  At some point a more general path modifying",
"  feature similar to the ZipNote capability is planned.",
"  -pp pfx   prefix string pfx to all paths in archive",
"  -pa pfx   prefix pfx to just added/updated entries in archive",
"  -Cl       store added/updated paths as lowercase",
"  -Cu       store added/updated paths as uppercase",
"",
"  If existing archive myzip.zip contains foo.txt:",
"    zip myzip bar.txt -pa abc_",
"  would result in foo.txt and abc_bar.txt in myzip, while:",
"    zip myzip bar.txt -pp abc_",
"  would result in abc_foo.txt and abc_bar.txt in myzip.",
"",
"  These can be used to put content into a directory:",
"    zip myzip bar.txt -pa abc/",
"  would result in foo.txt and abc/bar.txt in myzip, which has put",
"  bar.txt in the abc directory in the archive.",
"",
"  If existing archive myzip.zip contains foo.txt:",
"    zip myzip bar.txt -Cu",
"  would result in foo.txt and BAR.TXT in myzip.",
"",
"Current directory, temp files:",
"  -cd dir   change current directory zip operation relative to.",
"             Equivalent to changing current dir before calling zip, except",
"             current dir of caller not impacted.  On Windows, if dir",
"             includes different drive letter, zip changes to that drive.",
"  -b dir    when creating or updating archive, create temp archive in dir,",
"             which allows using seekable temp file when writing to write",
"             once CD, such archives compatible with more unzips (could",
"             require additional file copy if on another device)",
"",
"Zip error codes:",
"  This section to be expanded soon.  Zip error codes are detailed on",
"  man page.",
"",
"More option highlights (see manual for additional options and details):",
"  -MM       input patterns must match at least one file and matched files",
"             must be readable or exit with OPEN error and abort archive",
"             (without -MM, both are warnings only, and if unreadable files",
"             are skipped OPEN error (18) returned after archive created)",
"  -MV=m     [MVS] set MVS path translation mode.  m is one of:",
"              dots     - store paths as they are (typically aa.bb.cc.dd)",
"              slashes  - change aa.bb.cc.dd to aa/bb/cc/dd",
"              lastdot  - change aa.bb.cc.dd to aa/bb/cc.dd (default)",
"  -nw       no wildcards (wildcards are like any other character)",
"  -sc       show command line arguments as processed, and exit",
"  -sd       show debugging as Zip does each step",
"  -so       show all available options on this system",
"  -X        default=strip old extra fields, -X- keep old, -X strip most",
"  -ws       wildcards don't span directory boundaries in paths",
""
  };

  for (i = 0; i < sizeof(text)/sizeof(char *); i++)
  {
    zprintf("%s\n", text[i]);
  }
# ifdef DOS
  check_for_windows("Zip");
# endif
}


void quick_version()
{
  zfprintf(mesg, "zip %d.%d.%d  %s  (zip %s)\n", Z_MAJORVER, Z_MINORVER,
           Z_PATCHLEVEL, REVYMD, VERSION);
}


/*
 * XXX version_info() in a separate file
 */
local void version_info()
/* Print verbose info about program version and compile time options
   to stdout. */
{
  extent i;             /* counter in text arrays */

  /* AES_WG option string storage (with version). */

# ifdef IZ_CRYPT_AES_WG
  static char aes_wg_opt_ver[81];
# endif /* def IZ_CRYPT_AES_WG */

  /* Bzip2 option string storage (with version). */

# ifdef BZIP2_SUPPORT
  static char bz_opt_ver[81];
  static char bz_opt_ver2[81];
  static char bz_opt_ver3[81];
# endif

# ifdef LZMA_SUPPORT
  static char lzma_opt_ver[81];
# endif

# ifdef PPMD_SUPPORT
  static char ppmd_opt_ver[81];
# endif

# ifdef IZ_CRYPT_TRAD
  static char crypt_opt_ver[81];
# endif

  /* Non-default AppleDouble resource fork suffix. */
# if defined(UNIX) && defined(__APPLE__)
#  ifndef APPLE_NFRSRC
#   error APPLE_NFRSRC not defined.
   Bad code: error APPLE_NFRSRC not defined.
#  endif
#  if defined(__ppc__) || defined(__ppc64__)
#   if APPLE_NFRSRC
#    define APPLE_NFRSRC_MSG \
     "APPLE_NFRSRC         (\"/..namedfork/rsrc\" suffix for resource fork)"
#   endif /* APPLE_NFRSRC */
#  else /* defined(__ppc__) || defined(__ppc64__) [else] */
#   if ! APPLE_NFRSRC
#    define APPLE_NFRSRC_MSG \
     "APPLE_NFRSRC         (NOT!  \"/rsrc\" suffix for resource fork)"
#   endif /* ! APPLE_NFRSRC */
#  endif /* defined(__ppc__) || defined(__ppc64__) [else] */
# endif /* defined(UNIX) && defined(__APPLE__) */

  /* Compile options info array */
  static ZCONST char *comp_opts[] = {
# ifdef APPLE_NFRSRC_MSG
    APPLE_NFRSRC_MSG,
# endif
# ifdef APPLE_XATTR
    "APPLE_XATTR          (Apple extended attributes supported)",
# endif
# ifdef ASM_CRC
    "ASM_CRC              (assembly code used for CRC calculation)",
# endif
# ifdef ASMV
    "ASMV                 (assembly code used for pattern matching)",
# endif
# ifdef BACKUP_SUPPORT
    "BACKUP_SUPPORT       (enable backup options: -BT and related)",
# endif
# ifdef DYN_ALLOC
    "DYN_ALLOC",
# endif
# ifdef MMAP
    "MMAP",
# endif
# ifdef BIG_MEM
    "BIG_MEM",
# endif
# ifdef MEDIUM_MEM
    "MEDIUM_MEM",
# endif
# ifdef SMALL_MEM
    "SMALL_MEM",
# endif
# if defined(DEBUG)
    "DEBUG                (debug/trace mode)",
# endif
# if defined(_DEBUG)
    "_DEBUG               (compiled with debugging)",
# endif
# ifdef USE_EF_UT_TIME
    "USE_EF_UT_TIME       (store Universal Time)",
# endif
# ifdef NTSD_EAS
    "NTSD_EAS             (store NT Security Descriptor)",
# endif
# if defined(WIN32) && defined(NO_W32TIMES_IZFIX)
    "NO_W32TIMES_IZFIX",
# endif
# ifdef VMS
#  ifdef VMSCLI
    "VMSCLI               (VMS-style command-line interface)",
#  endif
#  ifdef VMS_IM_EXTRA
    "VMS_IM_EXTRA         (IM-style (obsolete) VMS file attribute encoding)",
#  endif
#  ifdef VMS_PK_EXTRA
    "VMS_PK_EXTRA         (PK-style (default) VMS file attribute encoding)",
#  endif
# endif /* VMS */
# ifdef WILD_STOP_AT_DIR
    "WILD_STOP_AT_DIR     (wildcards do not cross directory boundaries)",
# endif
# ifdef WIN32_OEM
    "WIN32_OEM            (store file paths on Windows as OEM)",
# endif
# ifdef BZIP2_SUPPORT
    bz_opt_ver,
    bz_opt_ver2,
    bz_opt_ver3,
# endif
# ifdef LZMA_SUPPORT
    lzma_opt_ver,
# endif
# ifdef PPMD_SUPPORT
    ppmd_opt_ver,
# endif
# ifdef SYMLINKS
#  ifdef VMS
    "SYMLINKS             (symbolic links supported, if C RTL permits)",
#  else
#   ifdef WIN32
    "SYMLINKS             (symbolic links (reparse points) supported)",
#   else
    "SYMLINKS             (symbolic links supported)",
#   endif
#  endif
# endif
# ifdef LARGE_FILE_SUPPORT
#  ifdef USING_DEFAULT_LARGE_FILE_SUPPORT
    "LARGE_FILE_SUPPORT   (default settings)",
#  else
    "LARGE_FILE_SUPPORT   (can read and write large files on file system)",
#  endif
# endif
# ifdef ZIP64_SUPPORT
    "ZIP64_SUPPORT        (use Zip64 to store large files in archives)",
# endif
# ifdef UNICODE_SUPPORT
    "UNICODE_SUPPORT      (store and read Unicode paths)",
# endif
# ifdef UNICODE_WCHAR
    "UNICODE_WCHAR        (Unicode support via wchar_t wide functions)",
# endif
# ifdef UNICODE_ICONV
    "UNICODE_ICONV        (Unicode support via iconv)",
# endif
# ifdef UNICODE_FILE_SCAN
    "UNICODE_FILE_SCAN    (file system scans use Unicode)",
# endif

# ifdef UNIX
    "STORE_UNIX_UIDs_GIDs (store UID, GID (any size) using \"ux\" extra field)",
#  ifdef UIDGID_NOT_16BIT
    "UIDGID_NOT_16BIT     (old Unix 16-bit UID/GID extra field not used)",
#  else
    "UIDGID_16BIT         (old Unix 16-bit UID/GID extra field also used)",
#  endif
# endif

# ifdef IZ_CRYPT_TRAD
    crypt_opt_ver,
#  ifdef ETWODD_SUPPORT
    "ETWODD_SUPPORT       (encrypt Traditional w/o data descriptor if -et)",
#  endif /* def ETWODD_SUPPORT */
# endif

# ifdef IZ_CRYPT_AES_WG
    aes_wg_opt_ver,
# endif

# ifdef IZ_CRYPT_AES_WG_NEW
    "IZ_CRYPT_AES_WG_NEW  (AES strong encryption (WinZip/Gladman new))",
# endif

# if defined(IZ_CRYPT_ANY) && defined(PASSWD_FROM_STDIN)
    "PASSWD_FROM_STDIN",
# endif /* defined(IZ_CRYPT_ANY) && defined(PASSWD_FROM_STDIN) */

# ifdef WINDLL
    "WINDLL               (Windows DLL/LIB)",
#  ifdef ZIPLIB
    "ZIPLIB               (compiled as Zip static library)",
#  else
    "ZIPDLL               (compiled as Zip dynamic library)",
#  endif
# else
#  ifdef ZIPLIB
    "ZIPLIB               (compiled as Zip static library)",
#  endif
#  ifdef ZIPDLL
    "ZIPDLL               (compiled as Zip dynamic library)",
#  endif
# endif

# ifdef WINDOWS_LONG_PATHS
    "WINDOWS_LONG_PATHS   (can store paths longer than 260 chars on Windows)",
# endif

    NULL
  };


  for (i = 0; i < sizeof(copyright)/sizeof(char *); i++)
  {
    zprintf(copyright[i], "zip");
    zprintf("\n");
  }

  for (i = 0; i < sizeof(versinfolines)/sizeof(char *); i++)
  {
    zprintf(versinfolines[i], "Zip", VERSION, REVDATE);
    zprintf("\n");
  }

  version_local();

  puts("Zip special compilation options:");
# if WSIZE != 0x8000
  zprintf("        WSIZE=%u\n", WSIZE);
# endif


/*
  RBW  --  2009/06/23  --  TEMP TEST for devel...drop when done.
  Show what some critical sizes are. For z/OS, long long and off_t
  must be 8 bytes (off_t is a typedefed long long), and fseeko must
  take zoff_t as its 2nd arg.
*/
# if 0
  zprintf("* size of int:         %d\n", sizeof(int));        /* May be 4 */
  zprintf("* size of long:        %d\n", sizeof(long));       /* May be 4 */
  zprintf("* size of long long:   %d\n", sizeof(long long));  /* Must be 8 */
  zprintf("* size of off_t:       %d\n", sizeof(off_t));      /* Must be 8 */
#  ifdef __LF
  zprintf("__LF is defined.\n");
  zprintf("  off_t must be defined as a long long\n");
#  else /* def __LF [else] */           /* not all compilers know elif */
#   ifdef _LP64
  zprintf("_LP64 is defined.\n");
  zprintf("  off_t must be defined as a long\n");
#   else /* def _LP64 [else] */
  zprintf("Neither __LF nor _LP64 is defined.\n");
  zprintf("  off_t must be defined as an int\n");
#   endif /* def _LP64 [else] */
#  endif /* def __LF [else] */
# endif /* 0 */


  /* Fill in IZ_AES_WG version. */
# ifdef IZ_CRYPT_AES_WG
  sprintf( aes_wg_opt_ver,
    "IZ_CRYPT_AES_WG      (AES encryption (IZ WinZip/Gladman), ver %d.%d%s)",
    IZ_AES_WG_MAJORVER, IZ_AES_WG_MINORVER, IZ_AES_WG_BETA_VER);
# endif

  /* Fill in bzip2 version.  (32-char limit valid as of bzip 1.0.3.) */
# ifdef BZIP2_SUPPORT
  sprintf( bz_opt_ver,
    "BZIP2_SUPPORT        (bzip2 library version %.32s)", BZ2_bzlibVersion());
  sprintf( bz_opt_ver2,
    "    bzip2 code and library copyright (c) Julian R Seward");
  sprintf( bz_opt_ver3,
    "    (See the bzip2 license for terms of use)");
# endif

  /* Fill in LZMA version. */
# ifdef LZMA_SUPPORT
  sprintf(lzma_opt_ver,
    "LZMA_SUPPORT         (LZMA compression, ver %s)",
    MY_VERSION);
  i++;
# endif

  /* Fill in PPMd version. */
# ifdef PPMD_SUPPORT
  sprintf(ppmd_opt_ver,
    "PPMD_SUPPORT         (PPMd compression, ver %s)",
    MY_VERSION);
  i++;
# endif

# ifdef IZ_CRYPT_TRAD
  sprintf(crypt_opt_ver,
    "IZ_CRYPT_TRAD        (Traditional (weak) encryption, ver %d.%d%s)",
    CR_MAJORVER, CR_MINORVER, CR_BETA_VER);
# endif /* def IZ_CRYPT_TRAD */

  for (i = 0; (int)i < (int)(sizeof(comp_opts)/sizeof(char *) - 1); i++)
  {
    zprintf("        %s\n",comp_opts[i]);
  }

# ifdef USE_ZLIB
  if (strcmp(ZLIB_VERSION, zlibVersion()) == 0)
    zprintf("        USE_ZLIB             (zlib version %s)\n", ZLIB_VERSION);
  else
    zprintf("        USE_ZLIB             (compiled with version %s, using %s)\n",
      ZLIB_VERSION, zlibVersion());
  i++;  /* zlib use means there IS at least one compilation option */
# endif

  if (i != 0)
    zprintf("\n");

/* Any CRYPT option sets "i", indicating at least one compilation option. */

# ifdef IZ_CRYPT_TRAD
  for (i = 0; i < sizeof(cryptnote)/sizeof(char *); i++)
  {
    zprintf("%s\n", cryptnote[i]);
  }
# endif /* def IZ_CRYPT_TRAD */

# ifdef IZ_CRYPT_AES_WG
#  ifdef IZ_CRYPT_TRAD
  zprintf("\n");
#  endif /* def IZ_CRYPT_TRAD */
  for (i = 0; i < sizeof(cryptAESnote)/sizeof(char *); i++)
  {
    zprintf("%s\n", cryptAESnote[i]);
  }
# endif /* def IZ_CRYPT_AES_WG */

# ifdef IZ_CRYPT_AES_WG_NEW
#  if defined( IZ_CRYPT_TRAD) || defined( IZ_CRYPT_AES_WG)
  zprintf("\n");
#  endif /* defined( IZ_CRYPT_TRAD) || defined( IZ_CRYPT_AES_WG) */
  for (i = 0; i < sizeof(cryptAESnote)/sizeof(char *); i++)
  {
    zprintf("%s\n", cryptAESnote[i]);
  }
# endif /* def IZ_CRYPT_AES_WG_NEW */

# if defined( IZ_CRYPT_ANY) || defined( IZ_CRYPT_AES_WG_NEW)
  zprintf("\n");
# endif

  /* Show option environment variables (unconditionally). */
  show_env(0);

# ifdef DOS
  check_for_windows("Zip");
# endif
}


#ifndef PROCNAME
/* Default to case-sensitive matching of archive entries for the modes
   that specifically operate on archive entries, as this archive may
   have come from a system that allows paths in the archive to differ
   only by case.  Except for adding ARCHIVE (copy mode), this is how it
   was done before.  Note that some case-insensitive ports (WIN32, VMS)
   define their own PROCNAME() in their respective osdep.h that use the
   filter_match_case flag set to FALSE by the -ic option to enable
   case-insensitive archive entry mathing. */
#  define PROCNAME(n) procname(n, (action == ARCHIVE || action == DELETE \
                                   || action == FRESHEN) \
                                  && filter_match_case)
#endif /* PROCNAME */


#if !defined(WINDLL) && !defined(MACOS)
local void zipstdout()
/* setup for writing zip file on stdout */
{
  mesg = stderr;
  if (isatty(1))
    ziperr(ZE_PARMS, "cannot write zip file to terminal");
  if ((zipfile = malloc(4)) == NULL)
    ziperr(ZE_MEM, "was processing arguments (1)");
  strcpy(zipfile, "-");
  /*
  if ((r = readzipfile()) != ZE_OK)
    ziperr(r, zipfile);
  */
}
#endif /* !WINDLL && !MACOS */


#ifndef ZIP_DLL_LIB
/* ------------- test the archive ------------- */

local int check_unzip_version(unzippath)
  char *unzippath;
{
# ifdef ZIP64_SUPPORT
  /* Here is where we need to check for the version of unzip the user
   * has.  If creating a Zip64 archive, we need UnZip 6 or later or
   * testing may fail.
   */
    char cmd[4004];
    FILE *unzip_out = NULL;
    char buf[1001];
    float UnZip_Version = 0.0;

    cmd[0] = '\0';
    strncat(cmd, unzippath, 4000);
    strcat(cmd, " -v");

#  if defined(ZOS)
    /*  RBW  --  More z/OS - MVS nonsense.  Cast shouldn't be needed. */
    /*  Real fix is probably to find where popen is defined...        */
    /*  The compiler seems to think it's returning an int.            */

    /* Assuming the cast is needed for z/OS, probably can put it in
       the main code version and drop this #if.  Other ports shouldn't
       have trouble with the cast, but left it as is for now.  - EG   */
    if ((unzip_out = (FILE *) popen(cmd, "r")) == NULL) {
#  else
    if ((unzip_out = popen(cmd, "r")) == NULL) {
#  endif
      zperror("unzip pipe error");
    } else {
      if (fgets(buf, 1000, unzip_out) == NULL) {
        zipwarn("failed to get information from UnZip", "");
      } else {
        /* the first line should start with the version */
        if (sscanf(buf, "UnZip %f ", &UnZip_Version) < 1) {
          zipwarn("unexpected output of UnZip -v", "");
        } else {
          /* printf("UnZip %f\n", UnZip_Version); */

          while (fgets(buf, 1000, unzip_out)) {
          }
        }
      }
      pclose(unzip_out);
    }
    if (UnZip_Version < 6.0 && zip64_archive) {
      sprintf(buf, "Found UnZip version %4.2f", UnZip_Version);
      zipwarn(buf, "");
      zipwarn("Need UnZip 6.00 or later to test this Zip64 archive", "");
      return 0;
    }
# endif /* def ZIP64_SUPPORT */
  return 1;
}


# ifdef UNIX

/* strcpy_qu()
 * Copy a string (src), adding apostrophe quotation, to dst.
 * Return actual length.
 *
 * This is used to build the string used to call unzip for testing
 * an archive.
 *
 * The destination string must be allocated at least 2 times + 2 the
 * space needed by the source string. It probably would be cleaner
 * to allocate the destination here and return a pointer to it.
 */
local int strcpy_qu( dst, src)
 char *dst;
 char *src;
{
  char *cp1;
  char *cp2;
  int len;

  /* Surround the archive name with apostrophes.
   * Convert an embedded apostrophe to the required mess.
   */

  *dst = '\'';                             /* Append an initial apostrophe. */
  len = 1;                                 /* One char so far. */

  /* Copy src to dst, converting >'< to >'"'"'<. */
  cp1 = src;
  while ((cp2 = strchr( cp1, '\'')))       /* Find next apostrophe. */
  {
    strncpy((dst + len), cp1, (cp2 - cp1));  /* Append chars up to next apos. */
    len += cp2 - cp1;                      /* Increment the dst length. */
    strncpy((dst + len), "'\"'\"'", 5);    /* Replace src >'< with >'"'"'<. */
    len += 5;                              /* Increment the dst length. */
    cp1 = cp2 + 1;                         /* Advance beyond src apostrophe. */
  }
  strcpy((dst + len), cp1);                /* Append the remainder of src. */
  len += strlen(cp1);                      /* Increment the dst length. */
  strcpy((dst + len), "'");                /* Append a final apostrophe. */
  len += 1;                                /* Increment the dst length. */

  return len;                              /* Help the caller count chars. */
}
# endif /* def UNIX */


# if 0
/* Quote double quotes in string (" -> \\\").  This reduces to \" in
 * the string, which inserts just " into the string.  This may not
 * work for all shells, as some may expect quotes to be quotes as
 * repeated quotes ("") to insert a single quote.
 *
 * This version does not handle character sets where \0 is used for
 * other than just a NULL terminator.  Passwords should be just ASCII,
 * but should also work for UTF-8 and most other character sets.)
 */
local char* quote_quotes(instring)
  char *instring;
{
  char *temp;
  char *outstring;
  int i;
  int j;
  char c;

  /* Get a temp string that is long enough to handle a large number
     of embedded quotes. */
  if ((temp = malloc(strlen(instring) * 4 + 100)) == NULL) {
    ziperr(ZE_MEM, "quote_quotes (1)");
  }
  temp[0] = '\0';
  for (i = 0, j = 0; (c = instring[i]); i++) {
    if (c == '"') {
      temp[j++] = '\\';
      temp[j++] = '"';
    } else {
      temp[j++] = c;
    }
  }
  temp[j] = '\0';

  /* Get a string that is the size of the resulting command string. */
  if ((outstring = malloc(strlen(temp) + 1)) == NULL) {
    ziperr(ZE_MEM, "quote_quotes (2)");
  }
  /* Copy over quoted string. */
  strcpy(outstring, temp);
  free(temp);

  return outstring;
}
# endif /* 0 */


local void check_zipfile(zipname, zippath)
  char *zipname;
  char *zippath;
  /* Invoke unzip -t on the given zip file */
{
# if (defined(MSDOS) && !defined(__GO32__)) || defined(__human68k__)
  int status, len;
  char *path, *p;
  char *zipnam;

  if ((zipnam = (char *)malloc(strlen(zipname) + 3)) == NULL)
    ziperr(ZE_MEM, "was creating unzip zipnam");

#  ifdef MSDOS
  /* Add quotes for MSDOS.  8/11/04 */

  /* accept spaces in name and path */
  /* double quotes are illegal in MSDOS/Windows paths */
  strcpy(zipnam, "\"");
  strcat(zipnam, zipname);
  strcat(zipnam, "\"");
#  else
  strcpy(zipnam, zipname);
#  endif

  if (unzip_path) {
    /* if user gave us the unzip to use (-TT) go with it */
    char *brackets;             /* "{}" = where path of temp archive goes. */
    int len;
    char *cmd;
    char *cmd2 = NULL;

    /* Replace first {} with archive name.  If no {} append name to string. */
    brackets = strstr(unzip_path, "{}");

    if ((cmd = (char *)malloc(strlen(unzip_path) + strlen(zipnam) + 3)) == NULL)
      ziperr(ZE_MEM, "was creating unzip cmd (1)");

    if (brackets) {
      /* have {} so replace with temp name */
      len = brackets - unzip_path;
      strcpy(cmd, unzip_path);
      cmd[len] = '\0';
      strcat(cmd, " ");
      strcat(cmd, zipnam);
      strcat(cmd, " ");
      strcat(cmd, brackets + 2);
    } else {
      /* No {} so append temp name to end */
      strcpy(cmd, unzip_path);
      strcat(cmd, " ");
      strcat(cmd, zipnam);
    }

    /* Replace first {p} with password given to zip.  If no password
     * was given (-P or -e), any {p} remains in the command string.  If
     * a password was given but no {p}, we don't know where to put the
     * password so don't include it.
     *
     * Since we don't know what utility is being used, we don't know how
     * to pass the password other than by using the command line.
     */
    if (key) {
      char *passwd_here;   /* where password goes */

      passwd_here = strstr(cmd, "{p}");

      if (passwd_here) {
        /* have {p} so replace with password */
        if ((cmd2 = (char *)malloc(strlen(cmd) + strlen(key) + 3)) == NULL)
          ziperr(ZE_MEM, "was creating unzip cmd (2)");

        /* put quotes around password */
        len = passwd_here - cmd;
        strcpy(cmd2, cmd);
        cmd2[len] = '\0';
        strcat(cmd2, "\"");
        strcat(cmd2, key);
        strcat(cmd2, "\"");
        strcat(cmd2, passwd_here + 3);

        /* Replace cmd with new string with password. */
        free(cmd);
        cmd = cmd2;
      } else {
        /* don't know where password should go, so don't include */
      }
    }

    status = system(cmd);

    free(unzip_path);
    unzip_path = NULL;
    free(cmd);

  } else {
    /* No -TT, so use local unzip command.
     *
     * Currently the password is passed on the command line.  We are looking
     * at passing a password to UnZip by other than the command line.
     */

    /* Here is where we need to check for the version of unzip the user
     * has.  If creating a Zip64 archive need UnZip 6 or later or may fail.
     */
    if (check_unzip_version("unzip") == 0)
      ZIPERR(ZE_TEST, zipfile);

    if (key) {
      char *k;

      /* Put quotes around password */
      if ((k = (char *)malloc(strlen(key) + 3)) == NULL)
          ziperr(ZE_MEM, "was creating unzip k (1)");
      strcpy(k, "\"");
      strcat(k, key);
      strcat(k, "\"");
      /* Run "unzip" with "-P pwd". */
      status = spawnlp(P_WAIT, "unzip", "unzip", verbose ? "-t" : "-tqq",
                       "-P", k, zipnam, NULL);
      free(k);
    } else {
      /* Run "unzip" without "-P pwd". */
      status = spawnlp(P_WAIT, "unzip", "unzip", verbose ? "-t" : "-tqq",
                       zipnam, NULL);
    }
#  ifdef __human68k__
    if (status == -1)
      zperror("unzip");
#  else
/*
 * unzip isn't in PATH range, assume an absolute path to zip in argv[0]
 * and hope that unzip is in the same directory.
 */
    if (status == -1) {
      p = MBSRCHR(zippath, '\\');
      path = MBSRCHR((p == NULL ? zippath : p), '/');
      if (path != NULL)
        p = path;
      if (p != NULL) {
        len = (int)(p - zippath) + 1;
        if ((path = malloc(len + sizeof("unzip.exe"))) == NULL)
          ziperr(ZE_MEM, "was creating unzip path");
        memcpy(path, zippath, len);
        strcpy(&path[len], "unzip.exe");

        if (check_unzip_version(path) == 0)
          ZIPERR(ZE_TEST, zipfile);

        if (key) {
          char *k;

          /* Put quotes around password */
          if ((k = (char *)malloc(strlen(key) + 3)) == NULL)
              ziperr(ZE_MEM, "was creating unzip k (2)");
          strcpy(k, "\"");
          strcat(k, key);
          strcat(k, "\"");

          status = spawnlp(P_WAIT, path, "unzip", verbose ? "-t" : "-tqq",
                           "-P", k, zipnam, NULL);
          free(k);
        } else {
          status = spawnlp(P_WAIT, path, "unzip", verbose ? "-t" : "-tqq",
                           zipnam, NULL);
        }
        free(path);
      }
      if (status == -1)
        zperror("unzip");
    }
#  endif /* ?__human68k__ */
  }
  free(zipnam);
  if (status != 0) {


# else /* (MSDOS && !__GO32__) || __human68k__ [else] */

  /* Non-MSDOS/Windows case */

  char *cmd;
  char *cp1;
  char *cp2;
  int keylen;
  int len;
  int result;
  char *cmd2 = NULL;

#  ifdef UNIX
#   define STRCPY_QU strcpy_qu          /* Add apostrophe quotation. */
#  else /* def UNIX */
#   define STRCPY_QU strcpy             /* Simply copy. */
#  endif /* def UNIX [else] */

  /* Tell picky compilers to shut up about unused variables */
  zippath = zippath;

  /* Calculate archive name length (including quotation). */
  len = strlen(zipname);                /* Archive name. */
#  ifdef UNIX
  /* 2013-10-18 SMS
   * Count apostrophes in the archive name.  With surrounding
   * apostrophes (added below), an embedded apostrophe must be
   * replaced by >'"'"'<
   * (apostrophe+quotation+apostrophe+quotation+apostrophe).  (Gack.)
   */
  for (cp1 = zipname; *cp1; cp1++)
  {
    if (*cp1 == '\'')
      len += 4;                         /* >'< -> >'"'"'<. */
  }
  /* Add two for the surrounding apostrophes. */
  len += 2;
#  endif /* def UNIX */

  /* Calculate password length (including quotation). */
  if (key)
  {
#  ifdef VMS
    keylen = strlen(key) + 2;                   /* Add 2 for quotation. */
#  else
    keylen = strlen(key);
#  endif

#  ifdef UNIX
    /* 2013-10-18 SMS
     * Count apostrophes in the password.  With surrounding
     * apostrophes (added below), an embedded apostrophe must be
     * replaced by >'"'"'<
     * (apostrophe+quotation+apostrophe+quotation+apostrophe).  (Gack.)
     */
    for (cp1 = key; *cp1; cp1++)
    {
      if (*cp1 == '\'')
        keylen += 4;                    /* >'< -> >'"'"'<. */
    }
    /* Add two for the surrounding apostrophes. */
    keylen += 2;
#  endif /* def UNIX */
  }

  if (unzip_path)
  {
    /* User gave us an unzip command (which may not use our UnZip). */
    char *brackets;             /* Pointer to "{}" or "{p}" marker. */

    /* Size of UnZip path + space + archive name (=len) + NUL.
     * (We might need two less, if "{}" is replaced in unzip_path.)
     */
    len += strlen(unzip_path) + 2;

    if ((cmd = malloc(len)) == NULL) {
      ziperr(ZE_MEM, "building command string for testing archive (1)");
    }

    /* Replace first {} with archive name.  If no {}, append name to string. */
    brackets = strstr(unzip_path, "{}");
    if (brackets)
    {
      /* Replace "{}" with the archive name. */
      len = brackets - unzip_path;      /* Length, pre-{}. */
      strncpy(cmd, unzip_path, len);    /* Command, pre-{}. */
      cmd[len] = '\0';                  /* NUL-terminate. */
#  if 0
      /* The user should supply a space here, if one is desired. */
      strcat(cmd, " ");
#  endif

      /* Append the archive name (apostrophe-quoted on Unix) here. */
      STRCPY_QU((cmd + strlen(cmd)), zipname);
#  if 0
      /* The user should supply a space here, if one is desired. */
      strcat(cmd, " ");
#  endif
      strcat(cmd, brackets + 2);        /* Command, post-{}. */
    }
    else
    {
      /* No {}, so append a space and the archive name. */
      strcpy(cmd, unzip_path);
      strcat(cmd, " ");

      /* Append the archive name (apostrophe-quoted on Unix) here. */
      STRCPY_QU((cmd + strlen(cmd)), zipname);
    }

    /* fprintf(stderr, " 1. cmd = >%s<\n", cmd); */

    /* Replace the first {p} with password given to zip.  If no password
     * was given (-P or -e), any {p} remains in the command string.  If
     * password but no {p}, we don't know where to put the password so
     * it is not passed to the unzip utility.
     */
    if (key)
    {
      brackets = strstr(cmd, "{p}");

      if (brackets) {
        /* Have {p}, so replace with password.
         * Allocate space for command plus (possibly quoted) password.
         * Removing {p} makes room for terminating NULL.
         */
        if ((cmd2 = (char *)malloc(strlen(cmd) + keylen)) == NULL)
          ziperr(ZE_MEM, "was creating unzip cmd (2)");
        len = brackets - cmd;
        strncpy(cmd2, cmd, len);                /* Command, pre-{p}. */
        STRCPY_QU((cmd2 + strlen(cmd2)), key);  /* Password (quoted?). */
        strcat(cmd2, (brackets + 3));           /* Command, post-{p}. */
        free(cmd);                              /* Free original cmd storage. */
        cmd = cmd2;                             /* Use revised cmd. */
      }
    }
    free(unzip_path);
    unzip_path = NULL;
  }
  else
  {
    /* No -TT, so use local unzip command */
    if (check_unzip_version("unzip") == 0)
      ZIPERR(ZE_TEST, zipfile);

    /* Size of UnZip command + -P pw + space + archive name (=len) + NUL. */
    if ((cmd = malloc(24 + keylen + len)) == NULL) {
      ziperr(ZE_MEM, "building command string for testing archive (2)");
    }

    strcpy(cmd, "unzip -t ");
#  ifdef QDOS
    strcat(cmd, "-Q4 ");
#  endif
    if (!verbose) strcat(cmd, "-qq ");

    /* fprintf( stderr, " 2.  cmd = >%s<\n", cmd); */

    if (key)
    {
      /* Add -P password. */
      strcat(cmd, "-P ");
#  ifdef VMS
      strcat(cmd, "\"");                        /* Quote password on VMS. */
#  endif
      STRCPY_QU((cmd + strlen(cmd)), key);      /* Password (quoted?). */
#  ifdef VMS
      strcat(cmd, "\"");                        /* Quote password on VMS. */
#  endif
      strcat(cmd, " ");
    }

    /* Append the archive name (apostrophe-quoted on Unix) here. */
    STRCPY_QU((cmd + strlen(cmd)), zipname);
  }

  /* fprintf(stderr, " 2p. cmd = >%s<\n", cmd); */

  result = system(cmd);
#  ifdef VMS
  /* Convert success severity to 0, others to non-zero. */
  result = ((result & STS$M_SEVERITY) != STS$M_SUCCESS);
#  endif /* def VMS */
  free(cmd);
  cmd = NULL;
  if (result) {
# endif /* (MSDOS && !__GO32__) || __human68k__ [else] */

    zfprintf(mesg, "test of %s FAILED\n", zipfile);
    ziperr(ZE_TEST, "original files unmodified");
  }
  if (noisy) {
    zfprintf(mesg, "test of %s OK\n", zipfile);
    fflush(mesg);
  }
  if (logfile) {
    zfprintf(logfile, "test of %s OK\n", zipfile);
    fflush(logfile);
  }
}

/* ------------- end test archive ------------- */
#endif /* ndef ZIP_DLL_LIB */


/* get_filters() is replaced by the following
local int get_filters(argc, argv)
*/

/* The filter patterns for options -x, -i, and -R are
   returned by get_option() one at a time, so use a linked
   list to store until all args are processed.  Then convert
   to array for processing.
 */

/* add a filter to the linked list */
local int add_filter(flag, pattern)
  int flag;
  char *pattern;
{
  char *iname;
  int pathput_save;
  FILE *fp;
  char *p = NULL;
  struct filterlist_struct *filter = NULL;

  /* should never happen */
  if (flag != 'R' && flag != 'x' && flag != 'i') {
    ZIPERR(ZE_LOGIC, "bad flag to add_filter");
  }
  if (pattern == NULL) {
    ZIPERR(ZE_LOGIC, "null pattern to add_filter");
  }

  if (pattern[0] == '@') {
    /* read file with 1 pattern per line */
    if (pattern[1] == '\0') {
      ZIPERR(ZE_PARMS, "missing file after @");
    }
    fp = fopen(pattern + 1, "r");
    if (fp == NULL) {
      sprintf(errbuf, "%c pattern file '%s'", flag, pattern);
      ZIPERR(ZE_OPEN, errbuf);
    }
    while ((p = getnam(fp)) != NULL) {
      if ((filter = (struct filterlist_struct *) malloc(sizeof(struct filterlist_struct))) == NULL) {
        ZIPERR(ZE_MEM, "adding filter (1)");
      }
      if (filterlist == NULL) {
        /* first filter */
        filterlist = filter;         /* start of list */
        lastfilter = filter;
      } else {
        lastfilter->next = filter;   /* link to last filter in list */
        lastfilter = filter;
      }

      /* always store full path for pattern matching */
      pathput_save = pathput;
      pathput = 1;
      iname = ex2in(p, 0, (int *)NULL);
      pathput = pathput_save;
      free(p);

      if (iname != NULL) {
        lastfilter->pattern = in2ex(iname);
        free(iname);
      } else {
        lastfilter->pattern = NULL;
      }
      lastfilter->flag = flag;
      pcount++;
      lastfilter->next = NULL;
    }
    fclose(fp);
  } else {
    /* single pattern */
    if ((filter = (struct filterlist_struct *) malloc(sizeof(struct filterlist_struct))) == NULL) {
      ZIPERR(ZE_MEM, "adding filter (2)");
    }
    if (filterlist == NULL) {
      /* first pattern */
      filterlist = filter;         /* start of list */
      lastfilter = filter;
    } else {
      lastfilter->next = filter;   /* link to last filter in list */
      lastfilter = filter;
    }

    /* always store full path for pattern matching */
    pathput_save = pathput;
    pathput = 1;
    iname = ex2in(pattern, 0, (int *)NULL);
    pathput = pathput_save;

    if (iname != NULL) {
       lastfilter->pattern = in2ex(iname);
       free(iname);
    } else {
      lastfilter->pattern = NULL;
    }
    lastfilter->flag = flag;
    pcount++;
    lastfilter->next = NULL;
  }

  return pcount;
}

/* convert list to patterns array */
local int filterlist_to_patterns()
{
  unsigned i;
  struct filterlist_struct *next = NULL;

  if (pcount == 0) {
    patterns = NULL;
    return 0;
  }
  if ((patterns = (struct plist *) malloc((pcount + 1) * sizeof(struct plist)))
      == NULL) {
    ZIPERR(ZE_MEM, "was creating pattern list");
  }

  for (i = 0; i < pcount && filterlist != NULL; i++) {
    switch (filterlist->flag) {
      case 'i':
        icount++;
        break;
      case 'R':
        Rcount++;
        break;
    }
    patterns[i].select = filterlist->flag;
    patterns[i].zname = filterlist->pattern;
    next = filterlist->next;
    free(filterlist);
    filterlist = next;
  }

  return pcount;
}


/* add a file argument to linked list */
local long add_name(filearg, verbatim)
  char *filearg;
  int verbatim;
{
  char *name = NULL;
  struct filelist_struct *fileentry = NULL;

  if ((fileentry = (struct filelist_struct *) malloc(sizeof(struct filelist_struct))) == NULL) {
    ZIPERR(ZE_MEM, "adding file (1)");
  }
  if ((name = malloc(strlen(filearg) + 1)) == NULL) {
    ZIPERR(ZE_MEM, "adding file (2)");
  }
  strcpy(name, filearg);
  fileentry->verbatim = verbatim;
  fileentry->next = NULL;
  fileentry->name = name;
  if (filelist == NULL) {
    /* first file argument */
    filelist = fileentry;         /* start of list */
    lastfile = fileentry;
  } else {
    lastfile->next = fileentry;   /* link to last name in list */
    lastfile = fileentry;
  }
  filearg_count++;

  return filearg_count;
}


/* add incremental archive path to linked list */
local long add_apath(path)
  char *path;
{
  char *name = NULL;
  struct filelist_struct *apath_entry = NULL;

  if ((apath_entry = (struct filelist_struct *) malloc(sizeof(struct filelist_struct))) == NULL) {
    ZIPERR(ZE_MEM, "adding incremental archive path entry");
  }
  if ((name = malloc(strlen(path) + 1)) == NULL) {
    ZIPERR(ZE_MEM, "adding incremental archive path");
  }
  strcpy(name, path);
  apath_entry->next = NULL;
  apath_entry->name = name;
  if (apath_list == NULL) {
    /* first apath */
    apath_list = apath_entry;         /* start of list */
    last_apath = apath_entry;
  } else {
    last_apath->next = apath_entry;   /* link to last apath in list */
    last_apath = apath_entry;
  }
  apath_count++;

  return apath_count;
}


/* Running Stats
   10/30/04 */

local int DisplayRunningStats()
{
  char tempstrg[100];

  if (mesg_line_started && !display_globaldots) {
    zfprintf(mesg, "\n");
    mesg_line_started = 0;
  }
  if (logfile_line_started) {
    zfprintf(logfile, "\n");
    logfile_line_started = 0;
  }
  if (display_volume) {
    if (noisy) {
      zfprintf(mesg, "%lu>%lu: ", current_in_disk + 1, current_disk + 1);
      mesg_line_started = 1;
    }
    if (logall) {
      zfprintf(logfile, "%lu>%lu: ", current_in_disk + 1, current_disk + 1);
      logfile_line_started = 1;
    }
  }
  if (display_counts) {
    if (noisy) {
      zfprintf(mesg, "%3ld/%3ld ", files_so_far, files_total - files_so_far);
      mesg_line_started = 1;
    }
    if (logall) {
      zfprintf(logfile, "%3ld/%3ld ", files_so_far, files_total - files_so_far);
      logfile_line_started = 1;
    }
  }
  if (display_bytes) {
    /* since file sizes can change as we go, use bytes_so_far from
       initial scan so all adds up */
    WriteNumString(bytes_so_far, tempstrg);
    if (noisy) {
      zfprintf(mesg, "[%4s", tempstrg);
      mesg_line_started = 1;
    }
    if (logall) {
      zfprintf(logfile, "[%4s", tempstrg);
      logfile_line_started = 1;
    }
    if (bytes_total >= bytes_so_far) {
      WriteNumString(bytes_total - bytes_so_far, tempstrg);
      if (noisy) {
        zfprintf(mesg, "/%4s] ", tempstrg);
      }
      if (logall) {
        zfprintf(logfile, "/%4s] ", tempstrg);
      }
    } else {
      WriteNumString(bytes_so_far - bytes_total, tempstrg);
      if (noisy) {
        zfprintf(mesg, "-%4s] ", tempstrg);
      }
      if (logall) {
        zfprintf(logfile, "-%4s] ", tempstrg);
      }
    }
  }
  if (display_time || display_est_to_go || display_zip_rate) {
    /* get current time */
    time(&clocktime);
  }
  if (display_time) {
    struct tm *now;

    now = localtime(&clocktime);

    /* avoid strftime() to keep old systems (like old VMS) happy */
    sprintf(errbuf, "%02d/%02d:%02d:%02d", now->tm_mday, now->tm_hour,
                                           now->tm_min, now->tm_sec);
    /* strftime(errbuf, 50, "%d/%X", now); */
    /* strcpy(errbuf, asctime(now)); */

    if (noisy) {
      zfprintf(mesg, errbuf);
      mesg_line_started = 1;
    }
    if (logall) {
      zfprintf(logfile, errbuf);
      logfile_line_started = 1;
    }
  }
#ifdef ENABLE_ENTRY_TIMING
  if (display_est_to_go || display_zip_rate) {
    /* get estimated time to go */
    uzoff_t bytes_to_go = bytes_total - bytes_so_far;
    zoff_t elapsed_time_in_usec;
    uzoff_t elapsed_time_in_10msec;
    uzoff_t bytes_per_second = 0;
    uzoff_t time_to_go = 0;
    uzoff_t secs;
    uzoff_t mins;
    uzoff_t hours;
    static uzoff_t lasttime = 0;
    uzoff_t files_to_go = 0;


    if (files_total > files_so_far)
      files_to_go = files_total - files_so_far;

    current_time = get_time_in_usec();
    lasttime = current_time;
    secs = current_time / 1000000;
    mins = secs / 60;
    secs = secs % 60;
    hours = mins / 60;
    mins = mins % 60;


    /* First time through we just finished the file scan and
       are starting to actually zip entries.  Use that time
       to calculate zipping speed. */
    if (start_zip_time == 0) {
      start_zip_time = current_time;
    }
    elapsed_time_in_usec = current_time - start_zip_time;
    elapsed_time_in_10msec = elapsed_time_in_usec / 10000;

    if (bytes_to_go < 1)
      bytes_to_go = 1;

    /* Seems best to wait about 90 msec for counts to stablize
       before displaying estimates of time to go. */
    if (display_est_to_go && elapsed_time_in_10msec > 8) {
      /* calculate zipping rate */
      bytes_per_second = (bytes_so_far * 100) / elapsed_time_in_10msec;
      /* if going REALLY slow, assume at least 1 byte per second */
      if (bytes_per_second < 1) {
        bytes_per_second = 1;
      }

      /* calculate estimated time to go based on rate */
      time_to_go = bytes_to_go / bytes_per_second;

      /* add estimate for console listing of entries */
      time_to_go += files_to_go / 40;

      secs = (time_to_go % 60);
      time_to_go /= 60;
      mins = (time_to_go % 60);
      time_to_go /= 60;
      hours = time_to_go;

      if (hours > 10) {
        /* show hours */
        sprintf(errbuf, "<%3dh to go>", (int)hours);
      } else if (hours > 0) {
        /* show hours */
        float h = (float)((int)hours + (int)mins / 60.0);
        sprintf(errbuf, "<%3.1fh to go>", h);
      } else if (mins > 10) {
        /* show minutes */
        sprintf(errbuf, "<%3dm to go>", (int)mins);
      } else if (mins > 0) {
        /* show minutes */
        float m = (float)((int)mins + (int)secs / 60.0);
        sprintf(errbuf, "<%3.1fm to go>", m);
      } else {
        /* show seconds */
        int s = (int)mins * 60 + (int)secs;
        sprintf(errbuf, "<%3ds to go>", s);
      }
      /* sprintf(errbuf, "<%02d:%02d:%02d to go> ", hours, mins, secs); */
      if (noisy) {
        zfprintf(mesg, errbuf);
        mesg_line_started = 1;
      }
      if (logall) {
        zfprintf(logfile, errbuf);
        logfile_line_started = 1;
      }
    }
# if 0
    else {
      /* first time have no data */
      sprintf(errbuf, "<>");
      if (noisy) {
        fprintf(mesg, "%s ", errbuf);
        mesg_line_started = 1;
      }
      if (logall) {
        fprintf(logfile, "%s ", errbuf);
        logfile_line_started = 1;
      }
    }
# endif

    if (display_zip_rate && elapsed_time_in_usec > 0) {
      /* calculate zipping rate */
      bytes_per_second = (bytes_so_far * 100) / elapsed_time_in_10msec;
      /* if going REALLY slow, assume at least 1 byte per second */
      if (bytes_per_second < 1) {
        bytes_per_second = 1;
      }

      WriteNumString(bytes_per_second, tempstrg);
      sprintf(errbuf, "{%4sB/s}", tempstrg);
      if (noisy) {
        zfprintf(mesg, errbuf);
        mesg_line_started = 1;
      }
      if (logall) {
        zfprintf(logfile, errbuf);
        logfile_line_started = 1;
      }
    }
# if 0
    else {
      /* first time have no data */
      sprintf(errbuf, "{}");
      if (noisy) {
        fprintf(mesg, "%s ", errbuf);
        mesg_line_started = 1;
      }
      if (logall) {
        fprintf(logfile, "%s ", errbuf);
        logfile_line_started = 1;
      }
    }
# endif
  }
#endif /* ENABLE_ENTRY_TIMING */
  if (noisy)
      fflush(mesg);
  if (logall)
      fflush(logfile);

  return 0;
}

local int BlankRunningStats()
{
  if (display_volume) {
    if (noisy) {
      zfprintf(mesg, "%lu>%lu: ", current_in_disk + 1, current_disk + 1);
      mesg_line_started = 1;
    }
    if (logall) {
      zfprintf(logfile, "%lu>%lu: ", current_in_disk + 1, current_disk + 1);
      logfile_line_started = 1;
    }
  }
  if (display_counts) {
    if (noisy) {
      zfprintf(mesg, "   /    ");
      mesg_line_started = 1;
    }
    if (logall) {
      zfprintf(logfile, "   /    ");
      logfile_line_started = 1;
    }
  }
  if (display_bytes) {
    if (noisy) {
      zfprintf(mesg, "     /      ");
      mesg_line_started = 1;
    }
    if (logall) {
      zfprintf(logfile, "     /      ");
      logfile_line_started = 1;
    }
  }
  if (noisy)
      fflush(mesg);
  if (logall)
      fflush(logfile);

  return 0;
}


#ifdef IZ_CRYPT_ANY
# ifndef ZIP_DLL_LIB
int encr_passwd(modeflag, pwbuf, size, zfn)
int modeflag;
char *pwbuf;
int size;
ZCONST char *zfn;
{
    char *prompt;

    /* Tell picky compilers to shut up about unused variables */
    zfn = zfn;

    prompt = (modeflag == ZP_PW_VERIFY) ?
              "Verify password: " : "Enter password: ";

    if (getp(prompt, pwbuf, size) == NULL) {
      ziperr(ZE_PARMS, "stderr is not a tty");
    }
    return IZ_PW_ENTERED;
}

/* This version should be sufficient for Zip.  Zip does not track the
   Zip file name so that parameter is not needed and, in fact, is
   misleading. */
int simple_encr_passwd(modeflag, pwbuf, bufsize)
  int modeflag;
  char *pwbuf;
  size_t bufsize; /* max password length  + 1 (includes NULL) */
{
    char *prompt;

    prompt = (modeflag == ZP_PW_VERIFY) ?
              "Verify password: " : "Enter password: ";

    if (getp(prompt, pwbuf, bufsize) == NULL) {
      ziperr(ZE_PARMS, "stderr is not a tty");
    }
    if (strlen(pwbuf) >= (bufsize - 1)) {
      return -1;
    }
    return 0;
}

# endif /* !ZIP_DLL_LIB */
#else /* def IZ_CRYPT_ANY [else] */
int encr_passwd(modeflag, pwbuf, size, zfn)
int modeflag;
char *pwbuf;
int size;
ZCONST char *zfn;
{
    /* Tell picky compilers to shut up about unused variables */
    modeflag = modeflag; pwbuf = pwbuf; size = size; zfn = zfn;

    return ZE_LOGIC;    /* This function should never be called! */
}

/* This version should be sufficient for Zip. */
/* Version when no encryption is included. */
int simple_encr_passwd(modeflag, pwbuf, bufsize)
  int modeflag;
  char *pwbuf;
  int bufsize;
{
    /* Tell picky compilers to shut up about unused variables */
    modeflag = modeflag; pwbuf = pwbuf; bufsize = bufsize;

    return ZE_LOGIC;    /* This function should never be called! */
}

#endif /* def IZ_CRYPT_ANY [else] */


/* rename a split
 * A split has a tempfile name until it is closed, then
 * here rename it as out_path the final name for the split.
 */
int rename_split(temp_name, out_path)
  char *temp_name;
  char *out_path;
{
  int r;
  /* Replace old zip file with new zip file, leaving only the new one */
  if ((r = replace(out_path, temp_name)) != ZE_OK)
  {
    zipwarn("new zip file left as: ", temp_name);
    free((zvoid *)tempzip);
    tempzip = NULL;
    ZIPERR(r, "was replacing split file");
  }
  if (zip_attributes) {
    setfileattr(out_path, zip_attributes);
  }
  return ZE_OK;
}


int set_filetype(out_path)
  char *out_path;
{
#ifdef __BEOS__
  /* Set the filetype of the zipfile to "application/zip" */
  setfiletype( out_path, "application/zip" );
#endif

#ifdef __ATHEOS__
  /* Set the filetype of the zipfile to "application/x-zip" */
  setfiletype(out_path, "application/x-zip");
#endif

#ifdef MACOS
  /* Set the Creator/Type of the zipfile to 'IZip' and 'ZIP ' */
  setfiletype(out_path, 'IZip', 'ZIP ');
#endif

#ifdef RISCOS
  /* Set the filetype of the zipfile to &DDC */
  setfiletype(out_path, 0xDDC);
#endif
  return ZE_OK;
}


/* datetime() - Convert "-t[t]" value string to DOS date/time.
 * Supply current date-time for date to use with time-only values.
 * 2013-12-17 SMS.
 */

/* Modified to acccept the following formats:
 *   ddmmyyyy
 *   ddmmyyyy:HH:MM
 *   ddmmyyyy:HH:MM:SS
 *   yyyy-mm-dd
 *   yyyy-mm-dd:HH:MM
 *   yyyy-mm-dd:HH:MM:SS
 *   :HH:MM                (times alone need leading :)
 *   :HH:MM:SS
 */

#define DT_BAD ((ulg)-1)        /* Bad return value. */

static ulg datetime(arg, curtime)
  ZCONST char *arg;
  ZCONST time_t curtime;
{
  int yr;                               /* Year. */
  int mo;                               /* Month. */
  int dy;                               /* Day (of month). */
  int hr;                               /* Hour. */
  int mn;                               /* Minute. */
  int sc;                               /* Second. */

  ulg dt;                               /* Return value. */
  int itm;                              /* sscan() item count. */
  char *lhp;                            /* Last hyphen. */
  char *fcp;                            /* First colon. */
  char xs[4];                           /* Excess characters. */
  char myarg[20];                       /* Local copy of arg. */

  dt = 0;
  yr = 0;
  mo = 0;
  dy = 0;
  hr = 0;
  mn = 0;
  sc = 0;

  if (strlen(arg) > 19)
  {
    dt = DT_BAD;                /* Longer than "yyyy-mm-dd:HH:MM:SS". */
  }
  else
  {
    strcpy(myarg, arg);                 /* Local copy of argument. */
    fcp = strchr(myarg, ':');           /* First colon. */
    lhp = strrchr(myarg, '-');          /* Last hyphen. */

    if ((fcp != NULL) && (lhp != NULL) && (lhp > fcp))
    {
      dt = DT_BAD;              /* Last hyphen must precede first colon. */
    }
    else if (lhp == NULL)
    {
      /* no hyphens */
      if (fcp == NULL)
      {
        /* No hyphen, no colon.  Look for "mmddyyyy". */
        itm = sscanf(myarg, "%2d%2d%4d%2s", &mo, &dy, &yr, xs);
        if (itm != 3)
        {
          dt = DT_BAD;          /* Excess characters, or not "mmddyyyy". */
        }
      }
      else
      {
        /* colon found, but no hyphens */
        if (fcp > myarg) {
          /* stuff before first colon, assume date in mmddyyyy format */
          *fcp = '\0';  /* NULL terminate date part */
          fcp++;        /* time part is one after that */
          itm = sscanf(myarg, "%2d%2d%4d%2s", &mo, &dy, &yr, xs);
          if (itm != 3)
          {
            dt = DT_BAD;        /* Excess characters, or not "mmddyyyy". */
          }
        }
        else
        {
          /* fcp == myarg, so colon starts myarg, assume time only */
          fcp++;                /* skip leading colon and point to time */
        }
      }
    }
    else
    {
      /* Found a hyphen.  Look for "yyyy-mm-dd[:HH[:MM[:SS]]]". */
      if (fcp != NULL)
      {
        /* Date ends at first colon.  Time begins after. */
        *(fcp++) = '\0';
      }
      itm = sscanf(myarg, "%4d-%2d-%2d%2s", &yr, &mo, &dy, xs);
      if (itm != 3)
      {
        dt = DT_BAD;            /* Excess characters, or not "yyyy-mm-dd". */
      }
    }
  }

  if (dt == 0)
  {
    if (fcp != NULL)
    {
      /* Look for "HH:MM[:SS]". */
      itm = sscanf(fcp, "%2d:%2d:%2d%2s", &hr, &mn, &sc, xs);
      if (itm == 2)
      { /* Not "HH:MM:SS".  Try "HH:MM"? */
        hr = 0;
        mn = 0;
        sc = 0;
        itm = sscanf(fcp, "%2d:%2d%2s", &hr, &mn, xs);
        if (itm != 2)
        {
          dt = DT_BAD;          /* Excess characters, or not "HH:MM". */
        }
      }
      else if (itm != 3)
      {
        dt = DT_BAD;            /* Excess characters, or not "HH:MM:SS". */
      }
    }
  }

  if (dt == 0)
  {
    if ((yr <= 0) || (mo <= 0) || (dy <= 0))
    {
      time_t timet;
      struct tm *ltm;

      timet = curtime;
      ltm = localtime(&timet);
      yr = ltm->tm_year + 1900;
      mo = ltm->tm_mon + 1;
      dy = ltm->tm_mday;
    }
    else if ((yr < 1980) || (yr > 2107) ||      /* Year. */
     (mo < 1) || (mo > 12) ||                   /* Month. */
     (dy < 1) || (dy > 31))                     /* Day (of month). */
    {
      dt = DT_BAD;                              /* Invalid date. */
    }
  }

  if ((dt == 0) &&
   ((hr < 0) || (hr > 23) ||            /* Hour. */
   (mn < 0) || (mn > 59) ||             /* Minute. */
   (sc < 0) || (sc > 59)))              /* Second. */
  {
    dt = DT_BAD;                        /* Invalid time. */
  }

  if (dt == 0)
  {
    dt = dostime(yr, mo, dy, hr, mn, sc);
  }
  return dt;
}



/* --------------------------------------------------------------------- */


#ifdef WIN32
/* write_console
 *
 * Write a string directly to the console.  Supports UTF-8, if
 * console supports it (codepage 65001).
 */
DWORD write_console(FILE *outfile, char *instring)
{
  DWORD charswritten;

  if (isatty(fileno(outfile))) {
    WriteConsoleA(
              GetStdHandle(STD_OUTPUT_HANDLE), /* in  HANDLE hConsoleOutput */
              instring,                        /* in  const VOID *lpBuffer */
              strlen(instring),                /* in  DWORD nNumberOfCharsToWrite */
              &charswritten,                   /* out LPDWORD lpNumberOfCharsWritten */
              NULL);                           /* reserved */
    return charswritten;
  }
  else
  {
    return fprintf(outfile, "%s", instring);
  }
}

DWORD write_consolew(FILE *outfile, wchar_t *instringw)
{
  DWORD charswritten;

  if (isatty(fileno(outfile))) {
    WriteConsoleW(
              GetStdHandle(STD_OUTPUT_HANDLE), /* in  HANDLE hConsoleOutput */
              instringw,                       /* in  const VOID *lpBuffer */
              wcslen(instringw),               /* in  DWORD nNumberOfCharsToWrite */
              &charswritten,                   /* out LPDWORD lpNumberOfCharsWritten */
              NULL);                           /* reserved */
    return charswritten;
  }
  else
  {
    return fprintf(outfile, "%S", instringw);
  }
}
#endif

#ifdef UNICODE_SUPPORT

/* Unicode_Tests
 *
 * Perform select tests of Unicode conversions and display results.
 */
int Unicode_Tests()
{
  {
#if 0
    char *instring = "Test - ςερτυθιοπ";
#endif
    char *instring = "Test - \xcf\x82\xce\xb5\xcf\x81\xcf\x84\xcf\x85\xce\xb8\xce\xb9\xce\xbf\xcf\x80";
    wchar_t *outstring;
    char *restored_string;
    int unicode_wchar = 0;
    int have_iconv = 0;
    int no_nl_langinfo = 0;
    int wchar_size = sizeof(wchar_t);
    int unicode_file_scan = 0;

# ifdef UNICODE_WCHAR
    unicode_wchar = 1;
# endif
# ifdef HAVE_ICONV
    have_iconv = 1;
# endif
# ifdef NO_NL_LANGINFO
    no_nl_langinfo = 1;
# endif
# ifdef UNICODE_FILE_SCAN
    unicode_file_scan = 1;
# endif

    printf("\n");
    printf("localename:   %s\n", localename);
    printf("charsetname:  %s\n", charsetname);
    printf("\n");

    if (unicode_wchar)
      printf("UNICODE_WCHAR defined\n");
    if (have_iconv)
      printf("HAVE_ICONV defined\n");
    if (no_nl_langinfo)
      printf("NO_NL_LANGINFO defined\n");
    if (unicode_file_scan)
      printf("UNICODE_FILE_SCAN defined\n");
    if (using_utf8)
      printf("UTF-8 Native\n");
    printf("sizeof(wchar_t) = %d bytes\n", wchar_size);
    printf("\n");

#ifdef WIN32
    printf("Using Windows API for character set conversions\n");
#else
# ifdef UNIX
    if (unicode_wchar)
      printf("Using wide (wchar_t) calls for character set translations\n");
    else if (have_iconv)
      printf("Using iconv for character set translations\n");
    if (wchar_size == 4)
      printf("Using Zip's internal functions for UTF-8 <--> wchar_t conversions\n");
    else if (have_iconv)
      printf("Using iconv for UTF-8 <--> wchar_t conversions\n");
# endif
#endif
    printf("\n");

    printf("Test utf8_to_wchar_string():\n");

#if defined(WIN32) && !defined(ZIP_DLL_LIB)
    printf("  instring (UTF-8) via write_console: '");
    write_console(stdout, instring);
#else
    printf("  instring (UTF-8) = '%s'", instring);
#endif
    printf("\n");
    {
      int i;
      printf("  instring bytes:   (");
      for (i = 0; instring[i]; i++) {
        printf(" %02x", (unsigned char)instring[i]);
      }
      printf(")\n");
    }
    printf("\n");
    printf(
      "  (Windows requires Lucida Console font and codepage 65001 to see (some) UTF-8.)\n");
    printf("\n");

    outstring = utf8_to_wchar_string(instring);

    if (outstring == NULL) {
      printf("outstring null\n");

      printf("  did not try to restore string\n");
    }
    else
    {
      int i;

#if defined(WIN32) && !defined(ZIP_DLL_LIB)
      printf("  outstring (wchar_t) via write_consolew:  ");
      write_consolew(stdout, outstring);
#else
      printf("  outstring (wchar_t) = '%S'", outstring);
#endif
      printf("\n");
      printf("\n");
      printf("  outstring words:  (");
      for (i = 0; outstring[i]; i++) {
        printf(" %04x", outstring[i]);
      }
      printf(")\n");
      printf("\n");

      printf("Test wchar_to_utf8_string():\n");

      restored_string = wchar_to_utf8_string(outstring);

      if (restored_string == NULL)
        printf("  restored string NULL\n");
      else
      {
#if defined(WIN32) && !defined(ZIP_DLL_LIB)
        printf("  restored string (UTF-8) via write_console:  ");
        write_console(stdout, restored_string);
#else
        printf("  restored string (UTF-8) = '%s'", restored_string);
#endif
        printf("\n");
        {
          int i;
          printf("  restored bytes:   (");
          for (i = 0; instring[i]; i++) {
            printf(" %02x", (unsigned char)instring[i]);
          }
          printf(")\n");
        }
      }
    }

    if (restored_string)
      free(restored_string);
    if (outstring)
      free(outstring);

    printf("\n");
  }

  {
    char *utf8_string = "Test - ςερτυθιοπ";
    char *local_string;
    char *restored_string;

    printf("Test utf8_to_local_string:\n");

#if defined(WIN32) && !defined(ZIP_DLL_LIB)
    printf("  UTF-8 string via write_console:  ");
    write_console(stdout, utf8_string);
    printf("\n");
#else
    printf("  UTF-8 string:  '%s'", utf8_string);
    printf("\n");
#endif
    {
      int i;
      printf("  UTF-8 string bytes:   (");
      for (i = 0; utf8_string[i]; i++) {
        printf(" %02x", (unsigned char)utf8_string[i]);
      }
      printf(")\n");
    }
    printf("\n");

    local_string = utf8_to_local_string(utf8_string);
#if defined(WIN32) && !defined(ZIP_DLL_LIB)
    printf("  local string via write_console:  ");
    write_console(stdout, local_string);
    printf("\n");
#else
    printf("  local string:  '%s'", local_string);
    printf("\n");
#endif
    {
      int i;
      printf("  local string bytes:     (");
      for (i = 0; local_string[i]; i++) {
        printf(" %02x", (unsigned char)local_string[i]);
      }
      printf(")\n");
    }

    printf("\n");
    printf("Test local_to_utf8_string:\n");

    restored_string = local_to_utf8_string(local_string);
#if defined(WIN32) && !defined(ZIP_DLL_LIB)
    printf("  restored string via write_console:  ");
    write_console(stdout, restored_string);
    printf("\n");
#else
    printf("  restored UTF-8 string:  '%s'", restored_string);
    printf("\n");
#endif
    {
      int i;
      printf("  restored UTF-8 bytes:   (");
      for (i = 0; restored_string[i]; i++) {
        printf(" %02x", (unsigned char)restored_string[i]);
      }
      printf(")\n");
    }

    if (local_string)
      free(local_string);
    if (restored_string)
      free(restored_string);

    printf("\n");
  }

  return 0;
}
#endif

/* --------------------------------------------------------------------- */


/* This function processes the -n (suffixes) option.  The code was getting too
   lengthy for the options switch. */
void suffixes_option(char *value)
{
  /* Value format: "method[-lvl]=sfx1:sfx2:sfx3...". */

#define MAX_SUF 4000

  int i;
  int j;  /* Place in suffix array of selected compression method. */
  int lvl;
  char *sfx_list;
  int k1;
  int k2;
  int k;
  int jj;
  int kk1;
  int kk2;
  int kk;
  int delta;
  int slen;
  char c;
  char *s;
  char suf[MAX_SUF + 1];
  char suf2[MAX_SUF + 1];
  char *new_list;
  int new_list_size;
  int merged = 0;

  lvl = -1;                   /* Assume the default level. */
  sfx_list = value;

  /* Now "*" in suffix list merges in the existing list.  There is no
      way to remove suffixes from an existing list, except to start
      the list from scratch.  It may make sense to implement the
      chmod approach, using '+' to add a suffix, "-n deflate=+.txt",
      and '-' to delete it. */

  /* Find the first special character in value. */
  for (i = 0; value[i] != '\0'; i++)
  {
    if ((value[i] == '=') || (value[i] == ':') || (value[i] == ';'))
      break;
  }

  j = 0;                      /* Default = STORE. */
  if (value[i] == '=')
  {
    /* Found "method[-lvl]=".  Replace "=" with NUL. */
    value[i] = '\0';

    /* Look for "-n" level specifier.
      *  Must be "--" or "-0" - "-9".
      */
    if ((value[i - 2] == '-') &&
        ((value[i - 1] == '-') ||
          ((value[i - 1] >= '0') && (value[i - 1] <= '9'))))
    {
      if (value[i - 1] != '-')
      {
        /* Some explicit level, 0-9. */
        lvl = value[i - 1] - '0';
      }
      value[i - 2] = '\0';
    }

    /* Check for a match in the method-by-suffix array. */
    for (j = 0; mthd_lvl[j].method >= 0; j++)
    {
      /* Matching only first char - if 2 methods start with same
          char, this needs to be updated. */
      if (abbrevmatch(mthd_lvl[j].method_str, value, CASE_INS, 1))
      {
        /* Matched method name. */
        sfx_list = value + (i + 1);     /* Stuff after "=". */
        break;
      }
    }
    if (mthd_lvl[j].method < 0)
    {
      sprintf(errbuf, "Invalid compression method: %s", value);
      free(value);
      ZIPERR(ZE_PARMS, errbuf);
    }
  } /* = */

  /* Check for a valid "-n" level value, and store it. */
  if (lvl < 0)
  {
    /* Restore the default level setting. */
    mthd_lvl[ j].level_sufx = -1;
  }
  else
  {
    if ((j == 0) && (lvl != 0))
    {
      /* Setting STORE to other than 0 */
      sprintf( errbuf, "Can't set default level for compression method \"%s\" to other than 0: %d",
        mthd_lvl[ j].method_str, lvl);
      free(value);
      ZIPERR(ZE_PARMS, errbuf);
    }
    else if ((j != 0) && (lvl == 0))
    {
      /* Setting something else to 0 */
      sprintf( errbuf, "Can't set default level for compression method \"%s\" to 0",
        mthd_lvl[ j].method_str);
      free(value);
      ZIPERR(ZE_PARMS, errbuf);
    }
    else
    {
      mthd_lvl[j].level_sufx = lvl;
    }
  }

  /* Make freeable copy of suffix list */
  new_list_size = strlen(sfx_list);
  if ((new_list = malloc(new_list_size + 1)) == NULL) {
    ZIPERR(ZE_MEM, "copying suffix list");
  }
  strcpy(new_list, sfx_list);
  free(value);
  sfx_list = new_list;

  /* Parse suffix list to see if * is in there. */
  k1 = 0;
  while ((c = sfx_list[k1])) {
    if (c == ':' || c == ';') {
      k1++;
      continue;
    }
    /* k1 at first char of suffix */
    k2 = k1 + 1;
    while ((c = sfx_list[k2])) {
      if (c == ':' || c == ';') {
        break;
      }
      k2++;
    }
    /* k2 - 1 at end of suffix */
    if (k2 - k1 != 1 || sfx_list[k1] != '*') {
      /* not suffix "*" */
      k1 = k2;
      continue;
    }
    /* found suffix "*" */
    if (merged) {
      /* more than one '*' found */
      ZIPERR(ZE_PARMS, "multiple '*' not allowed in suffix list");
    }
    if (mthd_lvl[j].suffixes) {
      /* replace "*" with current list */
      new_list_size = strlen(sfx_list);
      new_list_size += strlen(mthd_lvl[j].suffixes);
      if ((new_list = malloc(new_list_size + 1)) == NULL) {
        ZIPERR(ZE_MEM, "merging suffix lists");
      }
      for (k = 0; k < k1; k++) {
        new_list[k] = sfx_list[k];
      }
      new_list[k] = '\0';
      strcat(new_list, mthd_lvl[j].suffixes);
      strcat(new_list, sfx_list + k2);
      k2 += strlen(mthd_lvl[j].suffixes);
      k1 = k2;
      merged = 1;
      free(sfx_list);
      sfx_list = new_list;
    } else {
      /* no existing list to merge, just remove "*:" */
      for (k = k1; sfx_list[k + 1] && sfx_list[k + 2]; k++) {
        sfx_list[k] = sfx_list[k + 2];
      }
      sfx_list[k] = '\0';
    }
  }

  /* Set suffix list value. */
  if (*sfx_list == '\0') {   /* Store NULL for an empty list. */
    free(sfx_list);
    sfx_list = NULL;         /* (Worry about white space?) */
  }

  if (mthd_lvl[j].suffixes)
    free(mthd_lvl[j].suffixes);
  mthd_lvl[j].suffixes = sfx_list;

  /* ------------------ */

  /* Remove these suffixes from other methods */

  /* Run through the new suffix list */
  k1 = 0;
  while ((c = sfx_list[k1]))
  {
    if (c == ':' || c == ';') {
      k1++;
      continue;
    }
    /* k1 at first char of suffix */
    k2 = k1 + 1;
    while ((c = sfx_list[k2])) {
      if (c == ':' || c == ';') {
        break;
      }
      k2++;
    }
    /* k2 - 1 at end of suffix */
    delta = k2 - k1 + 1;
    if (delta > MAX_SUF) {
      sprintf(errbuf, "suffix too big (max %d)", MAX_SUF);
      ZIPERR(ZE_PARMS, errbuf);
    }
    for (k = k1; k < k2; k++) {
      suf[k - k1] = sfx_list[k];
    }
    suf[k - k1] = '\0';

    /* See if this suffix is listed for other methods */
    for (jj = 0; mthd_lvl[jj].method >= 0; jj++)
    {
      printf("jj = %d\n", jj);
      /* Only other global methods */
      if (j != jj && mthd_lvl[jj].suffixes) {
        kk1 = 0;
        s = mthd_lvl[jj].suffixes;

        while ((c = s[kk1])) {
          if (c == ':' || c == ';') {
            kk1++;
            continue;
          }
          /* kk1 at first char of suffix */
          kk2 = kk1 + 1;
          while ((c = s[kk2])) {
            if (c == ':' || c == ';') {
              break;
            }
            kk2++;
          }
          /* kk2 - 1 at end of suffix */
          delta = kk2 - kk1 + 1;
          if (delta > MAX_SUF) {
            sprintf(errbuf, "suffix too big (max %d)", MAX_SUF);
            ZIPERR(ZE_PARMS, errbuf);
          }
          for (kk = kk1; kk < kk2; kk++) {
            suf2[kk - kk1] = s[kk];
          }
          suf2[kk - kk1] = '\0';

          printf("\n  suf: '%s'  suf2: '%s'\n", suf, suf2);
          if (namecmp(suf, suf2) == 0) {
            /* found it, remove it */
            printf("    before mthd_lvl[%d].suffixes = '%s'\n", jj, mthd_lvl[jj].suffixes);
            slen = strlen(s);
            for (kk = kk1; kk + delta < slen; kk++) {
              s[kk] = s[kk + delta];
            }
            s[kk] = '\0';
            printf("    after  mthd_lvl[%d].suffixes = '%s'\n", jj, mthd_lvl[jj].suffixes);
          } else {
            /* not the one, skip it */
            kk1 = kk2;
          }
        } /* while */

        /* Set suffix list value. */
        if (*s == '\0') {   /* Store NULL for an empty list. */
          free(s);
          s = NULL;
        }
      }
    } /* for each method */
    k1 = k2;
  } /* while sfx_list */

  /* ------------------ */

}


/*
  -------------------------------------------------------
  Command Line Options
  -------------------------------------------------------

  Valid command line options.

  The function get_option() uses this table to check if an
  option is valid and if it takes a value (also called an
  option argument).  To add an option to zip just add it
  to this table and add a case in the main switch to handle
  it.  If either shortopt or longopt not used set to "".

   The fields:
       shortopt     - short option name (1 or 2 chars)
       longopt      - long option name
       value_type   - see zip.h for constants
       negatable    - option is negatable with trailing -
       ID           - unsigned long int returned for option
       name         - short description of option which is
                        returned on some errors and when options
                        are listed with -so option, can be NULL
*/

/* Single-letter option IDs are set to the shortopt char (like 'a').  For
   multichar short options set to arbitrary unused constant (like
   o_aa). */
#define o_aa            0x101
#define o_as            0x102
#define o_AC            0x103
#define o_AF            0x104
#define o_AS            0x105
#define o_BC            0x106
#define o_BD            0x107
#define o_BL            0x108
#define o_BN            0x109
#define o_BT            0x110
#define o_cd            0x111
#define o_C2            0x112
#define o_C5            0x113
#define o_Cl            0x114
#define o_Cu            0x115
#define o_db            0x116
#define o_dc            0x117
#define o_dd            0x118
#define o_de            0x119
#define o_des           0x120
#define o_df            0x121
#define o_DF            0x122
#define o_DI            0x123
#define o_dg            0x124
#define o_dr            0x125
#define o_ds            0x126
#define o_dt            0x127
#define o_du            0x128
#define o_dv            0x129
#define o_EA            0x130
#define o_FF            0x131
#define o_FI            0x132
#define o_FS            0x133
#define o_h2            0x134
#define o_ic            0x135
#define o_jj            0x136
#define o_la            0x137
#define o_lf            0x138
#define o_lF            0x139
#define o_li            0x140
#define o_ll            0x141
#define o_lu            0x142
#define o_mm            0x143
#define o_MM            0x144
#define o_MV            0x145
#define o_nw            0x146
#define o_pa            0x147
#define o_pn            0x148
#define o_pp            0x149
#define o_ps            0x150
#define o_pt            0x151
#define o_RE            0x152
#define o_sb            0x153
#define o_sc            0x154
#define o_sC            0x155
#define o_sd            0x156
#define o_sf            0x157
#define o_sF            0x158
#define o_si            0x159
#define o_so            0x160
#define o_sp            0x161
#define o_sP            0x162
#define o_ss            0x163
#define o_st            0x164
#define o_su            0x165
#define o_sU            0x166
#define o_sv            0x167
#define o_tn            0x168
#define o_tt            0x169
#define o_TT            0x170
#define o_UN            0x171
#define o_UT            0x172
#define o_ve            0x173
#define o_vq            0x174
#define o_VV            0x175
#define o_wl            0x176
#define o_ws            0x177
#define o_ww            0x178
#define o_yy            0x179
#define o_z64           0x180
#define o_atat          0x181
#define o_vn            0x182
#define o_et            0x183
#define o_exex          0x184


/* the below is mainly from the old main command line
   switch with a growing number of changes */
struct option_struct far options[] = {
  /* short longopt        value_type        negatable        ID    name */
    {"0",  "store",       o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '0',  "store"},
    {"1",  "compress-1",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '1',  "compress 1"},
    {"2",  "compress-2",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '2',  "compress 2"},
    {"3",  "compress-3",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '3',  "compress 3"},
    {"4",  "compress-4",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '4',  "compress 4"},
    {"5",  "compress-5",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '5',  "compress 5"},
    {"6",  "compress-6",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '6',  "compress 6"},
    {"7",  "compress-7",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '7',  "compress 7"},
    {"8",  "compress-8",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '8',  "compress 8"},
    {"9",  "compress-9",  o_OPT_EQ_VALUE,   o_NOT_NEGATABLE, '9',  "compress 9"},
    {"A",  "adjust-sfx",  o_NO_VALUE,       o_NOT_NEGATABLE, 'A',  "adjust self extractor offsets"},
#if defined(WIN32)
    {"AC", "archive-clear", o_NO_VALUE,     o_NOT_NEGATABLE, o_AC, "clear DOS archive bit of included files"},
    {"AS", "archive-set", o_NO_VALUE,       o_NOT_NEGATABLE, o_AS, "include only files with archive bit set"},
#endif
    {"AF", "argfiles",   o_NO_VALUE,       o_NEGATABLE,     o_AF, "enable (default)/disable @argfiles"},
#ifdef EBCDIC
    {"a",  "ascii",       o_NO_VALUE,       o_NOT_NEGATABLE, 'a',  "to ASCII"},
    {"aa", "all-ascii",   o_NO_VALUE,       o_NOT_NEGATABLE, o_aa, "all files ASCII text (skip bin check)"},
#endif /* EBCDIC */
#if defined( UNIX) && defined( __APPLE__)
    {"as", "sequester",   o_NO_VALUE,       o_NEGATABLE,     o_as, "sequester AppleDouble files in __MACOSX"},
#endif /* defined( UNIX) && defined( __APPLE__) */
#ifdef CMS_MVS
    {"B",  "binary",      o_NO_VALUE,       o_NOT_NEGATABLE, 'B',  "binary"},
#endif /* CMS_MVS */
#ifdef TANDEM
    {"B",  "",            o_NUMBER_VALUE,   o_NOT_NEGATABLE, 'B',  "nsk"},
#endif
#ifdef BACKUP_SUPPORT
    {"BC", "backup-control",o_REQUIRED_VALUE,o_NOT_NEGATABLE, o_BC,"dir for backup control file"},
    {"BD", "backup-dir",    o_REQUIRED_VALUE,o_NOT_NEGATABLE, o_BD,"dir for backup archive"},
    {"BL", "backup-log",    o_OPT_EQ_VALUE,  o_NOT_NEGATABLE, o_BL,"dir for backup log file"},
    {"BN", "backup-name",   o_REQUIRED_VALUE,o_NOT_NEGATABLE, o_BN,"name of backup"},
    {"BT", "backup-type",   o_REQUIRED_VALUE,o_NOT_NEGATABLE, o_BT,"backup type (FULL, DIFF, or INCR)"},
#endif
    {"b",  "temp-path",   o_REQUIRED_VALUE, o_NOT_NEGATABLE, 'b',  "dir to use for temp archive"},
    {"c",  "entry-comments", o_NO_VALUE,    o_NOT_NEGATABLE, 'c',  "add comments for each entry"},
#ifdef CHANGE_DIRECTORY
    {"cd", "current-directory", o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_cd, "set current dir for paths"},
#endif
#ifdef VMS
    {"C",  "preserve-case", o_NO_VALUE,     o_NEGATABLE,     'C',  "Preserve (C-: down-) case all on VMS"},
    {"C2", "preserve-case-2", o_NO_VALUE,   o_NEGATABLE,     o_C2, "Preserve (C2-: down-) case ODS2 on VMS"},
    {"C5", "preserve-case-5", o_NO_VALUE,   o_NEGATABLE,     o_C5, "Preserve (C5-: down-) case ODS5 on VMS"},
#endif /* VMS */
#ifdef ENABLE_PATH_CASE_CONV
    {"Cl", "case-lower",  o_NO_VALUE,      o_NOT_NEGATABLE, o_Cl, "convert added/updated names to lowercase"},
    {"Cu", "case-upper",  o_NO_VALUE,      o_NOT_NEGATABLE, o_Cu, "convert added/udated names to uppercase"},
#endif
    {"d",  "delete",      o_NO_VALUE,       o_NOT_NEGATABLE, 'd',  "delete entries from archive"},
    {"db", "display-bytes", o_NO_VALUE,     o_NEGATABLE,     o_db, "display running bytes"},
    {"dc", "display-counts", o_NO_VALUE,    o_NEGATABLE,     o_dc, "display running file count"},
    {"dd", "display-dots", o_NO_VALUE,      o_NEGATABLE,     o_dd, "display dots as process each file"},
#ifdef ENABLE_ENTRY_TIMING
    {"de", "display-est-to-go", o_NO_VALUE, o_NEGATABLE,     o_de, "display estimated time to go"},
#endif
    {"dg", "display-globaldots",o_NO_VALUE, o_NEGATABLE,     o_dg, "display dots for archive instead of files"},
#ifdef ENABLE_ENTRY_TIMING
    {"dr", "display-rate", o_NO_VALUE,      o_NEGATABLE,     o_dr, "display estimated zip rate in bytes/sec"},
#endif
    {"ds", "dot-size",     o_REQUIRED_VALUE,o_NOT_NEGATABLE, o_ds, "set progress dot size - default 10M bytes"},
    {"dt", "display-time", o_NO_VALUE,      o_NOT_NEGATABLE, o_dt, "display time start each entry"},
    {"du", "display-usize", o_NO_VALUE,     o_NEGATABLE,     o_du, "display uncompressed size in bytes"},
    {"dv", "display-volume", o_NO_VALUE,    o_NEGATABLE,     o_dv, "display volume (disk) number"},
#if defined( MACOS) || (defined( UNIX) && defined( __APPLE__))
    {"df", "datafork",    o_NO_VALUE,       o_NEGATABLE,     o_df, "save data fork only"},
#endif /* defined( MACOS) || (defined( UNIX) && defined( __APPLE__)) */
    {"D",  "no-dir-entries", o_NO_VALUE,    o_NOT_NEGATABLE, 'D',  "no entries for dirs themselves (-x */)"},
    {"DF", "difference-archive",o_NO_VALUE, o_NOT_NEGATABLE, o_DF, "create diff archive with changed/new files"},
    {"DI", "incremental-list",o_VALUE_LIST, o_NOT_NEGATABLE, o_DI, "archive list to exclude from -DF archive"},
#ifdef IZ_CRYPT_ANY
    {"e",  "encrypt",     o_NO_VALUE,       o_NOT_NEGATABLE, 'e',  "encrypt entries, ask for password"},
#endif /* def IZ_CRYPT_ANY */
#if defined( IZ_CRYPT_TRAD) && defined( ETWODD_SUPPORT)
    {"et", "etwodd",      o_NO_VALUE,       o_NOT_NEGATABLE, o_et, "encrypt Traditional without data descriptor"},
#endif /* defined( IZ_CRYPT_TRAD) && defined( ETWODD_SUPPORT) */
#ifdef OS2
    {"E",  "longnames",   o_NO_VALUE,       o_NOT_NEGATABLE, 'E',  "use OS2 longnames"},
#endif
    {"EA", "extended-attributes",o_REQUIRED_VALUE,o_NOT_NEGATABLE,o_EA,"control storage of extended attributes"},
    {"F",  "fix",         o_NO_VALUE,       o_NOT_NEGATABLE, 'F',  "fix mostly intact archive (try first)"},
    {"FF", "fixfix",      o_NO_VALUE,       o_NOT_NEGATABLE, o_FF, "try harder to fix archive (not as reliable)"},
    {"FI", "fifo",        o_NO_VALUE,       o_NEGATABLE,     o_FI, "read Unix FIFO (zip will wait on open pipe)"},
    {"FS", "filesync",    o_NO_VALUE,       o_NOT_NEGATABLE, o_FS, "add/delete entries to make archive match OS"},
    {"f",  "freshen",     o_NO_VALUE,       o_NOT_NEGATABLE, 'f',  "freshen existing archive entries"},
    {"fd", "force-descriptors", o_NO_VALUE, o_NOT_NEGATABLE, o_des,"force data descriptors as if streaming"},
#ifdef ZIP64_SUPPORT
    {"fz", "force-zip64", o_NO_VALUE,       o_NEGATABLE,     o_z64,"force use of Zip64 format, negate prevents"},
#endif
    {"g",  "grow",        o_NO_VALUE,       o_NOT_NEGATABLE, 'g',  "grow existing archive instead of replace"},
    {"h",  "help",        o_NO_VALUE,       o_NOT_NEGATABLE, 'h',  "help"},
    {"H",  "",            o_NO_VALUE,       o_NOT_NEGATABLE, 'h',  "help"},
    {"?",  "",            o_NO_VALUE,       o_NOT_NEGATABLE, 'h',  "help"},
    {"h2", "more-help",   o_NO_VALUE,       o_NOT_NEGATABLE, o_h2, "extended help"},
    {"hh", "",            o_NO_VALUE,       o_NOT_NEGATABLE, o_h2, "extended help"},
    {"HH", "",            o_NO_VALUE,       o_NOT_NEGATABLE, o_h2, "extended help"},
    {"i",  "include",     o_VALUE_LIST,     o_NOT_NEGATABLE, 'i',  "include only files matching patterns"},
#if defined(VMS) || defined(WIN32)
    {"ic", "ignore-case", o_NO_VALUE,       o_NEGATABLE,     o_ic, "ignore case when matching archive entries"},
#endif
#ifdef RISCOS
    {"I",  "no-image",    o_NO_VALUE,       o_NOT_NEGATABLE, 'I',  "no image"},
#endif
    {"j",  "junk-paths",  o_NO_VALUE,       o_NEGATABLE,     'j',  "strip paths and just store file names"},
#ifdef MACOS
    {"jj", "absolute-path", o_NO_VALUE,     o_NOT_NEGATABLE, o_jj, "MAC absolute path"},
#endif /* ?MACOS */
    {"J",  "junk-sfx",    o_NO_VALUE,       o_NOT_NEGATABLE, 'J',  "strip self extractor from archive"},
    {"k",  "DOS-names",   o_NO_VALUE,       o_NOT_NEGATABLE, 'k',  "force use of 8.3 DOS names"},
    {"l",  "to-crlf",     o_NO_VALUE,       o_NOT_NEGATABLE, 'l',  "convert text file line ends - LF->CRLF"},
    {"ll", "from-crlf",   o_NO_VALUE,       o_NOT_NEGATABLE, o_ll, "convert text file line ends - CRLF->LF"},
    {"lf", "logfile-path",o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_lf, "log to log file at path (default overwrite)"},
    {"lF", "log-output",  o_NO_VALUE,       o_NEGATABLE,     o_lF, "log to OUTNAME.log (default overwrite)"},
    {"la", "log-append",  o_NO_VALUE,       o_NEGATABLE,     o_la, "append to existing log file"},
    {"li", "log-info",    o_NO_VALUE,       o_NEGATABLE,     o_li, "include informational messages in log"},
    {"lu", "log-utf8",    o_NO_VALUE,       o_NEGATABLE,     o_lu, "log names as UTF-8"},
    {"L",  "license",     o_NO_VALUE,       o_NOT_NEGATABLE, 'L',  "display license"},
    {"m",  "move",        o_NO_VALUE,       o_NOT_NEGATABLE, 'm',  "add files to archive then delete files"},
    {"mm", "",            o_NO_VALUE,       o_NOT_NEGATABLE, o_mm, "not used"},
    {"MM", "must-match",  o_NO_VALUE,       o_NOT_NEGATABLE, o_MM, "error if infile not matched/not readable"},
#ifdef CMS_MVS
    {"MV", "MVS",         o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_MV, "MVS path translate (dots, slashes, lastdot)"},
#endif /* CMS_MVS */
    {"n",  "suffixes",    o_REQUIRED_VALUE, o_NOT_NEGATABLE, 'n',  "suffixes to not compress: .gz:.zip"},
    {"nw", "no-wild",     o_NO_VALUE,       o_NOT_NEGATABLE, o_nw, "no wildcards during add or update"},
#if defined(AMIGA) || defined(MACOS)
    {"N",  "notes",       o_NO_VALUE,       o_NOT_NEGATABLE, 'N',  "add notes as entry comments"},
#endif
    {"o",  "latest-time", o_NO_VALUE,       o_NOT_NEGATABLE, 'o',  "use latest entry time as archive time"},
    {"O",  "output-file", o_REQUIRED_VALUE, o_NOT_NEGATABLE, 'O',  "set out zipfile different than in zipfile"},
    {"p",  "paths",       o_NO_VALUE,       o_NOT_NEGATABLE, 'p',  "store paths"},
    {"pa", "prefix-add-path",o_REQUIRED_VALUE,o_NOT_NEGATABLE,o_pa,"add prefix to added/updated paths"},
    {"pn", "non-ansi-password", o_NO_VALUE, o_NEGATABLE,     o_pn, "allow non-ANSI password"},
    {"ps", "allow-short-password", o_NO_VALUE, o_NEGATABLE,  o_ps, "allow short password"},
    {"pp", "prefix-path", o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_pp, "add prefix to all paths in archive"},
    {"pt", "performance-time", o_NO_VALUE,  o_NEGATABLE,     o_pt, "time execution of zip"},
#ifdef IZ_CRYPT_ANY
    {"P",  "password",    o_REQUIRED_VALUE, o_NOT_NEGATABLE, 'P',  "encrypt entries, option value is password"},
#endif /* def IZ_CRYPT_ANY */
#if defined(QDOS) || defined(QLZIP)
    {"Q",  "Q-flag",      o_NUMBER_VALUE,   o_NOT_NEGATABLE, 'Q',  "Q flag"},
#endif
    {"q",  "quiet",       o_NO_VALUE,       o_NOT_NEGATABLE, 'q',  "quiet"},
    {"r",  "recurse-paths", o_NO_VALUE,     o_NOT_NEGATABLE, 'r',  "recurse down listed paths"},
    {"R",  "recurse-patterns", o_NO_VALUE,  o_NOT_NEGATABLE, 'R',  "recurse current dir and match patterns"},
    {"RE", "regex",       o_NO_VALUE,       o_NOT_NEGATABLE, o_RE, "allow [list] matching (regex)"},
    {"s",  "split-size",  o_REQUIRED_VALUE, o_NOT_NEGATABLE, 's',  "do splits, set split size (-s=0 no splits)"},
    {"sp", "split-pause", o_NO_VALUE,       o_NOT_NEGATABLE, o_sp, "pause while splitting to select destination"},
    {"sv", "split-verbose", o_NO_VALUE,     o_NOT_NEGATABLE, o_sv, "be verbose about creating splits"},
    {"sb", "split-bell",  o_NO_VALUE,       o_NOT_NEGATABLE, o_sb, "when pause for next split ring bell"},
    {"sc", "show-command",o_NO_VALUE,       o_NOT_NEGATABLE, o_sc, "show command line"},
    {"sP", "show-parsed-command",o_NO_VALUE,o_NOT_NEGATABLE, o_sP,"show command line as parsed"},

#ifdef UNICODE_TEST
    {"sC", "create-files",o_NO_VALUE,       o_NOT_NEGATABLE, o_sC, "create empty files using archive names"},
#endif
    {"sd", "show-debug",  o_NO_VALUE,       o_NOT_NEGATABLE, o_sd, "show debug"},
    {"sf", "show-files",  o_NO_VALUE,       o_NEGATABLE,     o_sf, "show files to operate on and exit"},
    {"sF", "sf-params",   o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_sF, "add info to -sf listing"},
#if !defined( VMS) && defined( ENABLE_USER_PROGRESS)
    {"si", "show-pid",    o_NO_VALUE,       o_NEGATABLE,     o_si, "show process ID"},
#endif /* !defined( VMS) && defined( ENABLE_USER_PROGRESS) */
    {"so", "show-options",o_NO_VALUE,       o_NOT_NEGATABLE, o_so, "show options"},
    {"ss", "show-suffixes",o_NO_VALUE,      o_NOT_NEGATABLE, o_ss, "show method-level suffix lists"},
    {"st", "stream",      o_NO_VALUE,       o_NOT_NEGATABLE, o_st, "include local attr/comment for stream extract"},
#ifdef UNICODE_SUPPORT
    {"su", "show-unicode", o_NO_VALUE,      o_NEGATABLE,     o_su, "as -sf but also show escaped Unicode"},
    {"sU", "show-just-unicode", o_NO_VALUE, o_NEGATABLE,     o_sU, "as -sf but only show escaped Unicode"},
#endif
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(ATARI)
    {"S",  "",            o_NO_VALUE,       o_NOT_NEGATABLE, 'S',  "include system and hidden"},
#endif /* MSDOS || OS2 || WIN32 || ATARI */
    {"t",  "from-date",   o_REQUIRED_VALUE, o_NOT_NEGATABLE, 't',  "exclude before date"},
    {"tt", "before-date", o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_tt, "include before date"},
#ifdef USE_EF_UT_TIME
    {"tn", "no-universal-time",o_NO_VALUE,  o_NOT_NEGATABLE, o_tn, "do not store universal time for file/entry"},
#endif
    {"T",  "test",        o_NO_VALUE,       o_NOT_NEGATABLE, 'T',  "test updates before replacing archive"},
    {"TT", "unzip-command", o_REQUIRED_VALUE,o_NOT_NEGATABLE,o_TT, "unzip/test command, name is added to end"},
    {"", "test-command", o_REQUIRED_VALUE,o_NOT_NEGATABLE,o_TT, "unzip/test command, name is added to end"},
    {"u",  "update",      o_NO_VALUE,       o_NOT_NEGATABLE, 'u',  "update existing entries and add new"},
    {"U",  "copy-entries", o_NO_VALUE,      o_NOT_NEGATABLE, 'U',  "select from archive instead of file system"},
#ifdef UNICODE_SUPPORT
    {"UN", "unicode",     o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_UN, "UN=quit/warn/ignore/no, esc, loc/utf8, show"},
    {"UT", "utest",       o_NO_VALUE,       o_NOT_NEGATABLE, o_UT, "Do some Unicode tests"},
#endif
    {"v",  "verbose",     o_NO_VALUE,       o_NOT_NEGATABLE, 'v',  "display additional information"},
    {"vq", "quick-version", o_NO_VALUE,     o_NOT_NEGATABLE, o_vq, "display quick version"},
    {"",   "version",     o_NO_VALUE,       o_NOT_NEGATABLE, o_ve, "(if no other args) show version information"},
#ifdef VMS
    {"V",  "VMS-portable", o_NO_VALUE,      o_NOT_NEGATABLE, 'V',  "store VMS attributes, portable file format"},
    {"VV", "VMS-specific", o_NO_VALUE,      o_NOT_NEGATABLE, o_VV, "store VMS attributes, VMS specific format"},
    {"vn", "vms-names",    o_NO_VALUE,      o_NEGATABLE,     o_vn, "preserve idiosyncratic VMS file names"},
    {"w",  "VMS-versions", o_NO_VALUE,      o_NOT_NEGATABLE, 'w',  "store VMS versions"},
    {"ww", "VMS-dot-versions", o_NO_VALUE,  o_NOT_NEGATABLE, o_ww, "store VMS versions as \".nnn\""},
#endif /* VMS */
#ifdef WINDOWS_LONG_PATHS
    {"wl", "windows-long-paths", o_NO_VALUE, o_NEGATABLE,    o_wl,  "include windows long paths (see help -hh)"},
#endif
    {"ws", "wild-stop-dirs", o_NO_VALUE,    o_NOT_NEGATABLE, o_ws,  "* stops at /, ** includes any /"},
    {"x",  "exclude",     o_VALUE_LIST,     o_NOT_NEGATABLE, 'x',  "exclude files matching patterns"},
/*    {"X",  "no-extra",    o_NO_VALUE,       o_NOT_NEGATABLE, 'X',  "no extra"},
*/
    {"X",  "strip-extra", o_NO_VALUE,       o_NEGATABLE,     'X',  "-X- keep all ef, -X strip but critical ef"},
#ifdef SYMLINKS
    {"y",  "symlinks",    o_NO_VALUE,       o_NOT_NEGATABLE, 'y',  "store symbolic links"},
# ifdef WIN32
    {"yy", "no-mount-points", o_NO_VALUE,   o_NEGATABLE,     o_yy, "-yy=don't follow mount points, -yy-=follow all"},
# endif
#endif /* SYMLINKS */
#ifdef IZ_CRYPT_ANY
    {"Y", "encryption-method", o_REQUIRED_VALUE, o_NOT_NEGATABLE, 'Y', "set encryption method"},
#endif /* def IZ_CRYPT_ANY */
    {"z",  "archive-comment", o_NO_VALUE,   o_NOT_NEGATABLE, 'z',  "ask for archive comment"},
    {"Z",  "compression-method", o_REQUIRED_VALUE, o_NOT_NEGATABLE, 'Z', "compression method"},
#if defined(MSDOS) || defined(OS2) || defined( VMS)
    {"$",  "volume-label", o_NO_VALUE,      o_NOT_NEGATABLE, '$',  "store volume label"},
#endif
#if !defined(MACOS) && !defined(WINDLL)
    {"@",  "names-stdin", o_NO_VALUE,       o_NOT_NEGATABLE, '@',  "get file names from stdin, one per line"},
#endif /* !MACOS */
    {"@@", "names-file",  o_REQUIRED_VALUE, o_NOT_NEGATABLE, o_atat,"get file names from file, one per line"},
#ifdef NTSD_EAS
    {"!",  "use-privileges", o_NO_VALUE,    o_NOT_NEGATABLE, '!',  "use privileges"},
    {"!!", "no-security",    o_NO_VALUE,    o_NOT_NEGATABLE, o_exex,"leave out security info (ACLs)"},
#endif
#ifdef RISCOS
    {"/",  "exts-to-swap", o_REQUIRED_VALUE, o_NOT_NEGATABLE,'/',  "override Zip$Exts"},
#endif
    /* the end of the list */
    {NULL, NULL,          o_NO_VALUE,       o_NOT_NEGATABLE, 0,    NULL} /* end has option_ID = 0 */
  };



#ifndef USE_ZIPMAIN
int main(argc, argv)
#else
int zipmain(argc, argv)
#endif
int argc;               /* number of tokens in command line */
char **argv;            /* command line tokens */
/* Add, update, freshen, or delete zip entries in a zip file.  See the
   command help in help() above. */
{
  int d;                /* true if just adding to a zip file */
  char *e;              /* malloc'd comment buffer */
  struct flist far *f;  /* steps through found linked list */
  int i;                /* arg counter, root directory flag */
  int kk;               /* next arg type (formerly another re-use of "k") */

  /* zip64 support 09/05/2003 R.Nausedat */
  uzoff_t c;            /* start of central directory */
  uzoff_t t;            /* length of central directory */
  zoff_t k;             /* marked counter, entry count */
  uzoff_t n;            /* total of entry len's */

  int o;                /* true if there were any ZE_OPEN errors */
#if !defined(ZIPLIB) && !defined(ZIPDLL)
  char *p;              /* steps through option arguments */
#endif
  char *pp;             /* temporary pointer */
  int r;                /* temporary variable */
  int s;                /* flag to read names from stdin */
  uzoff_t csize;        /* compressed file size for stats */
  uzoff_t usize;        /* uncompressed file size for stats */
  ulg tf;               /* file time */
  int first_listarg = 0;/* index of first arg of "process these files" list */
  struct zlist far *v;  /* temporary variable */
  struct zlist far * far *w;    /* pointer to last link in zfiles list */
  FILE *x /*, *y */;    /* input and output zip files (y global) */
  struct zlist far *z;  /* steps through zfiles linked list */
  int bad_open_is_error = 0; /* if read fails, 0=warning, 1=error */
#ifdef ZIP_DLL_LIB
  int retcode;          /* return code for dll */
#endif /* ZIP_DLL_LIB */
#if (!defined(VMS) && !defined(CMS_MVS))
  char *zipbuf;         /* stdio buffer for the zip file */
#endif /* !VMS && !CMS_MVS */
  int all_current;      /* used by File Sync to determine if all entries are current */

  struct filelist_struct *filearg;

/* used by get_option */
  unsigned long option; /* option ID returned by get_option */
  int argcnt = 0;       /* current argcnt in args */
  int argnum = 0;       /* arg number */
  int optchar = 0;      /* option state */
  char *value = NULL;   /* non-option arg, option value or NULL */
  int negated = 0;      /* 1 = option negated */
  int fna = 0;          /* current first non-opt arg */
  int optnum = 0;       /* index in table */
  time_t cur_time_opt;  /* Current date-time to get date when -t[t] have time only */

  int show_options = 0; /* show options */
  int show_suffixes = 0;/* Show method-level suffix lists. */
  int show_args = 0;    /* show command line */
  int show_parsed_args = 0; /* show parsed command line */
  int seen_doubledash = 0; /* seen -- argument */
  int key_needed = 0;   /* prompt for encryption key */
  int have_out = 0;     /* if set in_path and out_path different archive */

  int sort_found_list = 0; /* sort the found list (set below) */

#ifdef UNICODE_TEST
  int create_files = 0;
#endif
  int names_from_file = 0; /* names being read from file */

#ifdef SHOW_PARSED_COMMAND
  int parsed_arg_count;
  char **parsed_args;
  int max_parsed_args;
#endif /* def SHOW_PARSED_COMMAND */



#ifdef THEOS
  /* the argument expansion from the standard library is full of bugs */
  /* use mine instead */
  _setargv(&argc, &argv);
  setlocale(LC_CTYPE, "I");
#else
  /* This is all done below now */
# if 0
  /* Tell base library that we support locales.  This
     will load the locale the user has selected.  Before
     setlocale() is called, a minimal "C" locale is the
     default. */
  /* This is undefined in Win32.  Will try to address it in the next beta.
     However, on Windows we use the wide paths that are Unicode, so may
     not need to worry about locale. */
  /* It looks like we're only supporting back to Windows XP now, so this
     should be OK. */
#  ifndef WIN32
  SETLOCALE(LC_CTYPE, "");
#  endif
# endif
#endif

#ifdef ENABLE_ENTRY_TIMING
  start_time = get_time_in_usec();
#endif

/* --------------------------------------------------------------------- */
/* Locale detection is now done regardless of setting of UNICODE_SUPPORT.
    (setlocale was already being executed above.) */

  {
    char *loc = NULL;
    char *codeset = NULL;
    int locale_debug = 0;
    int charsetlen = 0;

#ifdef HAVE_SETLOCALE
  /* For Unix, we either need to be working in some UTF-8 environment or we
     need help elsewise to see all file system paths available to us,
     otherwise paths not supported in the current character set won't be seen
     and can't be archived.  If UTF-8 is native, the full Unicode paths
     should be viewable in a terminal window.

     As of Zip 3.1, we no longer set the locale to UTF-8.  If the native
     locale is UTF-8, we proceed with UTF-8 as native.  Otherwise we
     use the local character set as is.  (An exception is Windows, where
     we always use the wide functions if available, and so native UTF-8
     support is not needed.  UTF-8 is derived as needed from the wide
     versions of the paths.)

     If we detect we are already in some UTF-8 environment, then we can
     proceed.  If not, we can attempt (in some cases) to change to UTF-8 if
     supported.  (For most ports, the actual UTF-8 encoding probably does not
     matter, and setting the locale to en_US.UTF-8 may be sufficient.  For
     others, it does matter and some special handling is needed.)  If neither
     work and we can't establish some UTF-8 environment, we should give up on
     Unicode, as the level of support is not clear and may not be sufficient.
     One of the main reasons for supporting Unicode is so we can find and
     archive the various non-current-char-set paths out there as they come
     up in a directory scan.  We now distinguish this case (UNICODE_FILE_SCAN).
     We also use UTF-8 for writing paths in the archive and for display of
     the real paths on the console (if supported).  To some degree, the
     Unicode escapes (#Uxxxx and #Lxxxxxx) can be used to bypass that use
     of UTF-8.

     For Windows, the Unicode tasks are handled using the OS wide character
     calls, as there is no direct UTF-8 support.  So directory scans are done
     using wide character strings and then converted to UTF-8 for storage in
     the archive.  This falls into the "need help elsewise" category and adds
     considerable Windows-unique code, but it seems the only way if full
     native Unicode support is to be had.  (iconv won't work, as there are
     many abnormalities in how Unicode is handled in Windows that are handled
     by the native OS calls, but would need significant kluging if we tried to
     do all that.  For instance, the handling of surrogates.  Best to leave
     converting Windows wide strings to UTF-8 to Windows.)  has_win32_wide()
     is used to determine if the Windows port supports wide characters.
     
     Note that paths displayed in a Windows command prompt window will likely
     be escaped.  If a Unicode supporting font is loaded (like Lucida Console)
     and the code page is set to UTF-8 (chcp 65001), then most Western
     characters should be visible, but languages like Japanese probably will
     not display correctly (showing small boxes instead of the right characters).

     For the IBM ports (z/OS and related), this gets more complex than the Unix
     case as those ports are EBCDIC based.  While one can work with UTF-8 via
     iconv, one can not actually run in an ASCII-based locale via setlocale()
     in the z/OS Unix environment.  (Some?) IBM ports do provide a wide DBCS
     (double byte character set) environment that seems to mirror somewhat
     Windows wide functionality, but this is reported to be insufficient.
     
     AIX will support the UTF-8 locale, but it is an optional feature, so one
     must do a test to see if it is present.  Some specific testing is needed
     and is being worked on.

     In some cases, character set conversions can be done using iconv, but
     iconv only does character set conversions.  The wide functions provide
     many additional capabilities (like case conversion of wide characters)
     that iconv does not.  When iconv is used, alternatives need to be
     found for the missing capabilities, or features using those capabilities
     disabled for the port using iconv.

     See the large note in tailor.h for more on Unicode support.

     A new option -UT (--utest) performs select Unicode tests and displays
     the results.

     We plan to update the below checks shortly.
  */

# ifdef UNIX
    {
      if (locale_debug) {
        loc = setlocale(LC_CTYPE, NULL);
        zprintf("  Initial language locale = '%s'\n", loc);
      }

      /* New check provided by Danny Milosavljevic (SourceForge) */

      /* Tell base library that we support locales.  This
         will load the locale the user has selected.  Before
         setlocale() is called, a minimal "C" locale is the
         default. */
      setlocale(LC_CTYPE, "");

      loc = setlocale(LC_CTYPE, NULL);
      if (loc) {
        if (locale_debug) {
          zprintf("  Locale after initialization = '%s'\n", loc);
        }
        if ((localename = (char *)malloc((strlen(loc) + 1) * sizeof(char))) == NULL) {
          ZIPERR(ZE_MEM, "localename");
        }
        strcpy(localename, loc);
      }

#  ifndef NO_NL_LANGINFO
      /* get the codeset (character set encoding) currently used,
         for example "UTF-8". */
      codeset = nl_langinfo(CODESET);

      if (codeset) {
        if (locale_debug) {
          zprintf("  charsetname = codeset = '%s'\n", codeset);
        }
        charsetlen = strlen(codeset) + 1;
        if ((charsetname = (char *)malloc(charsetlen * sizeof(char))) == NULL) {
          ZIPERR(ZE_MEM, "localename");
        }
        strcpy(charsetname, codeset);
      }

#  else
      /* lacking a way to get codeset, get locale */
      {
        char *c;
        loc = setlocale(LC_CTYPE, NULL);
        /* for UTF-8, should be close to en_US.UTF-8 */
        for (c = loc; c; c++) {
          if (*c == '.') {
            /* loc is what is after '.', maybe UTF-8 */
            loc = c + 1;
            break;
          }
        }
      }

      if (locale_debug) {
        zprintf("  End part Locale = '%s'\n", loc);
      }

      charsetlen = strlen(loc) + 1;

      if ((charsetname = (char *)malloc(charsetlen * sizeof(char))) == NULL) {
        ZIPERR(ZE_MEM, "charsetname");
      }
      strcpy(charsetname, loc);

      if (locale_debug) {
        zprintf("  charsetname = '%s'\n", charsetname);
      }
#  endif

      if ((codeset && strcmp(codeset, "UTF-8") == 0)
           || (loc && strcmp(loc, "UTF-8") == 0)) {
        /* already using UTF-8 */
        using_utf8 = 1;
      }
      /* Tim K. advises not to force UTF-8 if not native */
#  if 0
      else {
        /* try setting UTF-8 */
        if (setlocale(LC_CTYPE, "en_US.UTF-8") != NULL) {
          using_utf8 = 1;
        } else {
          if (locale_debug) {
            zprintf("  Could not set Unicode UTF-8 locale\n");
          }
        }
        if (locale_debug) {
          zprintf("  Could not set Unicode UTF-8 locale\n");
        }
      }
#  endif


      /* Alternative fix for just MAEMO. */
# if 0
#  ifdef MAEMO
      loc = setlocale(LC_CTYPE, "");
#  else
      loc = setlocale(LC_CTYPE, "en_US.UTF-8");
#  endif

      if (locale_debug) {
        zprintf("langinfo %s\n", nl_langinfo(CODESET));
      }

      if (loc != NULL) {
        /* using UTF-8 character set so can set UTF-8 GPBF bit 11 */
        using_utf8 = 1;
        if (locale_debug) {
          zprintf("  Locale set to %s\n", loc);
        }
      } else {
        if (locale_debug) {
          zprintf("  Could not set Unicode UTF-8 locale\n");
        }
      }
#  endif
    }

# else /* UNIX */

#  ifdef WIN32
    {
      char *loc = NULL;
      int codepage = 0;
      int charsetlen = 0;
      char *prefix = "WINDOWS-";

      if (locale_debug) {
        loc = setlocale(LC_CTYPE, NULL);
        zprintf("  Initial language locale = '%s'\n", loc);
      }

      /* Set the applications codepage to the current windows codepage.
       */
      setlocale(LC_CTYPE, "");

      loc = setlocale(LC_CTYPE, NULL);

      if (locale_debug) {
        zprintf("  Locale after initialization = '%s'\n", loc);
      }

      if (loc) {
        if ((localename = (char *)malloc((strlen(loc) + 1) * sizeof(char))) == NULL) {
          ZIPERR(ZE_MEM, "localename");
        }
        strcpy(localename, loc);
      }

      /* Windows does not have nl_langinfo */

      /* lacking a way to get codeset, get locale */
      {
        char *c;
        loc = setlocale(LC_CTYPE, NULL);
        /* for UTF-8, should be close to en_US.UTF-8 */
        for (c = loc; c; c++) {
          if (*c == '.') {
            /* loc is what is after '.', maybe UTF-8 */
            loc = c + 1;
            break;
          }
        }
      }
      if (locale_debug) {
        zprintf("  End part Locale = '%s'\n", loc);
      }

      charsetlen = strlen(prefix) + strlen(loc) + 1;

      if ((charsetname = (char *)malloc(charsetlen * sizeof(char))) == NULL) {
        ZIPERR(ZE_MEM, "charsetname");
      }
      strcpy(charsetname, prefix);
      strcat(charsetname, loc);

      if (locale_debug) {
        zprintf("  charsetname = '%s'\n", charsetname);
      }

      codepage = _getmbcp();

      if (locale_debug) {
        zprintf("  Codepage = '%d'\n", codepage);
      }

      /* GetLocaleInfo; */

    }

#  endif /* WIN32 */
# endif /* not UNIX */

#else /* not HAVE_SETLOCALE */

/* other ports */
  SETLOCALE(LC_CTYPE, "");

#endif /* not HAVE_SETLOCALE */

    if (locale_debug) {
      zprintf("\n");
    }
  }
/* --------------------------------------------------------------------- */





#if defined(__IBMC__) && defined(__DEBUG_ALLOC__)
  {
    extern void DebugMalloc(void);
    atexit(DebugMalloc);
  }
#endif

#ifdef QDOS
  {
    extern void QDOSexit(void);
    atexit(QDOSexit);
  }
#endif

#ifdef NLM
  {
    extern void NLMexit(void);
    atexit(NLMexit);
  }
#endif

#ifdef RISCOS
  set_prefix();
#endif

#ifdef __human68k__
  fflush(stderr);
  setbuf(stderr, NULL);
#endif

#ifdef VMS
  /* This pointless reference to a do-nothing function ensures that the
   * globals get linked in, even on old systems, or when compiled using
   * /NAMES = AS_IS.  (See also globals.c.)
   */
  {
    void (*local_dummy)( void);
    local_dummy = globals_dummy;
  }
#endif /* def VMS */


/* Re-initialize global variables to make the zip dll re-entrant. It is
 * possible that we could get away with not re-initializing all of these
 * but better safe than sorry.
 */
#if defined(MACOS) || defined(WINDLL) || defined(USE_ZIPMAIN)
  action = ADD; /* one of ADD, UPDATE, FRESHEN, DELETE, or ARCHIVE */
  comadd = 0;   /* 1=add comments for new files */
  zipedit = 0;  /* 1=edit zip comment and all file comments */
  latest = 0;   /* 1=set zip file time to time of latest file */
  before = 0;   /* 0=ignore, else exclude files before this time */
  after = 0;    /* 0=ignore, else exclude files newer than this time */
  test = 0;     /* 1=test zip file with unzip -t */
  unzip_path = NULL; /* where to look for unzip command path */
  tempdir = 0;  /* 1=use temp directory (-b) */
  junk_sfx = 0; /* 1=junk the sfx prefix */
# if defined(AMIGA) || defined(MACOS)
  filenotes = 0;/* 1=take comments from AmigaDOS/MACOS filenotes */
# endif
# ifndef USE_ZIPMAIN
  zipstate = -1;
# endif
  tempzip = NULL;
  fcount = 0;
  recurse = 0;         /* 1=recurse into directories; 2=match filenames */
  dispose = 0;         /* 1=remove files after put in zip file */
  pathput = 1;         /* 1=store path with name */

# if defined(UNIX) && defined(__APPLE__)
  data_fork_only = 0;
  sequester = 0;
# endif
# ifdef RISCOS
  int scanimage = 0;   /* Scan through image files */
# endif

  method = BEST;        /* one of BEST, DEFLATE (only), or STORE (only) */
  dosify = 0;           /* 1=make new entries look like MSDOS */
  verbose = 0;          /* 1=report oddities in zip file structure */
  fix = 0;              /* 1=fix the zip file */
  filesync = 0;         /* 1=file sync, delete entries not on file system */
  adjust = 0;           /* 1=adjust offsets for sfx'd file (keep preamble) */
  level = 6;            /* 0=fastest compression, 9=best compression */
# if defined( IZ_CRYPT_TRAD) && defined( ETWODD_SUPPORT)
  etwodd = 0;           /* Encrypt Trad without data descriptor. */
# endif /* defined( IZ_CRYPT_TRAD) && defined( ETWODD_SUPPORT) */
  translate_eol = 0;    /* Translate end-of-line LF -> CR LF */
# ifdef VMS
  prsrv_vms = 0;        /* Preserve idiosyncratic VMS file names. */
  vmsver = 0;           /* Append VMS version number to file names. */
  vms_native = 0;       /* Store in VMS format */
  vms_case_2 = 0;       /* ODS2 file name case in VMS. -1: down. */
  vms_case_5 = 0;       /* ODS5 file name case in VMS. +1: preserve. */
  argv_cli = NULL;      /* New argv[] storage to free, if non-NULL. */
# endif /* VMS */
# if defined(OS2) || defined(WIN32)
  use_longname_ea = 0;  /* 1=use the .LONGNAME EA as the file's name */
# endif
# if defined (QDOS) || defined(QLZIP)
  qlflag = 0;
# endif
# ifdef NTSD_EAS
  use_privileges = 0;     /* 1=use security privileges overrides */
# endif
  no_wild = 0;            /* 1 = wildcards are disabled */
# ifdef WILD_STOP_AT_DIR
   wild_stop_at_dir = 1;  /* default wildcards do not include / in matches */
# else
   wild_stop_at_dir = 0;  /* default wildcards do include / in matches */
# endif

  for (i = 0; mthd_lvl[i].method >= 0; i++)
  { /* Restore initial compression level-method states. */
    mthd_lvl[i].level = -1;       /* By-method level. */
    mthd_lvl[i].level_sufx = -1;  /* By-suffix level. */
    mthd_lvl[i].suffixes = NULL;  /* Suffix list. */
  }
  mthd_lvl[0].suffixes = MTHD_SUFX_0;  /* STORE default suffix list. */

  skip_this_disk = 0;
  des_good = 0;           /* Good data descriptor found */
  des_crc = 0;            /* Data descriptor CRC */
  des_csize = 0;          /* Data descriptor csize */
  des_usize = 0;          /* Data descriptor usize */

  dot_size = 0;           /* buffers processed in deflate per dot, 0 = no dots */
  dot_count = 0;          /* buffers seen, recyles at dot_size */

  display_counts = 0;     /* display running file count */
  display_bytes = 0;      /* display running bytes remaining */
  display_globaldots = 0; /* display dots for archive instead of each file */
  display_volume = 0;     /* display current input and output volume (disk) numbers */
  display_usize = 0;      /* display uncompressed bytes */
  display_est_to_go = 0;  /* display estimated time to go */
  display_time = 0;       /* display time start each entry */
  display_zip_rate = 0;   /* display bytes per second rate */

  files_so_far = 0;       /* files processed so far */
  bad_files_so_far = 0;   /* bad files skipped so far */
  files_total = 0;        /* files total to process */
  bytes_so_far = 0;       /* bytes processed so far (from initial scan) */
  good_bytes_so_far = 0;  /* good bytes read so far */
  bad_bytes_so_far = 0;   /* bad bytes skipped so far */
  bytes_total = 0;        /* total bytes to process (from initial scan) */

  logall = 0;             /* 0 = warnings/errors, 1 = all */
  logfile = NULL;         /* pointer to open logfile or NULL */
  logfile_append = 0;     /* append to existing logfile */
  logfile_path = NULL;    /* pointer to path of logfile */

  use_outpath_for_log = 0; /* 1 = use output archive path for log */
  log_utf8 = 0;           /* log names as UTF-8 */
#ifdef WIN32
  nonlocal_name = 0;      /* Name has non-local characters */
  nonlocal_path = 0;      /* Path has non-local characters */
#endif

  startup_dir = NULL;     /* dir that Zip starts in (current dir ".") */
  working_dir = NULL;     /* dir user asked to change to for zipping */

  hidden_files = 0;       /* process hidden and system files */
  volume_label = 0;       /* add volume label */
  label = NULL;           /* volume label */
  dirnames = 1;           /* include directory entries by default */
# if defined(WIN32)
  only_archive_set = 0;   /* only include if DOS archive bit set */
  clear_archive_bits = 0; /* clear DOS archive bit of included files */
# endif
  linkput = 0;            /* 1=store symbolic links as such */
  noisy = 1;              /* 0=quiet operation */
  extra_fields = 1;       /* 0=create minimum, 1=don't copy old, 2=keep old */

  use_descriptors = 0;    /* 1=use data descriptors 12/29/04 */
  zip_to_stdout = 0;      /* output zipfile to stdout 12/30/04 */
  allow_empty_archive = 0;/* if no files, create empty archive anyway 12/28/05 */
  copy_only = 0;          /* 1=copying archive entries only */

  include_stream_ef = 0;  /* 1=include stream ef that allows full stream extraction */

  output_seekable = 1;    /* 1 = output seekable 3/13/05 EG */

# ifdef ZIP64_SUPPORT     /* zip64 support 10/4/03 */
  force_zip64 = -1;       /* if 1 force entries to be zip64 */
                          /* mainly for streaming from stdin */
  zip64_entry = 0;        /* current entry needs Zip64 */
  zip64_archive = 0;      /* if 1 then at least 1 entry needs zip64 */
# endif

# ifdef UNICODE_SUPPORT
  utf8_native = 1;        /* 1=force storing UTF-8 as standard per AppNote bit 11 */
# endif

  unicode_escape_all = 0; /* 1=escape all non-ASCII characters in paths */
  unicode_mismatch = 1;   /* unicode mismatch is 0=error, 1=warn, 2=ignore, 3=no */

  scan_delay = 5;         /* seconds before display Scanning files message */
  scan_dot_time = 2;      /* time in seconds between Scanning files dots */
  scan_start = 0;         /* start of scan */
  scan_last = 0;          /* time of last message */
  scan_started = 0;       /* scan has started */
  scan_count = 0;         /* Used for Scanning files ... message */

  before = 0;             /* 0=ignore, else exclude files before this time */
  after = 0;              /* 0=ignore, else exclude files newer than this time */

  key = NULL;             /* Scramble password if scrambling */
  key_needed = 0;         /* Need scramble password */
#ifdef IZ_CRYPT_AES_WG
  key_size = 0;
#endif
  force_ansi_key = 1;     /* Only ANSI characters for password (32 - 126) */

  path_prefix = NULL;     /* Prefix to add to all new archive entries */
  path_prefix_mode = 0;   /* 0=Prefix all paths, 1=Prefix only added/updated paths */
  tempath = NULL;         /* Path for temporary files */
  patterns = NULL;        /* List of patterns to be matched */
  pcount = 0;             /* number of patterns */
  icount = 0;             /* number of include only patterns */
  Rcount = 0;             /* number of -R include patterns */

#ifdef ENABLE_USER_PROGRESS
  u_p_phase = 0;
  u_p_task = NULL;
  u_p_name = NULL;
#endif

  found = NULL;           /* List of names found, or new found entry */
  fnxt = &found;

  /* used by get_option */
  argcnt = 0;             /* size of args */
  argnum = 0;             /* current arg number */
  optchar = 0;            /* option state */
  value = NULL;           /* non-option arg, option value or NULL */
  negated = 0;            /* 1 = option negated */
  fna = 0;                /* current first nonopt arg */
  optnum = 0;             /* option index */

  show_options = 0;       /* 1 = show options */
  show_what_doing = 0;    /* 1 = show what zip doing */
  show_args = 0;          /* 1 = show command line */
  show_parsed_args = 0;   /* 1 = show parsed command line */
  seen_doubledash = 0;    /* seen -- argument */

  args = NULL;            /* copy of argv that can be freed by free_args() */

  all_ascii = 0;          /* skip binary check and handle all files as text */

  zipfile = NULL;         /* path of usual in and out zipfile */
  tempzip = NULL;         /* name of temp file */
  y = NULL;               /* output file now global so can change in splits */
  in_file = NULL;         /* current input file for splits */
  in_split_path = NULL;   /* current in split path */
  in_path = NULL;         /* used by splits to track changing split locations */
  out_path = NULL;        /* if set, use -O out_path as output */
  have_out = 0;           /* if set, in_path and out_path not the same archive */
  zip_attributes = 0;

  total_disks = 0;        /* total disks in archive */
  current_in_disk = 0;    /* current read split disk */
  current_in_offset = 0;  /* current offset in current read disk */
  skip_current_disk = 0;  /* if != 0 and fix then skip entries on this disk */

  zip64_eocd_disk = 0;    /* disk with Zip64 End Of Central Directory Record */
  zip64_eocd_offset = 0;  /* offset for Zip64 EOCD Record */

  current_local_disk = 0; /* disk with current local header */

  current_disk = 0;           /* current disk number */
  cd_start_disk = (ulg)-1;    /* central directory start disk */
  cd_start_offset = 0;        /* offset of start of cd on cd start disk */
  cd_entries_this_disk = 0;   /* cd entries this disk */
  total_cd_entries = 0;       /* total cd entries in new/updated archive */

  /* for split method 1 (keep split with local header open and update) */
  current_local_tempname = NULL; /* name of temp file */
  current_local_file = NULL;  /* file pointer for current local header */
  current_local_offset = 0;   /* offset to start of current local header */

  /* global */
  bytes_this_split = 0;       /* bytes written to the current split */
  read_split_archive = 0;     /* 1=scanzipf_reg detected spanning signature */
  split_method = 0;           /* 0=no splits, 1=update LHs, 2=data descriptors */
  split_size = 0;             /* how big each split should be */
  split_bell = 0;             /* when pause for next split ring bell */
  bytes_prev_splits = 0;      /* total bytes written to all splits before this */
  bytes_this_entry = 0;       /* bytes written for this entry across all splits */
  noisy_splits = 0;           /* be verbose about creating splits */
  mesg_line_started = 0;      /* 1=started writing a line to mesg */
  logfile_line_started = 0;   /* 1=started writing a line to logfile */

#ifdef WINDOWS_LONG_PATHS
  include_windows_long_paths = 0;
  archive_has_long_path = 0;
#endif

  filelist = NULL;            /* list of input files */
  filearg_count = 0;

  apath_list = NULL;          /* list of incremental archive paths */
  apath_count = 0;

  allow_empty_archive = 0;    /* if no files, allow creation of empty archive anyway */
  bad_open_is_error = 0;      /* if read fails, 0=warning, 1=error */
  unicode_mismatch = 0;       /* unicode mismatch is 0=error, 1=warn, 2=ignore, 3=no */
  show_files = 0;             /* show files to operate on and exit */

  sf_usize = 0;               /* include usize in -sf listing */

  mvs_mode = 0;               /* 0=lastdot (default), 1=dots, 2=slashes */

  scan_delay = 5;             /* seconds before display Scanning files message */
  scan_dot_time = 2;          /* time in seconds between Scanning files dots */
  scan_started = 0;           /* space at start of scan has been displayed */
  scan_last = 0;              /* Time last dot displayed for Scanning files message */
  scan_start = 0;             /* Time scanning started for Scanning files message */
# ifdef UNICODE_SUPPORT
  use_wide_to_mb_default = 0;
# endif
  filter_match_case = 1;      /* default is to match case when matching archive entries */
  diff_mode = 0;              /* 1=diff mode - only store changed and add */

  allow_fifo = 0;             /* 1=allow reading Unix FIFOs, waiting if pipe open */
# ifdef ENABLE_ENTRY_TIMING
  start_zip_time = 0;         /* after scan, when start zipping files */
# endif
  names_from_file = 0;

  case_upper_lower = CASE_PRESERVE;

#ifdef ENABLE_ENTRY_TIMING
  performance_time = 0;
#endif

#ifdef UNICODE_SUPPORT
  unicode_show = 0;
#endif

  encryption_method = NO_ENCRYPTION;

  allow_arg_files = 1;

# ifdef ZIP_DLL_LIB
  /* set up error return jump */
  retcode = setjmp(zipdll_error_return);
  if (retcode) {
    return retcode;
  }
  /* verify NULL termination of (user-supplied) argv[] */
  if (argv[ argc] != NULL)
  {
    ZIPERR( ZE_PARMS, "argv[argc] != NULL");
  }
# endif /* def ZIP_DLL_LIB */
# ifdef BACKUP_SUPPORT
  backup_dir = NULL;            /* dir to save backup archives (and control) */
  backup_name = NULL;           /* name to use for archive, log, and control */
  backup_control_dir = NULL;    /* control file dir (overrides backup_dir) */
  backup_log_dir = NULL;        /* backup log dir (defaults to backup_dir) */
  backup_type = 0;              /* default to not using backup mode */
  backup_start_datetime = NULL; /* date/time stamp of start of backup */
  backup_control_dir = NULL;    /* dir to put control file */
  backup_control_path = NULL;   /* control file used to store backup set */
  backup_full_path = NULL;      /* full archive of backup set */
  backup_output_path = NULL;    /* path of output archive before finalizing */
# endif
#endif /* MACOS || WINDLL || USE_ZIPMAIN */

/* Standardize use of -RE to enable special [] use (was just MSDOS and WIN32) */
/*#if !defined(ALLOW_REGEX) && (defined(MSDOS) || defined(WIN32)) */
#if !defined(ALLOW_REGEX)
  allow_regex = 0;        /* 1 = allow [list] matching (regex) */
#else
  allow_regex = 1;
#endif

  mesg = (FILE *) stdout; /* cannot be made at link time for VMS */
  comment_stream = (FILE *)stdin;

  init_upper();           /* build case map table */

#ifdef LARGE_FILE_SUPPORT
  /* test if we can support large files - 9/29/04 */
  if (sizeof(zoff_t) < 8) {
    ZIPERR(ZE_COMPILE, "LARGE_FILE_SUPPORT enabled but OS not supporting it");
  }
#endif
  /* test if sizes are the same - 12/30/04 */
  if (sizeof(uzoff_t) != sizeof(zoff_t)){
    sprintf(errbuf, "uzoff_t size (%d bytes) not same as zoff_t (%d bytes)",
             (int)sizeof(uzoff_t), (int)sizeof(zoff_t));
    ZIPERR(ZE_COMPILE, errbuf);
  }


#if defined( IZ_CRYPT_AES_WG) || defined( IZ_CRYPT_AES_WG_NEW)
  /* Verify the AES_WG compile-time endian decision. */
  {
    union {
      unsigned int i;
      unsigned char b[ 4];
    } bi;

# ifndef PLATFORM_BYTE_ORDER
#  define ENDI_BYTE 0x00
#  define ENDI_PROB "(Undefined)"
# else
#  if PLATFORM_BYTE_ORDER == AES_LITTLE_ENDIAN
#   define ENDI_BYTE 0x78
#   define ENDI_PROB "Little"
#  else
#   if PLATFORM_BYTE_ORDER == AES_BIG_ENDIAN
#    define ENDI_BYTE 0x12
#    define ENDI_PROB "Big"
#   else
#    define ENDI_BYTE 0xff
#    define ENDI_PROB "(Unknown)"
#   endif
#  endif
# endif

    bi.i = 0x12345678;
    if (bi.b[ 0] != ENDI_BYTE) {
      sprintf( errbuf, "Bad AES_WG compile-time endian: %s", ENDI_PROB);
      ZIPERR( ZE_COMPILE, errbuf);
    }
  }
#endif /* defined( IZ_CRYPT_AES_WG) || defined( IZ_CRYPT_AES_WG_NEW) */


#if (defined(WIN32) && defined(USE_EF_UT_TIME))
  /* For the Win32 environment, we may have to "prepare" the environment
     prior to the tzset() call, to work around tzset() implementation bugs.
   */
  iz_w32_prepareTZenv();
#endif

#if (defined(IZ_CHECK_TZ) && defined(USE_EF_UT_TIME))
#  ifndef VALID_TIMEZONE
#     define VALID_TIMEZONE(tmp) \
             (((tmp = getenv("TZ")) != NULL) && (*tmp != '\0'))
#  endif
  zp_tz_is_valid = VALID_TIMEZONE(p);
#if (defined(AMIGA) || defined(DOS))
  if (!zp_tz_is_valid)
    extra_fields = 0;     /* disable storing "UT" time stamps */
#endif /* AMIGA || DOS */
#endif /* IZ_CHECK_TZ && USE_EF_UT_TIME */

/* For systems that do not have tzset() but supply this function using another
   name (_tzset() or something similar), an appropriate "#define tzset ..."
   should be added to the system specifc configuration section.  */
#if (!defined(TOPS20) && !defined(VMS))
#if (!defined(RISCOS) && !defined(MACOS) && !defined(QDOS))
#if (!defined(BSD) && !defined(MTS) && !defined(CMS_MVS) && !defined(TANDEM))
  tzset();
#endif
#endif
#endif

#ifdef VMSCLI
  {
    unsigned int status;
    char **argv_old;

    argv_old = argv;    /* Save the original argv for later comparison. */
    status = vms_zip_cmdline( &argc, &argv);

    /* Record whether vms_zip_cmdline() created a new argv[]. */
    if (argv_old == argv)
      argv_cli = NULL;
    else
      argv_cli = argv;

    if (!(status & 1))
      return status;
  }
#endif /* VMSCLI */

  /*    Substitutes the extended command line argument list produced by
   *    the MKS Korn Shell in place of the command line info from DOS.
   */

  /* extract extended argument list from environment */
  expand_args(&argc, &argv);

  /* Process arguments */
  diag("processing arguments");
  /* First, check if just the help or version screen should be displayed */
  /* Now support help listing when called from DLL */
  if (argc == 1
# ifndef WINDLL
      && isatty(1)
# endif
      )   /* no arguments, and output screen available */
  {                             /* show help screen */
# ifdef VMSCLI
    VMSCLI_help();
# else
    help();
# endif
#ifdef WINDLL
    return ZE_OK;
#else
    EXIT(ZE_OK);
#endif
  }
  /* Check -v here as env arg can change argc.  Handle --version in main switch. */
  else if (argc == 2 && strcmp(argv[1], "-v") == 0 &&
           /* only "-v" as argument, and */
           (isatty(1) || isatty(0)))
           /* stdout or stdin is connected to console device */
  {                             /* show diagnostic version info */
    version_info();
#ifdef WINDLL
    return ZE_OK;
#else
    EXIT(ZE_OK);
#endif
  }
#ifndef ZIP_DLL_LIB
# ifndef VMS
#   ifndef RISCOS
  envargs(&argc, &argv, "ZIPOPT", "ZIP");  /* get options from environment */
#   else /* RISCOS */
  envargs(&argc, &argv, "ZIPOPT", "Zip$Options");  /* get options from environment */
  getRISCOSexts("Zip$Exts");        /* get the extensions to swap from environment */
#   endif /* ? RISCOS */
# else /* VMS */
  envargs(&argc, &argv, "ZIPOPT", "ZIP_OPTS");  /* 4th arg for unzip compat. */
# endif /* ?VMS */
#endif /* !ZIP_DLL_LIB */

  zipfile = tempzip = NULL;
  y = NULL;
  d = 0;                        /* disallow adding to a zip file */

#ifndef NO_EXCEPT_SIGNALS
# if (!defined(MACOS) && !defined(ZIP_DLL_LIB) && !defined(NLM))
  signal(SIGINT, handler);
#  ifdef SIGTERM                  /* AMIGADOS and others have no SIGTERM */
  signal(SIGTERM, handler);
#  endif
#  if defined(SIGABRT) && !(defined(AMIGA) && defined(__SASC))
   signal(SIGABRT, handler);
#  endif
#  ifdef SIGBREAK
   signal(SIGBREAK, handler);
#  endif
#  ifdef SIGBUS
   signal(SIGBUS, handler);
#  endif
#  ifdef SIGILL
   signal(SIGILL, handler);
#  endif
#  ifdef SIGSEGV
   signal(SIGSEGV, handler);
#  endif
# endif /* !MACOS && !ZIP_DLL_LIB && !NLM */
# ifdef NLM
  NLMsignals();
# endif
#endif /* ndef NO_EXCEPT_SIGNALS */


#ifdef ENABLE_USER_PROGRESS
# ifdef VMS
  establish_ctrl_t( user_progress);
# else /* def VMS */
#  ifdef SIGUSR1
  signal( SIGUSR1, user_progress);
#  endif /* def SIGUSR1 */
# endif /* def VMS [else] */
#endif /* def ENABLE_USER_PROGRESS */


#ifdef UNICODE_SUPPORT_WIN32
  /* check if this Win32 OS has support for wide character calls */
  has_win32_wide();
#endif

  /* Set default STORE list. */
  {
    int new_list_size;
    char *new_list;

    new_list_size = strlen(MTHD_SUFX_0);
    if ((new_list = malloc(new_list_size + 1)) == NULL) {
      ZIPERR(ZE_MEM, "setting STORE suffix list");
    }
    strcpy(new_list, MTHD_SUFX_0);
    mthd_lvl[0].suffixes = new_list;
  }

  /* Get current (local) date for possible use with time-only "-t[t]"
   * option values.
   */
  cur_time_opt = time(NULL);

#ifdef SHOW_PARSED_COMMAND
  /* make copy of command line as it is parsed */
  parsed_arg_count = 0;
  parsed_args = NULL;
  max_parsed_args = 0;    /* space allocated on first arg assignment */
#endif


  /* make copy of args that can use with insert_arg() used by get_option() */
  args = copy_args(argv, 0);

  kk = 0;                       /* Next non-option argument type */
  s = 0;                        /* set by -@ */

  /*
  -------------------------------------------
  Process command line using get_option
  -------------------------------------------

  Each call to get_option() returns either a command
  line option and possible value or a non-option argument.
  Arguments are permuted so that all options (-r, -b temp)
  are returned before non-option arguments (zipfile).
  Returns 0 when nothing left to read.
  */

  /* set argnum = 0 on first call to init get_option */
  argnum = 0;

  /* get_option returns the option ID and updates parameters:
         args    - usually same as argv if no argument file support
         argcnt  - current argc for args
         value   - char* to value (free() when done with it) or NULL if no value
         negated - option was negated with trailing -
  */

  while ((option = get_option(&args, &argcnt, &argnum,
                              &optchar, &value, &negated,
                              &fna, &optnum, 0)))
  {
    /* Limit returned value or non-option to MAX_OPTION_VALUE_SIZE. */
    if (value && (MAX_OPTION_VALUE_SIZE) &&
        strlen(value) > (MAX_OPTION_VALUE_SIZE)) {
      sprintf(errbuf, "command line argument larger than %d - truncated: ",
              MAX_OPTION_VALUE_SIZE); 
      zipwarn(errbuf, value);
      value[MAX_OPTION_VALUE_SIZE] = '\0';
    }

#ifdef SHOW_PARSED_COMMAND
    {
      /* save this parsed argument */

      int use_short = 1;
      char *opt;

      /* this arg may parse to option and value (creating 2 parsed args),
         or to non-opt arg (creating 1 parsed arg) */
      if (parsed_arg_count > max_parsed_args - 2) {
        /* increase space used to store parsed arguments */
        max_parsed_args += 256;
        if ((parsed_args = (char **)realloc(parsed_args,
                (max_parsed_args + 1) * sizeof(char *))) == NULL) {
          ZIPERR(ZE_MEM, "realloc parsed command line");
        }
      }

      if (option != o_NON_OPTION_ARG) {
        /* assume option and possible value */

        /* default to using short option string */
        opt = options[optnum].shortopt;
        if (strlen(opt) == 0) {
          /* if no short, use long option string */
          use_short = 0;
          opt = options[optnum].longopt;
        }
        if ((parsed_args[parsed_arg_count] =
                      (char *)malloc(strlen(opt) + 3)) == NULL) {
          ZIPERR(ZE_MEM, "parse command line (1)");
        }
        if (use_short) {
          strcpy(parsed_args[parsed_arg_count], "-");
        } else {
          strcpy(parsed_args[parsed_arg_count], "--");
        }
        strcat(parsed_args[parsed_arg_count], opt);

        if (value) {
          parsed_arg_count++;

          if ((parsed_args[parsed_arg_count] =
                     (char *)malloc(strlen(value) + 1)) == NULL) {
            ZIPERR(ZE_MEM, "parse command line (2)");
          }
          strcpy(parsed_args[parsed_arg_count], value);
        }
      } else {
        /* non-option arg */

        if ((parsed_args[parsed_arg_count] =
                   (char *)malloc(strlen(value) + 1)) == NULL) {
          ZIPERR(ZE_MEM, "parse command line (3)");
        }
        strcpy(parsed_args[parsed_arg_count], value);
      }
      parsed_arg_count++;

      parsed_args[parsed_arg_count] = NULL;
    }
#endif


    switch (option)
    {
        case '0':
          method = STORE; level = 0;
          break;
        case '1':  case '2':  case '3':  case '4':
        case '5':  case '6':  case '7':  case '8':  case '9':
          {
            /* Could be a simple number (-3) or include a value
               (-3=def:bz) */
            char *dp1;
            char *dp2;
            char t;
            int j;
            int lvl;

            /* Calculate the integer compression level value. */
            lvl = (int)option - '0';

            /* Analyze any option value (method names). */
            if (value == NULL)
            {
              /* No value.  Set the global compression level. */
              level = lvl;
            }
            else
            {
              /* Set the by-method compression level for the specified
               * method names in "value" ("mthd1:mthd2:...:mthdn").
               */
              dp1 = value;
              while (*dp1 != '\0')
              {
                /* Advance dp2 to the next delimiter.  t = delimiter. */
                for (dp2 = dp1;
                 (((t = *dp2) != ':') && (*dp2 != ';') && (*dp2 != '\0'));
                 dp2++);

                /* NUL-terminate the latest list segment. */
                *dp2 = '\0';

                /* Check for a match in the method-by-suffix array. */
                for (j = 0; mthd_lvl[ j].method >= 0; j++)
                {
                  if (abbrevmatch( mthd_lvl[ j].method_str, dp1, CASE_INS, 1))
                  {
                    /* Matched method name.  Set the by-method level. */
                    mthd_lvl[ j].level = lvl;
                    break;

                  }
                }

                if (mthd_lvl[ j].method < 0)
                {
                  sprintf( errbuf, "Invalid compression method: \"%s\"", dp1);
                  free( value);
                  ZIPERR( ZE_PARMS, errbuf);
                }

                /* Quit at end-of-string. */
                if (t == '\0') break;

                /* Otherwise, advance dp1 to the next list segment. */
                dp1 = dp2+ 1;
              }
            }
          }
          break;

#ifdef EBCDIC
      case 'a':
        aflag = FT_ASCII_TXT;
        zipmessage("Translating to ASCII...", "");
        break;
      case o_aa:
        aflag = FT_ASCII_TXT;
        all_ascii = 1;
        zipmessage("Skipping binary check, assuming all files text (ASCII)...", "");
        break;
#endif /* EBCDIC */
#ifdef CMS_MVS
        case 'B':
          bflag = 1;
          zipmessage("Using binary mode...", "");
          break;
#endif /* CMS_MVS */
#ifdef TANDEM
        case 'B':
          nskformatopt(value);
          free(value);
          break;
#endif
        case 'A':   /* Adjust unzipsfx'd zipfile:  adjust offsets only */
          adjust = 1; break;
#if defined(WIN32)
        case o_AC:
          clear_archive_bits = 1; break;
        case o_AS:
          /* Since some directories could be empty if no archive bits are
             set for files in a directory, don't add directory entries (-D).
             Just files with the archive bit set are added, including paths
             (unless paths are excluded).  All major unzips should create
             directories for the paths as needed. */
          dirnames = 0;
          only_archive_set = 1; break;
#endif /* defined(WIN32) */
        case o_AF:
          /* Turn off and on processing of @argfiles.  Default is on. */
          if (negated)
            allow_arg_files = 0;
          else
            allow_arg_files = 1;
#if defined( UNIX) && defined( __APPLE__)
        case o_as:
          /* sequester Apple Double resource files */
          if (negated)
            sequester = 0;
          else
            sequester = 1;
          break;
#endif /* defined( UNIX) && defined( __APPLE__) */

        case 'b':   /* Specify path for temporary file */
          tempdir = 1;
          tempath = value;
          break;

#ifdef BACKUP_SUPPORT
        case o_BC:  /* Dir for BACKUP control file */
          if (backup_control_dir)
            free(backup_control_dir);
          backup_control_dir = value;
          break;
        case o_BD:  /* Dir for BACKUP archive (and control file) */
          if (backup_dir)
            free(backup_dir);
          backup_dir = value;
          break;
        case o_BL:  /* dir for BACKUP log */
          if (backup_log_dir)
            free(backup_log_dir);
          if (value == NULL)
          {
            if ((backup_log_dir = malloc(1)) == NULL) {
              ZIPERR(ZE_MEM, "backup_log_dir");
            }
            strcpy(backup_log_dir, "");
          } else {
            backup_log_dir = value;
          }
          break;
        case o_BN:  /* Name of BACKUP archive and control file */
          if (backup_name)
            free(backup_name);
          backup_name = value;
          break;
        case o_BT:  /* Type of BACKUP archive */
          if (abbrevmatch("none", value, CASE_INS, 1)) {
            /* Revert to normal operation, no backup control file */
            backup_type = BACKUP_NONE;
          } else if (abbrevmatch("full", value, CASE_INS, 1)) {
            /* Perform full backup (normal archive), init control file */
            backup_type = BACKUP_FULL;
          } else if (abbrevmatch("differential", value, CASE_INS, 1)) {
            /* Perform differential (all files different from base archive) */
            backup_type = BACKUP_DIFF;
            diff_mode = 1;
            allow_empty_archive = 1;
          } else if (abbrevmatch("incremental", value, CASE_INS, 1)) {
            /* Perform incremental (all different from base and other incr) */
            backup_type = BACKUP_INCR;
            diff_mode = 1;
            allow_empty_archive = 1;
          } else {
            zipwarn("-BT:  Unknown backup type: ", value);
            free(value);
            ZIPERR(ZE_PARMS, "-BT:  backup type must be FULL, DIFF, INCR, or NONE");
          }
          free(value);
          break;
#endif
        case 'c':   /* Add comments for new files in zip file */
          comadd = 1;  break;

#ifdef CHANGE_DIRECTORY
        case o_cd:  /* Change default directory */
          if (working_dir)
            free(working_dir);
          working_dir = value;
          break;
#endif

        /* -C, -C2, and -C5 are with -V */

#ifdef ENABLE_PATH_CASE_CONV
        case o_Cl:   /* Convert added/updated paths to lowercase */
          case_upper_lower = CASE_LOWER;
          break;
        case o_Cu:   /* Convert added/updated paths to uppercase */
          case_upper_lower = CASE_UPPER;
          break;
#endif

        case 'd':   /* Delete files from zip file */
          if (action != ADD) {
            ZIPERR(ZE_PARMS, "specify just one action");
          }
          action = DELETE;
          break;
#ifdef MACOS
        case o_df:
          MacZip.DataForkOnly = true;
          break;
#endif /* def MACOS */
#if defined( UNIX) && defined( __APPLE__)
        case o_df:
          if (negated)
            data_fork_only = 0;
          else
            data_fork_only = 1;
          break;
#endif /* defined( UNIX) && defined( __APPLE__) */
        case o_db:
          if (negated)
            display_bytes = 0;
          else
            display_bytes = 1;
          break;
        case o_dc:
          if (negated)
            display_counts = 0;
          else
            display_counts = 1;
          break;
        case o_dd:
          /* display dots */
          display_globaldots = 0;
          if (negated) {
            dot_count = 0;
          } else {
            /* set default dot size if dot_size not set (dot_count = 0) */
            if (dot_count == 0)
              /* default to 10 MiB */
              dot_size = 10 * 0x100000;
            dot_count = -1;
          }
          break;
#ifdef ENABLE_ENTRY_TIMING
        case o_de:
          if (negated)
            display_est_to_go = 0;
          else
            display_est_to_go = 1;
          break;
#endif
        case o_dg:
          /* display dots globally for archive instead of for each file */
          if (negated) {
            display_globaldots = 0;
          } else {
            display_globaldots = 1;
            /* set default dot size if dot_size not set (dot_count = 0) */
            if (dot_count == 0)
              dot_size = 10 * 0x100000;
            dot_count = -1;
          }
          break;
#ifdef ENABLE_ENTRY_TIMING
        case o_dr:
          if (negated)
            display_zip_rate = 0;
          else
            display_zip_rate = 1;
          break;
#endif
        case o_ds:
          /* input dot_size is now actual dot size to account for
             different buffer sizes */
          if (value == NULL)
            dot_size = 10 * 0x100000;
          else if (value[0] == '\0') {
            /* default to 10 MiB */
            dot_size = 10 * 0x100000;
            free(value);
          } else {
            dot_size = ReadNumString(value);
            if (dot_size == (zoff_t)-1) {
              sprintf(errbuf, "option -ds (--dot-size) has bad size:  '%s'",
                      value);
              free(value);
              ZIPERR(ZE_PARMS, errbuf);
            }
            if (dot_size < 0x400) {
              /* < 1 KB so there is no multiplier, assume MiB */
              dot_size *= 0x100000;

            } else if (dot_size < 0x400L * 32) {
              /* 1K <= dot_size < 32K */
              sprintf(errbuf, "dot size must be at least 32 KB:  '%s'", value);
              free(value);
              ZIPERR(ZE_PARMS, errbuf);

            } else {
              /* 32K <= dot_size */
            }
            free(value);
          }
          dot_count = -1;
          break;
        case o_dt:
          if (negated)
            display_time = 0;
          else
            display_time = 1;
          break;
        case o_du:
          if (negated)
            display_usize = 0;
          else
            display_usize = 1;
          break;
        case o_dv:
          if (negated)
            display_volume = 0;
          else
            display_volume = 1;
          break;
        case 'D':   /* Do not add directory entries */
          dirnames = 0; break;
        case o_DF:  /* Create a difference archive */
          diff_mode = 1;
          allow_empty_archive = 1;
          break;
        case o_DI:  /* Create an incremental archive */
          ZIPERR(ZE_PARMS, "-DI not yet implemented");

          /* implies diff mode */
          diff_mode = 2;

          /* add archive path to list */
          add_apath(value);

          break;
        case 'e':   /* Encrypt */
#ifdef IZ_CRYPT_ANY
          if (key == NULL)
            key_needed = 1;
          if (encryption_method == NO_ENCRYPTION) {
            encryption_method = TRADITIONAL_ENCRYPTION;
          }
#else /* def IZ_CRYPT_ANY */
          ZIPERR(ZE_PARMS, "encryption (-e) not supported");
#endif /* def IZ_CRYPT_ANY [else] */
          break;

/* SMSd. */ /* Bracket with a #if/macro?  Also option, above? */
        case o_EA:
          ZIPERR(ZE_PARMS, "-EA (extended attributes) not yet implemented");

#if defined(IZ_CRYPT_TRAD) && defined(ETWODD_SUPPORT)
        case o_et:      /* Encrypt Traditional without data descriptor. */
          etwodd = 1;
          break;
#endif /* defined( IZ_CRYPT_TRAD) && defined( ETWODD_SUPPORT) */
        case 'F':   /* fix the zip file */
#if defined(ZIPLIB) || defined(ZIPDLL)
          ZIPERR(ZE_PARMS, "-F not yet supported for LIB or DLL");
#else
          fix = 1;
#endif
          break;
        case o_FF:  /* try harder to fix file */
#if defined(ZIPLIB) || defined(ZIPDLL)
          ZIPERR(ZE_PARMS, "-FF not yet supported for LIB or DLL");
#else
          fix = 2;
#endif
          break;

/* SMSd. */ /* Bracket with a #if/macro?  Also option, above? */
        case o_FI:
          if (negated)
            allow_fifo = 0;
          else
            allow_fifo = 1;
          break;

        case o_FS:  /* delete exiting entries in archive where there is
                       no matching file on file system */
          filesync = 1; break;
        case 'f':   /* Freshen zip file--overwrite only */
          if (action != ADD) {
            ZIPERR(ZE_PARMS, "specify just one action");
          }
          action = FRESHEN;
          break;
        case 'g':   /* Allow appending to a zip file */
          d = 1;  break;

        case 'h': case 'H': case '?':  /* Help */
#ifdef VMSCLI
          VMSCLI_help();
#else
          help();
#endif
          RETURN(finish(ZE_OK));

        case o_h2:  /* Extended Help */
          help_extended();
          RETURN(finish(ZE_OK));

        /* -i is with -x */
#if defined(VMS) || defined(WIN32)
        case o_ic:  /* Ignore case (case-insensitive matching of archive entries) */
          if (negated)
            filter_match_case = 1;
          else
            filter_match_case = 0;
          break;
#endif
#ifdef RISCOS
        case 'I':   /* Don't scan through Image files */
          scanimage = 0;
          break;
#endif
#ifdef MACOS
        case o_jj:   /* store absolute path including volname */
            MacZip.StoreFullPath = true;
            break;
#endif /* ?MACOS */
        case 'j':   /* Junk directory names */
          if (negated)
            pathput = 1;
          else
            pathput = 0;
          break;
        case 'J':   /* Junk sfx prefix */
          junk_sfx = 1;  break;
        case 'k':   /* Make entries using DOS names (k for Katz) */
          dosify = 1;  break;
        case 'l':   /* Translate end-of-line */
          translate_eol = 1; break;
        case o_ll:
          translate_eol = 2; break;
        case o_lf:
          /* open a logfile */
          /* allow multiple use of option but only last used */
          if (logfile_path) {
            free(logfile_path);
          }
          logfile_path = value;
          break;
        case o_lF:
          /* open a logfile */
          /* allow multiple use of option but only last used */
          if (logfile_path) {
            free(logfile_path);
          }
          logfile_path = NULL;
          if (negated) {
            use_outpath_for_log = 0;
          } else {
            use_outpath_for_log = 1;
          }
          break;
        case o_la:
          /* append to existing logfile */
          if (negated)
            logfile_append = 0;
          else
            logfile_append = 1;
          break;
        case o_li:
          /* log all including informational messages */
          if (negated)
            logall = 0;
          else
            logall = 1;
          break;

/* SMSd. */ /* Bracket option, above, with the same #if/macro? */
#ifdef UNICODE_SUPPORT
        case o_lu:
          /* log messages in UTF-8 (requires UTF-8 enabled reader to read log) */
          if (negated)
            log_utf8 = 0;
          else
            log_utf8 = 1;
          break;
#endif

        case 'L':   /* Show license */
          license();
          RETURN(finish(ZE_OK));
        case 'm':   /* Delete files added or updated in zip file */
          dispose = 1;  break;
        case o_mm:  /* To prevent use of -mm for -MM */
          ZIPERR(ZE_PARMS, "-mm not supported, Must_Match is -MM");
          dispose = 1;  break;
        case o_MM:  /* Exit with error if input file can't be read */
          bad_open_is_error = 1; break;
#ifdef CMS_MVS
        case o_MV:   /* MVS path translation mode */
          if (abbrevmatch("dots", value, CASE_INS, 1)) {
            /* aaa.bbb.ccc.ddd stored as is */
            mvs_mode = 1;
          } else if (abbrevmatch("slashes", value, CASE_INS, 1)) {
            /* aaa.bbb.ccc.ddd -> aaa/bbb/ccc/ddd */
            mvs_mode = 2;
          } else if (abbrevmatch("lastdot", value, CASE_INS, 1)) {
            /* aaa.bbb.ccc.ddd -> aaa/bbb/ccc.ddd */
            mvs_mode = 0;
          } else {
            zipwarn("-MV must be dots, slashes, or lastdot: ", value);
            free(value);
            ZIPERR(ZE_PARMS, "-MV (MVS path translate mode) bad value");
          }
          free(value);
          break;
#endif /* CMS_MVS */
        case 'n':   /* Specify compression-method-by-name-suffix list. */
          {         /* Value format: "method[-lvl]=sfx1:sfx2:sfx3...". */
            suffixes_option(value);
          }
          break;
        case o_nw:  /* no wildcards - wildcards are handled like other
                       characters */
          no_wild = 1;
          break;
#if defined(AMIGA) || defined(MACOS)
        case 'N':   /* Get zipfile comments from AmigaDOS/MACOS filenotes */
          filenotes = 1; break;
#endif
        case 'o':   /* Set zip file time to time of latest file in it */
          latest = 1;  break;
        case 'O':   /* Set output file different than input archive */
          if (strcmp( value, "-") == 0)
          {
            mesg = stderr;
            if (isatty(1))
              ziperr(ZE_PARMS, "cannot write zip file to terminal");
            if ((out_path = malloc(4)) == NULL)
              ziperr(ZE_MEM, "was processing arguments (2)");
            strcpy( out_path, "-");
          }
          else
          {
            out_path = ziptyp(value);
            free(value);
          }
          have_out = 1;
          break;
        case 'p':   /* Store path with name */
          zipwarn("-p (include path) is deprecated.  Use -j- instead", "");
          break;            /* (do nothing as annoyance avoidance) */
        case o_pa:  /* Set prefix for paths of new entries in archive */
          if (path_prefix)
            free(path_prefix);
          path_prefix = value;
          path_prefix_mode = 1;
          break;
        case o_pp:  /* Set prefix for paths of new entries in archive */
          if (path_prefix)
            free(path_prefix);
          path_prefix = value;
          path_prefix_mode = 0;
          break;
        case o_pn:  /* Allow non-ANSI password */
          if (negated) {
            force_ansi_key = 1;
          } else {
            force_ansi_key = 0;
          }
          break;
        case o_ps:  /* Allow short password */
          if (negated) {
            allow_short_key = 0;
          } else {
            allow_short_key = 1;
          }
          break;
#ifdef ENABLE_ENTRY_TIMING
        case o_pt:  /* Time performance */
          if (negated) {
            performance_time = 0;
          } else {
            performance_time = 1;
          }
          break;
#endif
        case 'P':   /* password for encryption */
          if (key != NULL) {
            free(key);
          }
#ifdef IZ_CRYPT_ANY
          key = value;
          key_needed = 0;
          if (encryption_method == NO_ENCRYPTION) {
            encryption_method = TRADITIONAL_ENCRYPTION;
          }
#else /* def IZ_CRYPT_ANY */
          ZIPERR(ZE_PARMS, "encryption (-P) not supported");
#endif /* def IZ_CRYPT_ANY [else] */
          break;
#if defined(QDOS) || defined(QLZIP)
        case 'Q':
          qlflag  = strtol(value, NULL, 10);
       /* qlflag  = strtol((p+1), &p, 10); */
       /* p--; */
          if (qlflag == 0) qlflag = 4;
          free(value);
          break;
#endif
        case 'q':   /* Quiet operation */
          noisy = 0;
#ifdef MACOS
          MacZip.MacZip_Noisy = false;
#endif  /* MACOS */
          if (verbose) verbose--;
          break;
        case 'r':   /* Recurse into subdirectories, match full path */
          if (recurse == 2) {
            ZIPERR(ZE_PARMS, "do not specify both -r and -R");
          }
          recurse = 1;  break;
        case 'R':   /* Recurse into subdirectories, match filename */
          if (recurse == 1) {
            ZIPERR(ZE_PARMS, "do not specify both -r and -R");
          }
          recurse = 2;  break;

        case o_RE:   /* Allow [list] matching (regex) */
          allow_regex = 1; break;

        case o_sc:  /* show command line args */
          show_args = 1; break;

        case o_sP:  /* show parsed command line args */
          show_parsed_args = 1; break;

#ifdef UNICODE_TEST
        case o_sC:  /* create empty files from archive names */
          create_files = 1;
          show_files = 1; break;
#endif
        case o_sd:  /* show debugging */
          show_what_doing = 1; break;
        case o_sf:  /* show files to operate on */
          if (!negated)
            show_files = 1;
          else
            show_files = 2;
          break;
        case o_sF:  /* list of params for -sf */
          if (abbrevmatch("usize", value, CASE_INS, 1)) {
            sf_usize = 1;
          } else {
            zipwarn("bad -sF parameter: ", value);
            free(value);
            ZIPERR(ZE_PARMS, "-sF (-sf (show files) parameter) bad value");
          }
          free(value);
          break;

#if !defined( VMS) && defined( ENABLE_USER_PROGRESS)
        case o_si:  /* Show process id. */
          show_pid = 1; break;
#endif /* !defined( VMS) && defined( ENABLE_USER_PROGRESS) */

        case o_so:  /* show all options */
          show_options = 1; break;
        case o_ss:  /* show all options */
          show_suffixes = 1; break;
#ifdef UNICODE_SUPPORT
        case o_su:  /* -sf but also show Unicode if exists */
          if (!negated)
            show_files = 3;
          else
            show_files = 4;
          break;
        case o_sU:  /* -sf but only show Unicode if exists or normal if not */
          if (!negated)
            show_files = 5;
          else
            show_files = 6;
          break;
#endif

        case 's':   /* enable split archives */
          /* get the split size from value */
          if (strcmp(value, "-") == 0) {
            /* -s- do not allow splits */
            split_method = -1;
          } else {
            split_size = ReadNumString(value);
            if (split_size == (uzoff_t)-1) {
              sprintf(errbuf, "bad split size:  '%s'", value);
              ZIPERR(ZE_PARMS, errbuf);
            }
            if (split_size == 0) {
              /* do not allow splits */
              split_method = -1;
            } else {
              if (split_method == 0) {
                split_method = 1;
              }
              if (split_size < 0x400) {
                /* < 1 KB there is no multiplier, assume MiB */
                split_size *= 0x100000;
              }
              /* By setting the minimum split size to 64 KB we avoid
                 not having enough room to write a header unsplit
                 which is required */
              if (split_size < 0x400L * 64) {
                /* split_size < 64K */
                sprintf(errbuf, "minimum split size is 64 KB:  '%s'", value);
                free(value);
                ZIPERR(ZE_PARMS, errbuf);
              }
            }
          }
          free(value);
          break;
        case o_sb:  /* when pause for next split ring bell */
          split_bell = 1;
          break;
        case o_sp:  /* enable split select - pause splitting between splits */
#if defined(ZIPLIB) || defined(ZIPDLL)
          free(value);
          ZIPERR(ZE_PARMS, "-sp not yet supported for LIB or DLL");
#else
          use_descriptors = 1;
          split_method = 2;
#endif
          break;
        case o_sv:  /* be verbose about creating splits */
          noisy_splits = 1;
          break;

#ifdef STREAM_EF_SUPPORT
        case o_st:  /* enable stream mode (storing of attributes and comments in local headers) */
          include_stream_ef = 1;
          break;
#endif


#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(ATARI)
        case 'S':
          hidden_files = 1; break;
#endif /* MSDOS || OS2 || WIN32 || ATARI */
#ifdef MACOS
        case 'S':
          MacZip.IncludeInvisible = true; break;
#endif /* MACOS */
        case 't':   /* Exclude files earlier than specified date */
          {
            ulg dt;

            /* Support ISO 8601 & American dates */
            dt = datetime( value, cur_time_opt);
            if (dt == DT_BAD)
            {
              ZIPERR(ZE_PARMS,
         "invalid date/time for -t:  use mmddyyyy or yyyy-mm-dd[:HH:MM[:SS]]");
            }
            before = dt;
          }
          free(value);
          break;
        case o_tt:  /* Exclude files at or after specified date */
          {
            ulg dt;

            /* Support ISO 8601 & American dates */
            dt = datetime( value, cur_time_opt);
            if (dt == DT_BAD)
            {
              ZIPERR(ZE_PARMS,
         "invalid date/time for -tt:  use mmddyyyy or yyyy-mm-dd[:HH:MM[:SS]]");
            }
            after = dt;
          }
          free(value);
          break;
#ifdef USE_EF_UT_TIME
        case o_tn:
          no_universal_time = 1;
          break;
#endif
        case 'T':   /* test zip file */
#ifdef ZIP_DLL_LIB
          ZIPERR(ZE_PARMS, "-T not supported by DLL/LIB");
#endif
          test = 1; break;
        case o_TT:  /* command path to use instead of 'unzip -t ' */
#ifdef ZIP_DLL_LIB
          ZIPERR(ZE_PARMS, "-TT not supported by DLL/LIB");
#endif
          if (unzip_path)
            free(unzip_path);
          unzip_path = value;
          break;
        case 'U':   /* Select archive entries to keep or operate on */
          if (action != ADD) {
            ZIPERR(ZE_PARMS, "specify just one action");
          }
          action = ARCHIVE;
          break;
#ifdef UNICODE_SUPPORT
        case o_UN:   /* Unicode */
          if (abbrevmatch("quit", value, CASE_INS, 1)) {
            /* Unicode path mismatch is error */
            unicode_mismatch = 0;
          } else if (abbrevmatch("warn", value, CASE_INS, 1)) {
            /* warn of mismatches and continue */
            unicode_mismatch = 1;
          } else if (abbrevmatch("ignore", value, CASE_INS, 1)) {
            /* ignore mismatches and continue */
            unicode_mismatch = 2;
          } else if (abbrevmatch("no", value, CASE_INS, 1)) {
            /* no use Unicode path */
            unicode_mismatch = 3;
          } else if (abbrevmatch("escape", value, CASE_INS, 1)) {
            /* escape all non-ASCII characters */
            unicode_escape_all = 1;

          } else if (abbrevmatch("LOCAL", value, CASE_INS, 1)) {
            /* use extra fields for backward compatibility (reverse of UTF8) */
            utf8_native = 0;

          } else if (abbrevmatch("UTF8", value, CASE_INS, 1)) {
            /* store UTF-8 as standard per AppNote bit 11 (reverse of LOCAL) */
            utf8_native = 1;

          } else if (abbrevmatch("ShowUTF8", value, CASE_INS, 1)) {
            /* store UTF-8 as standard per AppNote bit 11 (reverse of LOCAL) */
            unicode_show = 1;

          } else {
            zipwarn("-UN must be Quit, Warn, Ignore, No, Escape, UTF8, LOCAL, or ShowUTF8: ", value);

            free(value);
            ZIPERR(ZE_PARMS, "-UN (unicode) bad value");
          }
          free(value);
          break;
#endif
#ifdef UNICODE_SUPPORT
        case o_UT:   /* Perform select Unicode tests and exit */
          Unicode_Tests();
          RETURN(finish(ZE_OK));
          break;
#endif
        case 'u':   /* Update zip file--overwrite only if newer */
          if (action != ADD) {
            ZIPERR(ZE_PARMS, "specify just one action");
          }
          action = UPDATE;
          break;
        case 'v':        /* Either display version information or */
        case o_ve:       /* Mention oddities in zip file structure */
          if (option == o_ve ||      /* --version */
              (argcnt == 2 && strlen(args[1]) == 2)) { /* -v only */
            /* display version */
            version_info();
            RETURN(finish(ZE_OK));
          } else {
            noisy = 1;
            verbose++;
          }
          break;
        case o_vq:       /* Display Quick Version */
          quick_version();
          RETURN(finish(ZE_OK));
          break;
#ifdef VMS
        case 'C':  /* Preserve case (- = down-case) all. */
          if (negated)
          { /* Down-case all. */
            if ((vms_case_2 > 0) || (vms_case_5 > 0))
            {
              ZIPERR( ZE_PARMS, "Conflicting case directives (-C-)");
            }
            vms_case_2 = -1;
            vms_case_5 = -1;
          }
          else
          { /* Not negated.  Preserve all. */
            if ((vms_case_2 < 0) || (vms_case_5 < 0))
            {
              ZIPERR( ZE_PARMS, "Conflicting case directives (-C)");
            }
            vms_case_2 = 1;
            vms_case_5 = 1;
          }
          break;
        case o_C2:  /* Preserve case (- = down-case) ODS2. */
          if (negated)
          { /* Down-case ODS2. */
            if (vms_case_2 > 0)
            {
              ZIPERR( ZE_PARMS, "Conflicting case directives (-C2-)");
            }
            vms_case_2 = -1;
          }
          else
          { /* Not negated.  Preserve ODS2. */
            if (vms_case_2 < 0)
            {
              ZIPERR( ZE_PARMS, "Conflicting case directives (-C2)");
            }
            vms_case_2 = 1;
          }
          break;
        case o_C5:  /* Preserve case (- = down-case) ODS5. */
          if (negated)
          { /* Down-case ODS5. */
            if (vms_case_5 > 0)
            {
              ZIPERR( ZE_PARMS, "Conflicting case directives (-C5-)");
            }
            vms_case_5 = -1;
          }
          else
          { /* Not negated.  Preserve ODS5. */
            if (vms_case_5 < 0)
            {
              ZIPERR( ZE_PARMS, "Conflicting case directives (-C5)");
            }
            vms_case_5 = 1;
          }
          break;
        case 'V':   /* Store in VMS format.  (Record multiples.) */
          vms_native = 1; break;
          /* below does work with new parser but doesn't allow tracking
             -VV separately, like adding a separate description */
          /* vms_native++; break; */
        case o_VV:  /* Store in VMS specific format */
          vms_native = 2; break;
        case o_vn:  /* Preserve idiosyncratic VMS file names. */
          if (negated) {
            prsrv_vms = 0;
          } else {
            prsrv_vms = 1;
          }
          break;
        case 'w':   /* Append the VMS version number */
          vmsver |= 1;  break;
        case o_ww:   /* Append the VMS version number as ".nnn". */
          vmsver |= 3;  break;
#endif /* VMS */

#ifdef WINDOWS_LONG_PATHS
        case o_wl:  /* Include windows paths > MAX_PATH (260 characters) */
          if (negated)
            include_windows_long_paths = 0;
          else
            include_windows_long_paths = 1;
          break;
#endif

        case o_ws:  /* Wildcards do not include directory boundaries in matches */
          wild_stop_at_dir = 1;
          break;

        case 'i':   /* Include only the following files */
          /* if nothing matches include list then still create an empty archive */
          allow_empty_archive = 1;
        case 'x':   /* Exclude following files */
          {
            add_filter((int) option, value);
          }
          free(value);
          break;
#ifdef SYMLINKS
        case 'y':   /* Store symbolic links as such */
          linkput = 1;
          break;
# ifdef WIN32
        case o_yy:  /* Mount points */
          /* Normal action is to follow normal mount points */
          if (negated) /* -yy- */
            follow_mount_points = FOLLOW_ALL;  /* Include special */
          else         /* -yy */
            follow_mount_points = FOLLOW_NONE; /* Skip mount points */
          break;

# endif
#endif /* SYMLINKS */

#ifdef IZ_CRYPT_ANY
        case 'Y':   /* Encryption method */
          encryption_method = NO_ENCRYPTION;

          if (abbrevmatch("traditional", value, CASE_INS, 1)) {
#  ifdef IZ_CRYPT_TRAD
            encryption_method = TRADITIONAL_ENCRYPTION;
#  else /* def IZ_CRYPT_TRAD */
            free(value);
            ZIPERR(ZE_PARMS,
                   "Traditional zip encryption not supported in this build");
#  endif /* def IZ_CRYPT_TRAD [else] */
          }
          if (strmatch("AES", value, 0, 3)) {
# if defined( IZ_CRYPT_AES_WG) || defined( IZ_CRYPT_AES_WG_NEW)
            if (abbrevmatch("AES128", value, CASE_INS, 5)) {
              encryption_method = AES_128_ENCRYPTION;
            } else if (abbrevmatch("AES192", value, CASE_INS, 5)) {
              encryption_method = AES_192_ENCRYPTION;
            } else if (abbrevmatch("AES256", value, CASE_INS, 4)) {
              encryption_method = AES_256_ENCRYPTION;
            }
# else
            free(value);
            ZIPERR(ZE_PARMS,
                   "AES encryption not supported in this build");
# endif
          }
          if (encryption_method == NO_ENCRYPTION) {
            /* Method specified not valid one. */
            strcpy(errbuf, "");
# ifdef IZ_CRYPT_TRAD
            strcat(errbuf, "Traditional");
# endif
# if defined( IZ_CRYPT_AES_WG) || defined( IZ_CRYPT_AES_WG_NEW)
            if (strlen(errbuf) > 0)
              strcat(errbuf, ", ");
            strcat(errbuf, "AES128, AES192, and AES256");
# endif
            zipwarn("valid encryption methods are:  ", errbuf);
            sprintf(errbuf,
                    "Option -Y:  unknown encryption method \"%s\"", value);
            free(value);
            ZIPERR(ZE_PARMS, errbuf);
          }
          free(value);
          break;
#else /* def IZ_CRYPT_ANY */
          ZIPERR(ZE_PARMS, "encryption (-Y) not supported");
#endif /* def IZ_CRYPT_ANY [else] */

        case 'z':   /* Edit zip file comment */
          zipedit = 1;  break;

        case 'Z':   /* Compression method */
          if (abbrevmatch("deflate", value, CASE_INS, 1)) {
            /* deflate */
            method = DEFLATE;
          } else if (abbrevmatch("store", value, CASE_INS, 1)) {
            /* store */
            method = STORE;
          } else if (abbrevmatch("bzip2", value, CASE_INS, 1)) {
            /* bzip2 */
#ifdef BZIP2_SUPPORT
            method = BZIP2;
#else
            ZIPERR(ZE_COMPILE, "Compression method bzip2 not enabled");
#endif
          } else if (abbrevmatch("lzma", value, CASE_INS, 1)) {
            /* LZMA */
#ifdef LZMA_SUPPORT
            method = LZMA;
#else
            ZIPERR(ZE_COMPILE, "Compression method LZMA not enabled");
#endif
          } else if (abbrevmatch("ppmd", value, CASE_INS, 1)) {
            /* PPMD */
#ifdef PPMD_SUPPORT
            method = PPMD;
#else
            ZIPERR(ZE_COMPILE, "Compression method PPMd not enabled");
#endif
          } else if (abbrevmatch("cd_only", value, CASE_INS, 3)) {
            /* cd_only */
#ifdef CD_ONLY_SUPPORT
            method = CD_ONLY;
#else
            ZIPERR(ZE_COMPILE, "Compression method cd_only not enabled");
#endif
          } else {
            strcpy(errbuf, "store, deflate");
#ifdef BZIP2_SUPPORT
            strcat(errbuf, ", bzip2");
#endif
#ifdef LZMA_SUPPORT
            strcat(errbuf, ", lzma");
#endif
#ifdef PPMD_SUPPORT
            strcat(errbuf, ", ppmd");
#endif
#ifdef CD_ONLY_SUPPORT
            strcat(errbuf, ", cd_only");
#endif
            zipwarn("valid compression methods are:  ", errbuf);
            sprintf(errbuf,
                    "Option -Z:  unknown compression method \"%s\"", value);
            free(value);
            ZIPERR(ZE_PARMS, errbuf);
          }
          free(value);
          break;

#if defined(MSDOS) || defined(OS2) || defined( VMS)
        case '$':   /* Include volume label */
          volume_label = 1; break;
#endif
#ifndef MACOS
        case '@':   /* read file names from stdin */
          comment_stream = NULL;
          s = 1;          /* defer -@ until have zipfile name */
          break;
#endif /* !MACOS */

        case o_atat: /* -@@ - read file names from file */
          {
            FILE *infile;
            char *path;

            if (value == NULL || strlen(value) == 0) {
              ZIPERR(ZE_PARMS, "-@@:  No file name to open to read names from");
            }

#if !defined(MACOS) && !defined(WINDLL)
            /* Treat "-@@ -" as "-@" */
            if (strcmp( value, "-") == 0)
            {
              comment_stream = NULL;
              s = 1;          /* defer -@ until have zipfile name */
              break;
            }
#endif /* ndef MACOS */

            infile = fopen(value, "r");
            if (infile == NULL) {
              sprintf(errbuf, "-@@:  Cannot open input file:  %s\n", value);
              ZIPERR(ZE_OPEN, errbuf);
            }
            while ((path = getnam(infile)) != NULL)
            {
              /* file argument now processed later */
              add_name(path, 0);
              free(path);
            }
            fclose(infile);
            names_from_file = 1;
          }
          break;

        case 'X':
          if (negated)
            extra_fields = 2;
          else
            extra_fields = 0;
          break;
#ifdef OS2
        case 'E':
          /* use the .LONGNAME EA (if any) as the file's name. */
          use_longname_ea = 1;
          break;
#endif
#ifdef NTSD_EAS
        case '!':
          /* use security privilege overrides */
          use_privileges = 1;
          break;

        case o_exex:  /* (-!!) */
          /* leave out security information (ACLs) */
          no_security = 1;
          break;
#endif
#ifdef RISCOS
        case '/':
          exts2swap = value; /* override Zip$Exts */
          break;
#endif
        case o_des:
          use_descriptors = 1;
          break;

#ifdef ZIP64_SUPPORT
        case o_z64:   /* Force creation of Zip64 entries */
          if (negated) {
            force_zip64 = 0;
          } else {
            force_zip64 = 1;
          }
          break;
#endif

        case o_NON_OPTION_ARG:
          /* not an option */
          /* no more options as permuting */
          /* just dash also ends up here */

          if (recurse != 2 && kk == 0 && patterns == NULL) {
            /* have all filters so convert filterlist to patterns array
               as PROCNAME needs patterns array */
            filterlist_to_patterns();
          }

          /* "--" stops arg processing for remaining args */
          /* ignore only first -- */
          if (strcmp(value, "--") == 0 && seen_doubledash == 0) {
            /* -- */
            seen_doubledash = 1;
            if (kk == 0) {
              ZIPERR(ZE_PARMS, "can't use -- before archive name");
            }

            /* just ignore as just marks what follows as non-option arguments */

          } else if (kk == 6) {
            /* value is R pattern */

            add_filter((int)'R', value);
            free(value);
            if (first_listarg == 0) {
              first_listarg = argnum;
            }
          } else switch (kk)
          {
            case 0:
              /* first non-option arg is zipfile name */
#ifdef BACKUP_SUPPORT
              /* if doing a -BT operation, archive name is created from backup
                 path and a timestamp instead of read from command line */
              if (backup_type == 0)
              {
#endif /* BACKUP_SUPPORT */
#if (!defined(MACOS))
                if (strcmp(value, "-") == 0) {  /* output zipfile is dash */
                  /* just a dash */
# ifdef WINDLL
                  ZIPERR(ZE_PARMS, "DLL can't output to stdout");
# else
                  zipstdout();
# endif
                } else
#endif /* !MACOS */
                {
                  /* name of zipfile */
                  if ((zipfile = ziptyp(value)) == NULL) {
                    ZIPERR(ZE_MEM, "was processing arguments (3)");
                  }
                  /* read zipfile if exists */
                  /*
                  if ((r = readzipfile()) != ZE_OK) {
                    ZIPERR(r, zipfile);
                  }
                  */

                  free(value);
                }
                if (show_what_doing) {
                  zfprintf(mesg, "sd: Zipfile name '%s'\n", zipfile);
                  fflush(mesg);
                }
                /* if in_path not set, use zipfile path as usual for input */
                /* in_path is used as the base path to find splits */
                if (in_path == NULL) {
                  if ((in_path = malloc(strlen(zipfile) + 1)) == NULL) {
                    ZIPERR(ZE_MEM, "was processing arguments (4)");
                  }
                  strcpy(in_path, zipfile);
                }
                /* if out_path not set, use zipfile path as usual for output */
                /* out_path is where the output archive is written */
                if (out_path == NULL) {
                  if ((out_path = malloc(strlen(zipfile) + 1)) == NULL) {
                    ZIPERR(ZE_MEM, "was processing arguments (5)");
                  }
                  strcpy(out_path, zipfile);
                }
#ifdef BACKUP_SUPPORT
              }
#endif /* BACKUP_SUPPORT */
              kk = 3;
              if (s)
              {
                /* do -@ and get names from stdin */
                /* Should be able to read names from
                   stdin and output to stdout, but
                   this was not allowed in old code.
                   This check moved to kk = 3 case to fix. */
                /* if (strcmp(zipfile, "-") == 0) {
                  ZIPERR(ZE_PARMS, "can't use - and -@ together");
                }
                */
                while ((pp = getnam(stdin)) != NULL)
                {
                  kk = 4;
                  if (recurse == 2) {
                    /* reading patterns from stdin */
                    add_filter((int)'R', pp);
                  } else {
                    /* file argument now processed later */
                    add_name(pp, 0);
                  }
                  /*
                  if ((r = PROCNAME(pp)) != ZE_OK) {
                    if (r == ZE_MISS)
                      zipwarn("name not matched: ", pp);
                    else {
                      ZIPERR(r, pp);
                    }
                  }
                  */
                  free(pp);
                }
                s = 0;
              }
              if (recurse == 2) {
                /* rest are -R patterns */
                kk = 6;
              }
#ifdef BACKUP_SUPPORT
              /* if using -BT, then value is really first input argument */
              if (backup_type == 0)
#endif
                break;

            case 3:  case 4:
              /* no recurse and -r file names */
              /* can't read filenames -@ and input - from stdin at
                 same time */
              if (s == 1 && strcmp(value, "-") == 0) {
                ZIPERR(ZE_PARMS, "can't read input (-) and filenames (-@) both from stdin");
              }
              /* add name to list for later processing */
              add_name(value, seen_doubledash);
              /*
              if ((r = PROCNAME(value)) != ZE_OK) {
                if (r == ZE_MISS)
                  zipwarn("name not matched: ", value);
                else {
                  ZIPERR(r, value);
                }
              }
              */
              free(value);  /* Added by Polo from forum */
              if (kk == 3) {
                first_listarg = argnum;
                kk = 4;
              }
              break;

            } /* switch kk */
            break;

        default:
          /* should never get here as get_option will exit if not in table,
             unless option is in table but no case for it */
          sprintf(errbuf, "no such option ID: %ld", option);
          ZIPERR(ZE_PARMS, errbuf);

    }  /* switch */
  } /* while */


  /* ========================================================== */

#ifdef SHOW_PARSED_COMMAND
  if (parsed_args) {
    parsed_args[parsed_arg_count] = NULL; /* NULL-terminate parsed_args[]. */
  }
#endif /* def SHOW_PARSED_COMMAND */


  /* do processing of command line and one-time tasks */

  if (show_what_doing) {
    zfprintf(mesg, "sd: Command line read\n");
    fflush(mesg);
  }

  /* Check method-level suffix options. */
  if (mthd_lvl[0].suffixes == NULL)
  {
    /* We expect _some_ non-NULL default Store suffix list, even if ""
     * is compiled in.  This check must be done before setting an empty
     * list ("", ":", or ";") to NULL (below).
     */
    ZIPERR(ZE_PARMS, "missing Store suffix list");
  }

  for (i = 0; mthd_lvl[ i].method >= 0; i++)
  {
    /* Replace an empty suffix list ("", ":", or ";") with NULL. */
    if ((mthd_lvl[i].suffixes != NULL) &&
        ((mthd_lvl[i].suffixes[0] == '\0') ||
         !strcmp(mthd_lvl[i].suffixes, ";") ||
         !strcmp(mthd_lvl[i].suffixes, ":")))
    {
      mthd_lvl[i].suffixes = NULL;     /* NULL out an empty list. */
    }
  }

  /* Show method-level suffix lists. */
  if (show_suffixes)
  {
    char level_str[ 8];
    char *suffix_str;
    int any = 0;

    zfprintf( mesg, "   Method[-lvl]=Suffix_list\n");
    for (i = 0; mthd_lvl[ i].method >= 0; i++)
    {
      suffix_str = mthd_lvl[ i].suffixes;
      /* "-v": Show all methods.  Otherwise, only those with a suffix list. */
      if (verbose || (suffix_str != NULL))
      {
        /* "-lvl" string, if a non-default level was specified. */
        if ((mthd_lvl[ i].level_sufx <= 0) || (suffix_str == NULL))
          strcpy( level_str, "  ");
        else
          sprintf( level_str, "-%d", mthd_lvl[ i].level_sufx);

        /* Display an empty string, if none specified. */
        if (suffix_str == NULL)
          suffix_str = "";

        zfprintf(mesg, "     %8s%s=%s\n",
                 mthd_lvl[ i].method_str, level_str, suffix_str);
        if (suffix_str != NULL)
          any = 1;
      }
    }
    if (any == 0)
    {
      zfprintf( mesg, "   (All suffix lists empty.)\n");
    }
    zfprintf( mesg, "\n");
    ZIPERR(ZE_ABORT, "show suffixes");
  }

  /* show command line args */
  if (show_args || show_parsed_args) {
    if (show_args) {
      zfprintf(mesg, "command line:\n");
      for (i = 0; args[i]; i++) {
        zfprintf(mesg, "'%s'  ", args[i]);
      }
      zfprintf(mesg, "\n");
    }

#ifdef SHOW_PARSED_COMMAND
    if (show_parsed_args) {
      if (parsed_args == NULL) {
        zfprintf(mesg, "\nno parsed command line\n");
      }
      else
      {
        zfprintf(mesg, "\nparsed command line:\n");
        for (i = 0; parsed_args[i]; i++) {
          zfprintf(mesg, "'%s'  ", parsed_args[i]);
        }
        zfprintf(mesg, "\n");
      }
    }
#endif
  }

#ifdef SHOW_PARSED_COMMAND
  /* free up parsed command line */
  if (parsed_args) {
    for (i = 0; parsed_args[i]; i++) {
      free(parsed_args[i]);
    }
    free(parsed_args);
  }
#endif

  if (show_args || show_parsed_args) {
    ZIPERR(ZE_ABORT, "show command line");
  }


  /* show all options */
  if (show_options) {
    zprintf("available options:\n");
    zprintf(" %-2s  %-18s %-4s %-3s %-30s\n", "sh", "long", "val", "neg", "description");
    zprintf(" %-2s  %-18s %-4s %-3s %-30s\n", "--", "----", "---", "---", "-----------");
    for (i = 0; options[i].option_ID; i++) {
      zprintf(" %-2s  %-18s ", options[i].shortopt, options[i].longopt);
      switch (options[i].value_type) {
        case o_NO_VALUE:
          zprintf("%-4s ", "");
          break;
        case o_REQUIRED_VALUE:
          zprintf("%-4s ", "req");
          break;
        case o_OPTIONAL_VALUE:
          zprintf("%-4s ", "opt");
          break;
        case o_VALUE_LIST:
          zprintf("%-4s ", "list");
          break;
        case o_ONE_CHAR_VALUE:
          zprintf("%-4s ", "char");
          break;
        case o_NUMBER_VALUE:
          zprintf("%-4s ", "num");
          break;
        case o_OPT_EQ_VALUE:
          zprintf("%-4s ", "=val");
          break;
        default:
          zprintf("%-4s ", "unk");
      }
      switch (options[i].negatable) {
        case o_NEGATABLE:
          zprintf("%-3s ", "neg");
          break;
        case o_NOT_NEGATABLE:
          zprintf("%-3s ", "");
          break;
        default:
          zprintf("%-3s ", "unk");
      }
      if (options[i].name) {
        zprintf("%-30s\n", options[i].name);
      }
      else
        zprintf("\n");
    }
    RETURN(finish(ZE_OK));
  }


#ifdef WINDLL
  if (zipfile == NULL) {
    ZIPERR(ZE_PARMS, "DLL can't Zip from/to stdin/stdout");
  }
#endif

#ifdef CHANGE_DIRECTORY
  /* change dir
     Change from startup_dir to user given working_dir.  Restoring
     the startup directory is done in freeup() (if needed). */
  if (working_dir) {
    /* save startup dir */
    if (startup_dir)
      free(startup_dir);
    if ((startup_dir = GETCWD(NULL, 0)) == NULL) {
      sprintf(errbuf, "saving dir: %s\n  %s", startup_dir, strerror(errno));
      free(working_dir);
      working_dir = NULL;
      ZIPERR(ZE_PARMS, errbuf);
    }

    /* change to working dir */
    if (CHDIR(working_dir)) {
      sprintf(errbuf, "changing to dir: %s\n  %s", working_dir, strerror(errno));
      free(working_dir);
      working_dir = NULL;
      ZIPERR(ZE_PARMS, errbuf);
    }

    if (noisy) {
      char *current_dir = NULL;
      
      if ((current_dir = GETCWD(NULL, 0)) == NULL) {
        sprintf(errbuf, "getting current dir: %s", strerror(errno));
        ZIPERR(ZE_PARMS, errbuf);
      }
      sprintf(errbuf, "current directory set to: %s", current_dir);
      zipmessage(errbuf, "");
      free(current_dir);
    }
  }
#endif

  
#ifdef BACKUP_SUPPORT

  /* ----------------------------------------- */
  /* Backup */

  /* Set up for -BT backup using control file */
  if (backup_type && fix) {
    ZIPERR(ZE_PARMS, "can't use -F or -FF with -BT");
  }
  if (have_out && (backup_type)) {
    ZIPERR(ZE_PARMS, "can't specify output file when using -BT");
  }
  if (backup_type && (backup_dir == NULL)) {
    ZIPERR(ZE_PARMS, "-BT without -BD");
  }
  if (backup_type && (backup_name == NULL)) {
    ZIPERR(ZE_PARMS, "-BT without -BN");
  }
  if ((backup_dir != NULL) && (!backup_type)) {
    ZIPERR(ZE_PARMS, "-BD without -BT");
  }
  if ((backup_name != NULL) && (!backup_type)) {
    ZIPERR(ZE_PARMS, "-BN without -BT");
  }
  if ((backup_control_dir != NULL) && (!backup_type)) {
    ZIPERR(ZE_PARMS, "-BC without -BT");
  }
  if ((backup_log_dir != NULL) && (!backup_type)) {
    ZIPERR(ZE_PARMS, "-BL without -BT");
  }

  if (backup_log_dir && (logfile_path || use_outpath_for_log)) {
    ZIPERR(ZE_PARMS, "Can't use -BL with -lf or -lF");
  }

  if (backup_type) {
    /* control dir defaults to backup dir */
    if (backup_control_dir == NULL) {
      if ((backup_control_dir = malloc(strlen(backup_dir) + 1)) == NULL) {
        ZIPERR(ZE_MEM, "backup control path (1a)");
      }
      strcpy(backup_control_dir, backup_dir);
    }
    /* build control file path */
    i = strlen(backup_control_dir) + 1 + strlen(backup_name) + 1 +
        strlen(BACKUP_CONTROL_FILE_NAME) + 1;
    if (backup_control_path)
      free(backup_control_path);
    if ((backup_control_path = malloc(i)) == NULL) {
      ZIPERR(ZE_MEM, "backup control path (1b)");
    }
    strcpy(backup_control_path, backup_control_dir);
# ifndef VMS
    strcat(backup_control_path, "/");
# endif /* ndef VMS */
    strcat(backup_control_path, backup_name);
    strcat(backup_control_path, "_");
    strcat(backup_control_path, BACKUP_CONTROL_FILE_NAME);

    free(backup_control_dir);
    backup_control_dir = NULL;
  }

  if (backup_type == BACKUP_DIFF || backup_type == BACKUP_INCR) {
    /* read backup control file to get list of archives to read */
    int i;
    int j;
    char prefix[7];
    FILE *fcontrolfile = NULL;
    char *linebuf = NULL;

    /* see if we can open this path */
    if ((fcontrolfile = fopen(backup_control_path, "r")) == NULL) {
      sprintf(errbuf, "could not open control file: %s",
              backup_control_path);
      ZIPERR(ZE_OPEN, errbuf);
    }

    /* read backup control file */
    if ((linebuf = (char *)malloc((FNMAX + 101) * sizeof(char))) == NULL) {
      ZIPERR(ZE_MEM, "backup line buf");
    }
    i = 0;
    while (fgets(linebuf, FNMAX + 10, fcontrolfile) != NULL) {
      int baselen;
      char ext[6];
      if (strlen(linebuf) >= FNMAX + 9) {
        sprintf(errbuf, "archive path at control file line %d truncated", i + 1);
        zipwarn(errbuf, "");
      }
      baselen = strlen(linebuf);
      if (linebuf[baselen - 1] == '\n') {
        linebuf[baselen - 1] = '\0';
      }
      if (strlen(linebuf) < 7) {
        sprintf(errbuf, "improper line in backup control file at line %d", i);
        ZIPERR(ZE_BACKUP, errbuf);
      }
      strncpy(prefix, linebuf, 6);
      prefix[6] = '\0';
      for (j = 0; j < 7; j++) prefix[j] = tolower(prefix[j]);

      if (strcmp(prefix, "full: ") == 0) {
        /* full backup path */
        if (backup_full_path != NULL) {
          ZIPERR(ZE_BACKUP, "more than one full backup entry in control file");
        }
        if ((backup_full_path = (char *)malloc(strlen(linebuf) + 1)) == NULL) {
          ZIPERR(ZE_MEM, "backup archive path (1)");
        }
        for (j = 6; linebuf[j]; j++) backup_full_path[j - 6] = linebuf[j];
        backup_full_path[j - 6] = '\0';
        baselen = strlen(backup_full_path) - 4;
        for (j = baselen; backup_full_path[j]; j++) ext[j - baselen] = tolower(backup_full_path[j]);
        ext[4] = '\0';
        if (strcmp(ext, ".zip") != 0) {
          strcat(backup_full_path, ".zip");
        }

      } else if (strcmp(prefix, "diff: ") == 0 || strcmp(prefix, "incr: ") == 0) {
        /* path of diff or incr archive to be read for incremental backup */
        if (backup_type == BACKUP_INCR) {
          char *path;
          if ((path = (char *)malloc(strlen(linebuf))) == NULL) {
            ZIPERR(ZE_MEM, "backup archive path (2)");
          }
          for (j = 6; linebuf[j]; j++) path[j - 6] = linebuf[j];
          path[j - 6] = '\0';
          baselen = strlen(path) - 4;
          for (j = baselen; path[j]; j++) ext[j - baselen] = tolower(path[j]);
          ext[4] = '\0';
          if (strcmp(ext, ".zip") != 0) {
            strcat(path, ".zip");
          }
          add_apath(path);
          free(path);
        }
      }
      i++;
    }
    fclose(fcontrolfile);
  }

  if ((backup_type == BACKUP_DIFF || backup_type == BACKUP_INCR) && backup_full_path == NULL) {
    zipwarn("-BT set to DIFF or INCR but no baseline full archive found", "");
    zipwarn("check control file or perform a FULL backup first to create baseline", "");
    ZIPERR(ZE_BACKUP, "no full backup archive specified or found");
  }

  if (backup_type) {
    /* build output path */
    struct tm *now;
    time_t clocktime;

    /* get current time */
    if ((backup_start_datetime = malloc(20)) == NULL) {
      ZIPERR(ZE_MEM, "backup_start_datetime");
    }
    time(&clocktime);
    now = localtime(&clocktime);
    sprintf(backup_start_datetime, "%04d%02d%02d_%02d%02d%02d",
      now->tm_year + 1900, now->tm_mon + 1, now->tm_mday, now->tm_hour,
      now->tm_min, now->tm_sec);

    i = strlen(backup_dir) + 1 + strlen(backup_name) + 1;
    if ((backup_output_path = malloc(i + 290)) == NULL) {
      ZIPERR(ZE_MEM, "backup control path (2)");
    }

    if (backup_type == BACKUP_FULL) {
      /* path of full archive */
      strcpy(backup_output_path, backup_dir);
# ifndef VMS
      strcat(backup_output_path, "/");
# endif /* ndef VMS */
      strcat(backup_output_path, backup_name);
      strcat(backup_output_path, "_full_");
      strcat(backup_output_path, backup_start_datetime);
      strcat(backup_output_path, ".zip");
      if ((backup_full_path = malloc(strlen(backup_output_path) + 1)) == NULL) {
        ZIPERR(ZE_MEM, "backup full path");
      }
      strcpy(backup_full_path, backup_output_path);

    } else if (backup_type == BACKUP_DIFF) {
      /* path of diff archive */
      strcpy(backup_output_path, backup_full_path);
      /* chop off .zip */
      backup_output_path[strlen(backup_output_path) - 4] = '\0';
      strcat(backup_output_path, "_diff_");
      strcat(backup_output_path, backup_start_datetime);
      strcat(backup_output_path, ".zip");

    } else if (backup_type == BACKUP_INCR) {
      /* path of incr archvie */
      strcpy(backup_output_path, backup_full_path);
      /* chop off .zip */
      backup_output_path[strlen(backup_output_path) - 4] = '\0';
      strcat(backup_output_path, "_incr_");
      strcat(backup_output_path, backup_start_datetime);
      strcat(backup_output_path, ".zip");
    }

    if (backup_type == BACKUP_DIFF || backup_type == BACKUP_INCR) {
      /* make sure full backup exists */
      FILE *ffull = NULL;

      if (backup_full_path == NULL) {
        ZIPERR(ZE_BACKUP, "no full backup path in control file");
      }
      if ((ffull = fopen(backup_full_path, "r")) == NULL) {
        zipwarn("Can't open:  ", backup_full_path);
        ZIPERR(ZE_OPEN, "full backup can't be opened");
      }
      fclose(ffull);
    }

    zipfile = ziptyp(backup_full_path);
    if (show_what_doing) {
      zfprintf(mesg, "sd: Zipfile name '%s'\n", zipfile);
      fflush(mesg);
    }
    if ((in_path = malloc(strlen(zipfile) + 1)) == NULL) {
      ZIPERR(ZE_MEM, "was processing arguments (6)");
    }
    strcpy(in_path, zipfile);

    out_path = ziptyp(backup_output_path);

    zipmessage("Backup output path: ", out_path);
  }


  if (backup_log_dir) {
    if (strlen(backup_log_dir) == 0)
    {
      /* empty backup log path - default to archive path */
      int i;
      i = strlen(out_path);
      /* remove .zip and replace with .log */
      if ((logfile_path = malloc(i + 1)) == NULL) {
        ZIPERR(ZE_MEM, "backup path (1)");
      }
      strcpy(logfile_path, out_path);
      strcpy(logfile_path + i - 4, ".log");
    }
    else
    {
      /* use given path for backup log */
      char *p;
      int i;

      /* find start of archive name */
      for (p = out_path + strlen(out_path); p >= out_path; p--) {
        if (*p == '/' || *p == '\\')
          break;
      }
      if (p > out_path) {
        /* found / or \ - skip it */
        p++;
      } else {
        /* use entire out_path */
      }
      i = strlen(backup_log_dir) + 1 + strlen(p) + 1;
      if ((logfile_path = malloc(i)) == NULL) {
        ZIPERR(ZE_MEM, "backup path (2)");
      }
      strcpy(logfile_path, backup_log_dir);
# ifndef VMS
      strcat(logfile_path, "/");
# endif /* ndef VMS */
      strcat(logfile_path, p);
      strcpy(logfile_path + i - 4, ".log");
    }

    free(backup_log_dir);
    backup_log_dir = NULL;
  }

  /* ----------------------------------------- */

#endif /* BACKUP_SUPPORT */


  if (use_outpath_for_log) {
    if (logfile_path) {
      zipwarn("both -lf and -lF used, -lF ignored", "");
    } else {
      int out_path_len;

      if (out_path == NULL) {
        ZIPERR(ZE_PARMS, "-lF but no out path");
      }
      out_path_len = strlen(out_path);
      if ((logfile_path = malloc(out_path_len + 5)) == NULL) {
        ZIPERR(ZE_MEM, "logfile path");
      }
      if (out_path_len > 4) {
        char *ext = out_path + out_path_len - 4;
        if (STRCASECMP(ext, ".zip") == 0) {
          /* remove .zip before adding .log */
          strncpy(logfile_path, out_path, out_path_len - 4);
          logfile_path[out_path_len - 4] = '\0';
        } else {
          /* just append .log */
          strcpy(logfile_path, out_path);
        }
      } else {
        strcpy(logfile_path, out_path);
      }
      strcat(logfile_path, ".log");
    }
  }


  /* open log file */
  if (logfile_path) {
    char mode[10];
    char *p;
    char *lastp;

    if (strlen(logfile_path) == 1 && logfile_path[0] == '-') {
      /* log to stdout */
      if (zip_to_stdout) {
        ZIPERR(ZE_PARMS, "cannot send both zip and log output to stdout");
      }
      if (logfile_append) {
        ZIPERR(ZE_PARMS, "cannot use -la when logging to stdout");
      }
      if (verbose) {
        ZIPERR(ZE_PARMS, "cannot use -v when logging to stdout");
      }
      /* to avoid duplicate output, turn off normal messages to stdout */
      noisy = 0;
      /* send output to stdout */
      logfile = stdout;

    } else {
      /* not stdout */
      /* if no extension add .log */
      p = logfile_path;
      /* find last / */
      lastp = NULL;
      for (p = logfile_path; (p = MBSRCHR(p, '/')) != NULL; p++) {
        lastp = p;
      }
      if (lastp == NULL)
        lastp = logfile_path;
      if (MBSRCHR(lastp, '.') == NULL) {
        /* add .log */
        if ((p = malloc(strlen(logfile_path) + 5)) == NULL) {
          ZIPERR(ZE_MEM, "logpath");
        }
        strcpy(p, logfile_path);
        strcat(p, ".log");
        free(logfile_path);
        logfile_path = p;
      }

      if (logfile_append) {
        sprintf(mode, "a");
      } else {
        sprintf(mode, "w");
      }
      if ((logfile = zfopen(logfile_path, mode)) == NULL) {
        sprintf(errbuf, "could not open logfile '%s'", logfile_path);
        ZIPERR(ZE_PARMS, errbuf);
      }
    }
    if (logfile != stdout) {
      /* At top put start time and command line */

      /* get current time */
      struct tm *now;

      time(&clocktime);
      now = localtime(&clocktime);

      zfprintf(logfile, "---------\n");
      zfprintf(logfile, "Zip log opened %s", asctime(now));
      zfprintf(logfile, "command line arguments:\n ");
      for (i = 1; args[i]; i++) {
        size_t j;
        int has_space = 0;

        for (j = 0; j < strlen(args[i]); j++) {
          if (isspace(args[i][j])) {
            has_space = 1;
            break;
          }
        }
        if (has_space) {
          zfprintf(logfile, "\"%s\" ", args[i]);
        }
        else {
          zfprintf(logfile, "%s ", args[i]);
        }
      }
      zfprintf(logfile, "\n\n");
      fflush(logfile);
    }
  } else {
    /* only set logall if logfile open */
    logall = 0;
  }


  /* Show process ID. */
#if !defined( VMS) && defined( ENABLE_USER_PROGRESS)
  if (show_pid)
  {
    fprintf( mesg, "PID = %d \n", getpid());
  }
#endif /* !defined( VMS) && defined( ENABLE_USER_PROGRESS) */



  /* process command line options */


#ifdef IZ_CRYPT_ANY

# if defined( IZ_CRYPT_AES_WG) || defined( IZ_CRYPT_AES_WG_NEW)
  if ((key == NULL) && (encryption_method != NO_ENCRYPTION)) {
    key_needed = 1;
  }
# endif /* defined( IZ_CRYPT_AES_WG) || defined( IZ_CRYPT_AES_WG_NEW) */

  /* ----------------- */
  if (key_needed && noisy) {
    if (encryption_method == TRADITIONAL_ENCRYPTION) {
      zipmessage("Using traditional (weak) zip encryption", "");
    } else if (encryption_method == AES_128_ENCRYPTION) {
      zipmessage("Using AES 128 encryption", "");
    } else if (encryption_method == AES_192_ENCRYPTION) {
      zipmessage("Using AES 192 encryption", "");
    } else if (encryption_method == AES_256_ENCRYPTION) {
      zipmessage("Using AES 256 encryption", "");
    }
  }

  if (key_needed) {
    int i;

# ifdef IZ_CRYPT_AES_WG_NEW
    /* may be pulled from the new code later */
#  define MAX_PWD_LENGTH 128
# endif /* def IZ_CRYPT_AES_WG_NEW */

# if defined(IZ_CRYPT_AES_WG) || defined(IZ_CRYPT_AES_WG_NEW)
#  define MAX_PWLEN temp_pwlen
    int temp_pwlen;

    if (encryption_method <= TRADITIONAL_ENCRYPTION)
        MAX_PWLEN = IZ_PWLEN;
    else
        MAX_PWLEN = MAX_PWD_LENGTH;
# else
#  define MAX_PWLEN IZ_PWLEN
# endif

    if ((key = malloc(MAX_PWLEN+2)) == NULL) {
      ZIPERR(ZE_MEM, "was getting encryption password (1)");
    }
    r = simple_encr_passwd(ZP_PW_ENTER, key, MAX_PWLEN+1);
    if (r == -1) {
      sprintf(errbuf, "password too long - max %d", MAX_PWLEN);
      ZIPERR(ZE_PARMS, errbuf);
    }
    if (*key == '\0') {
      ZIPERR(ZE_PARMS, "zero length password not allowed");
    }
    if (force_ansi_key) {
      for (i = 0; key[i]; i++) {
        if (key[i] < 32 || key[i] > 126) {
          zipwarn("password must be ANSI (unless use -pn)", "");
          ZIPERR(ZE_PARMS, "non-ANSI character in password");
        }
      }
    }

    if ((encryption_method == AES_256_ENCRYPTION) && (strlen(key) < 24)) {
      if (allow_short_key)
      {
        zipwarn("Password shorter than minimum of 24 chars", "");
      }
      else
      {
        zipwarn(
          "AES256 password must be at least 24 chars (longer is better)", "");
        ZIPERR(ZE_PARMS, "AES256 password too short");
      }
    }
    if ((encryption_method == AES_192_ENCRYPTION) && (strlen(key) < 20)) {
      if (allow_short_key)
      {
        zipwarn("Password shorter than minimum of 20 chars", "");
      }
      else
      {
        zipwarn(
          "AES192 password must be at least 20 chars (longer is better)", "");
        ZIPERR(ZE_PARMS, "AES192 password too short");
      }
    }
    if ((encryption_method == AES_128_ENCRYPTION) && (strlen(key) < 16)) {
      if (allow_short_key)
      {
        zipwarn("Password shorter than minimum of 16 chars", "");
      }
      else
      {
        zipwarn(
          "AES128 password must be at least 16 chars (longer is better)", "");
        ZIPERR(ZE_PARMS, "AES128 password too short");
      }
    }
    
    if ((e = malloc(MAX_PWLEN+2)) == NULL) {
      ZIPERR(ZE_MEM, "was verifying encryption password (1)");
    }
#ifndef ZIP_DLL_LIB
    /* Only verify password if not getting it from a callback. */
    r = simple_encr_passwd(ZP_PW_VERIFY, e, MAX_PWLEN+1);
    r = strcmp(key, e);
    free((zvoid *)e);
    if (r) {
      ZIPERR(ZE_PARMS, "password verification failed");
    }
#endif
  }
  if (key) {
    /* if -P "" could get here */
    if (*key == '\0') {
      ZIPERR(ZE_PARMS, "zero length password not allowed");
    }
  }
  /* ----------------- */
#endif /* def IZ_CRYPT_ANY */

  /* Check path prefix */
  if (path_prefix) {
    int i;
    char last_c = '\0';
    char c;
    char *allowed_other_chars = "!@#$%^&()-_=+/[]{}|";

    for (i = 0; (c = path_prefix[i]); i++) {
      if (!isprint(c)) {
        if (path_prefix_mode == 1)
          ZIPERR(ZE_PARMS, "option -pa (--prefix_added): non-print char in prefix");
        else
          ZIPERR(ZE_PARMS, "option -pp (--prefix_path): non-print char in prefix");
      }
#if (defined(MSDOS) || defined(OS2)) && !defined(WIN32)
      if (c == '\\') {
        c = '/';
        path_prefix[i] = c;
      }
#endif
      if (!isalnum(c) && !strchr(allowed_other_chars, c)) {
        if (path_prefix_mode == 1)
          strcpy(errbuf, "option -pa (--prefix_added), only alphanum and \"");
        else
          strcpy(errbuf, "option -pp (--prefix_path), only alphanum and \"");
        strcat(errbuf, allowed_other_chars);
        strcat(errbuf, "\" allowed");
        ZIPERR(ZE_PARMS, errbuf);
      }
      if (last_c == '.' && c == '.') {
        if (path_prefix_mode == 1)
          ZIPERR(ZE_PARMS, "option -pa (--prefix_added): \"..\" not allowed");
        else
          ZIPERR(ZE_PARMS, "option -pp (--prefix_path): \"..\" not allowed");
      }
      last_c = c;
    }
  }


  if (split_method && out_path) {
    /* if splitting, the archive name must have .zip extension */
    int plen = strlen(out_path);
    char *out_path_ext;

#ifdef VMS
    /* On VMS, adjust plen (and out_path_ext) to avoid the file version. */
    plen -= strlen( vms_file_version( out_path));
#endif /* def VMS */
    out_path_ext = out_path+ plen- 4;

    if (plen < 4 ||
        out_path_ext[0] != '.' ||
        toupper(out_path_ext[1]) != 'Z' ||
        toupper(out_path_ext[2]) != 'I' ||
        toupper(out_path_ext[3]) != 'P') {
      ZIPERR(ZE_PARMS, "archive name must end in .zip for splits");
    }
  }


  if (verbose && (dot_size == 0) && (dot_count == 0)) {
    /* now default to default 10 MiB dot size */
    dot_size = 10 * 0x100000;
    /* show all dots as before if verbose set and dot_size not set (dot_count = 0) */
    /* maybe should turn off dots in default verbose mode */
    /* dot_size = -1; */
  }

  /* done getting -R filters so convert filterlist if not done */
  if (pcount && patterns == NULL) {
    filterlist_to_patterns();
  }

#if (defined(MSDOS) || defined(OS2)) && !defined(WIN32)
  if ((kk == 3 || kk == 4) && volume_label == 1) {
    /* read volume label */
    PROCNAME(NULL);
    kk = 4;
  }
#endif

  if (have_out && kk == 3) {
    copy_only = 1;
    action = ARCHIVE;
  }

#ifdef CD_ONLY_SUPPORT
  if (method == CD_ONLY) {
    if (!copy_only) {
      ZIPERR(ZE_PARMS, "cd_only compression only available in copy mode");
    }
    cd_only = 1;
  }
#endif
  
  if (have_out && namecmp(in_path, out_path) == 0) {
    sprintf(errbuf, "--out path must be different than in path: %s", out_path);
    ZIPERR(ZE_PARMS, errbuf);
  }

  if (fix && diff_mode) {
    ZIPERR(ZE_PARMS, "can't use --diff (-DF) with fix (-F or -FF)");
  }

  if (action == ARCHIVE && !have_out && !show_files) {
    ZIPERR(ZE_PARMS, "-U (--copy) requires -O (--out)");
  }

  if (fix && !have_out) {
    zipwarn("fix options -F and -FF require --out:\n",
            "                     zip -F indamagedarchive --out outfixedarchive");
    ZIPERR(ZE_PARMS, "fix options require --out");
  }

  if (fix && !copy_only) {
    ZIPERR(ZE_PARMS, "no other actions allowed when fixing archive (-F or -FF)");
  }

#ifdef BACKUP_SUPPORT
  if (!have_out && diff_mode && !backup_type) {
    ZIPERR(ZE_PARMS, "-DF (--diff) requires -O (--out)");
  }
#endif

  if (kk < 3 && diff_mode == 1) {
    ZIPERR(ZE_PARMS, "-DF (--diff) requires an input archive\n");
  }
  if (kk < 3 && diff_mode == 2) {
    ZIPERR(ZE_PARMS, "-DI (--incr) requires an input archive\n");
  }

  if (diff_mode && (action == ARCHIVE || action == DELETE)) {
    ZIPERR(ZE_PARMS, "can't use --diff (-DF) with -d or -U");
  }

  if (action != ARCHIVE && (recurse == 2 || pcount) && first_listarg == 0 &&
      !filelist && (kk < 3 || (action != UPDATE && action != FRESHEN))) {
    ZIPERR(ZE_PARMS, "nothing to select from");
  }

/*
  -------------------------------------
  end of new command line code
  -------------------------------------
*/





  /* Check option combinations */

  if (action == DELETE && (method != BEST || dispose || recurse ||
      key != NULL || comadd || zipedit)) {
    zipwarn("invalid option(s) used with -d; ignored.","");
    /* reset flags - needed? */
    method  = BEST;
    dispose = 0;
    recurse = 0;
    if (key != NULL) {
      free((zvoid *)key);
      key   = NULL;
    }
    comadd  = 0;
    zipedit = 0;
  }
  if (action == ARCHIVE &&
      ((method != BEST && method != CD_ONLY) || dispose || recurse /* || comadd || zipedit */)) {
    zipwarn("can't set method, move, recurse, or comments with copy mode","");
    /* reset flags - needed? */
    method  = BEST;
    dispose = 0;
    recurse = 0;
    comadd  = 0;
    zipedit = 0;
  }
  if (linkput && dosify)
    {
      zipwarn("can't use -y with -k, -y ignored", "");
      linkput = 0;
    }
  if (fix == 1 && adjust)
    {
      zipwarn("can't use -F with -A, -F ignored", "");
      fix = 0;
    }
  if (fix == 2 && adjust)
    {
      zipwarn("can't use -FF with -A, -FF ignored", "");
      fix = 0;
    }

  if (split_method && (fix || adjust)) {
    ZIPERR(ZE_PARMS, "can't create split archive while fixing or adjusting\n");
  }

#if defined(EBCDIC)  && !defined(ZOS_UNIX)
  if (aflag==FT_ASCII_TXT && !translate_eol) {
    /* Translation to ASCII implies EOL translation!
     * (on z/OS, consistent EOL translation is controlled separately)
     * The default translation mode is "UNIX" mode (single LF terminators).
     */
    translate_eol = 2;
  }
#endif
#ifdef CMS_MVS
  if (aflag==FT_ASCII_TXT && bflag)
    ZIPERR(ZE_PARMS, "can't use -a with -B");
#endif
#if defined( UNIX) && defined( __APPLE__)
  if (data_fork_only && sequester)
    {
      zipwarn("can't use -as with -df, -as ignored", "");
      sequester = 0;
    }
#endif /* defined( UNIX) && defined( __APPLE__) */

#ifdef VMS
  if (!extra_fields && vms_native) {
    zipwarn("can't use -V with -X-, -V ignored", "");
    vms_native = 0;
  }
  if (vms_native && translate_eol)
    ZIPERR(ZE_PARMS, "can't use -V with -l or -ll");
#endif

#if (!defined(MACOS))
  if (kk < 3) {               /* zip used as filter */
# ifdef WINDLL
    ZIPERR(ZE_PARMS, "DLL can't input/output to stdin/stdout");
# else
    zipstdout();
    comment_stream = NULL;

    if (s) {
      ZIPERR(ZE_PARMS, "can't use - (stdin) and -@ together");
    }

    /* Add stdin ("-") to the member list. */
    if ((r = procname("-", 0)) != ZE_OK) {
      if (r == ZE_MISS) {
        if (bad_open_is_error) {
          zipwarn("name not matched: ", "-");
          ZIPERR(ZE_OPEN, "-");
        } else {
          zipwarn("name not matched: ", "-");
        }
      } else {
        ZIPERR(r, "-");
      }


    }

    /* Specify stdout ("-") as the archive. */
    /* Unreached/unreachable message? */
    if (isatty(1))
      ziperr(ZE_PARMS, "cannot write zip file to terminal");
    if ((out_path = malloc(4)) == NULL)
      ziperr(ZE_MEM, "was processing arguments for stdout");
    strcpy( out_path, "-");
# endif

    kk = 4;
  }
#endif /* !MACOS */

  if (zipfile && !strcmp(zipfile, "-")) {
    if (show_what_doing) {
      zfprintf(mesg, "sd: Zipping to stdout\n");
      fflush(mesg);
    }
    zip_to_stdout = 1;
  }

  if (test && zip_to_stdout) {
    test = 0;
    zipwarn("can't use -T on stdout, -T ignored", "");
  }
  if (split_method && (d || zip_to_stdout)) {
    ZIPERR(ZE_PARMS, "can't create split archive with -d or -g or on stdout\n");
  }
  if ((action != ADD || d) && zip_to_stdout) {
    ZIPERR(ZE_PARMS, "can't use -d, -f, -u, -U, or -g on stdout\n");
  }
  if ((action != ADD || d) && filesync) {
    ZIPERR(ZE_PARMS, "can't use -d, -f, -u, -U, or -g with filesync -FS\n");
  }


  if (noisy) {
    if (fix == 1)
      zipmessage("Fix archive (-F) - assume mostly intact archive", "");
    else if (fix == 2)
      zipmessage("Fix archive (-FF) - salvage what can", "");
  }



  /* ----------------------------------------------- */


  /* If -FF we do it all here */
  if (fix == 2) {

    /* Open zip file and temporary output file */
    if (show_what_doing) {
      zfprintf(mesg, "sd: Open zip file and create temp file (-FF)\n");
      fflush(mesg);
    }
    diag("opening zip file and creating temporary zip file");
    x = NULL;
    tempzn = 0;
    if (show_what_doing) {
      zfprintf(mesg, "sd: Creating new zip file (-FF)\n");
      fflush(mesg);
    }
#if defined(UNIX) && !defined(NO_MKSTEMP)
    {
      int yd;
      int i;

      /* use mkstemp to avoid race condition and compiler warning */

      if (tempath != NULL)
      {
        /* if -b used to set temp file dir use that for split temp */
        if ((tempzip = malloc(strlen(tempath) + 12)) == NULL) {
          ZIPERR(ZE_MEM, "allocating temp filename (1)");
        }
        strcpy(tempzip, tempath);
        if (lastchar(tempzip) != '/')
          strcat(tempzip, "/");
      }
      else
      {
        /* create path by stripping name and appending template */
        if ((tempzip = malloc(strlen(out_path) + 12)) == NULL) {
        ZIPERR(ZE_MEM, "allocating temp filename (2)");
        }
        strcpy(tempzip, out_path);
        for (i = strlen(tempzip); i > 0; i--) {
          if (tempzip[i - 1] == '/')
            break;
        }
        tempzip[i] = '\0';
      }
      strcat(tempzip, "ziXXXXXX");

      if ((yd = mkstemp(tempzip)) == EOF) {
        ZIPERR(ZE_TEMP, tempzip);
      }
      if (show_what_doing) {
        zfprintf(mesg, "sd: Temp file (1u): %s\n", tempzip);
        fflush(mesg);
      }
      if ((y = fdopen(yd, FOPW_TMP)) == NULL) {
        ZIPERR(ZE_TEMP, tempzip);
      }
    }
#else
    if ((tempzip = tempname(out_path)) == NULL) {
      ZIPERR(ZE_TEMP, out_path);
    }
    if (show_what_doing) {
      zfprintf(mesg, "sd: Temp file (1n): %s\n", tempzip);
      fflush(mesg);
    }
    Trace((stderr, "zip diagnostic: tempzip: %s\n", tempzip));
    if ((y = zfopen(tempzip, FOPW_TMP)) == NULL) {
      ZIPERR(ZE_TEMP, tempzip);
    }
#endif

#if (!defined(VMS) && !defined(CMS_MVS))
    /* Use large buffer to speed up stdio: */
#if (defined(_IOFBF) || !defined(BUFSIZ))
    zipbuf = (char *)malloc(ZBSZ);
#else
    zipbuf = (char *)malloc(BUFSIZ);
#endif
    if (zipbuf == NULL) {
      ZIPERR(ZE_MEM, tempzip);
    }
# ifdef _IOFBF
    setvbuf(y, zipbuf, _IOFBF, ZBSZ);
# else
    setbuf(y, zipbuf);
# endif /* _IOBUF */
#endif /* !VMS  && !CMS_MVS */


    if ((r = readzipfile()) != ZE_OK) {
      ZIPERR(r, zipfile);
    }

    /* Write central directory and end header to temporary zip */
    if (show_what_doing) {
      zfprintf(mesg, "sd: Writing central directory (-FF)\n");
      fflush(mesg);
    }
    diag("writing central directory");
    k = 0;                        /* keep count for end header */
    c = tempzn;                   /* get start of central */
    n = t = 0;
    for (z = zfiles; z != NULL; z = z->nxt)
    {
      if ((r = putcentral(z)) != ZE_OK) {
        ZIPERR(r, tempzip);
      }
      tempzn += 4 + CENHEAD + z->nam + z->cext + z->com;
      n += z->len;
      t += z->siz;
      k++;
    }
    if (zcount == 0)
      zipwarn("zip file empty", "");
    t = tempzn - c;               /* compute length of central */
    diag("writing end of central directory");
    if ((r = putend(k, t, c, zcomlen, zcomment)) != ZE_OK) {
      ZIPERR(r, tempzip);
    }
    if (fclose(y)) {
      ZIPERR(d ? ZE_WRITE : ZE_TEMP, tempzip);
    }
    if (in_file != NULL) {
      fclose(in_file);
      in_file = NULL;
    }

    /* Replace old zip file with new zip file, leaving only the new one */
    if (strcmp(out_path, "-") && !d)
    {
      diag("replacing old zip file with new zip file");
      if ((r = replace(out_path, tempzip)) != ZE_OK)
      {
        zipwarn("new zip file left as: ", tempzip);
        free((zvoid *)tempzip);
        tempzip = NULL;
        ZIPERR(r, "was replacing the original zip file");
      }
      free((zvoid *)tempzip);
    }
    tempzip = NULL;
    if (zip_attributes && strcmp(zipfile, "-")) {
      setfileattr(out_path, zip_attributes);
#ifdef VMS
      /* If the zip file existed previously, restore its record format: */
      if (x != NULL)
        (void)VMSmunch(out_path, RESTORE_RTYPE, NULL);
#endif
    }

    set_filetype(out_path);

    /* finish logfile (it gets closed in freeup() called by finish()) */
    if (logfile) {
        struct tm *now;
        time_t clocktime;

        zfprintf(logfile, "\nTotal %ld entries (", files_total);
        DisplayNumString(logfile, bytes_total);
        zfprintf(logfile, " bytes)");

        /* get current time */
        time(&clocktime);
        now = localtime(&clocktime);
        zfprintf(logfile, "\nDone %s", asctime(now));
        fflush(logfile);
    }

    RETURN(finish(ZE_OK));
  } /* end -FF */

  /* ----------------------------------------------- */



  zfiles = NULL;
  zfilesnext = &zfiles;                        /* first link */
  zcount = 0;

  total_cd_total_entries = 0;                  /* total CD entries for all archives */

#ifdef BACKUP_SUPPORT
  /* If --diff, read any incremental archives.  The entries from these
     archives get added to the z list and so prevents any matching
     entries from the file scan from being added to the new archive. */

  if ((diff_mode == 2 || backup_type == BACKUP_INCR) && apath_list) {
    struct filelist_struct *ap = apath_list;

    for (; ap; ) {
      if (show_what_doing) {
        zfprintf(mesg, "sd: Scanning incremental archive:  %s\n", ap->name);
        fflush(mesg);
      }

      if (backup_type == BACKUP_INCR) {
        zipmessage("Scanning incr archive:  ", ap->name);
      }

      read_inc_file(ap->name);

      ap = ap->next;
    } /* for */
  }
#endif /* BACKUP_SUPPORT */



  /* Read old archive */

  /* Now read the zip file here instead of when doing args above */
  /* Only read the central directory and build zlist */

#ifdef BACKUP_SUPPORT
      if (backup_type == BACKUP_DIFF || backup_type == BACKUP_INCR) {
        zipmessage("Scanning full archive:  ", backup_full_path);
      }
#endif /* BACKUP_SUPPORT */


  if (show_what_doing) {
    zfprintf(mesg, "sd: Reading archive\n");
    fflush(mesg);
  }

  /* read zipfile if exists */
#ifdef BACKUP_SUPPORT
  if (backup_type == BACKUP_FULL) {
    /* skip reading archive, even if exists, as replacing */
  }
  else
#endif
  if ((r = readzipfile()) != ZE_OK) {
    ZIPERR(r, zipfile);
  }


#ifndef UTIL
  if (split_method == -1) {
    split_method = 0;
  } else if (!fix && split_method == 0 && total_disks > 1) {
    /* if input archive is multi-disk and splitting has not been
       enabled or disabled (split_method == -1), then automatically
       set split size to same as first input split */
    zoff_t size = 0;

    in_split_path = get_in_split_path(in_path, 0);

    if (filetime(in_split_path, NULL, &size, NULL) == 0) {
      zipwarn("Could not get info for input split: ", in_split_path);
      return ZE_OPEN;
    }
    split_method = 1;
    split_size = (uzoff_t) size;

    free(in_split_path);
    in_split_path = NULL;
  }

  if (noisy_splits && split_size > 0)
    zipmessage("splitsize = ", zip_fuzofft(split_size, NULL, NULL));
#endif

  /* so disk display starts at 1, will be updated when entries are read */
  current_in_disk = 0;

  /* no input zipfile and showing contents */
  if (!zipfile_exists && show_files &&
      ((kk == 3 && !names_from_file) || action == ARCHIVE)) {
    ZIPERR(ZE_OPEN, zipfile);
  }

  if (zcount == 0 && (action != ADD || d)) {
    zipwarn(zipfile, " not found or empty");
  }

  if (have_out && kk == 3) {
    /* no input paths so assume copy mode and match everything if --out */
    for (z = zfiles; z != NULL; z = z->nxt) {
      z->mark = pcount ? filter(z->zname, filter_match_case) : 1;
    }
  }

  /* Scan for new files */

#ifdef ENABLE_USER_PROGRESS
  u_p_phase = 1;
  u_p_task = "Scanning";
#endif /* def ENABLE_USER_PROGRESS */

  /* Process file arguments from command line added using add_name() */
  if (filelist) {
    int old_no_wild = no_wild;

    if (action == ARCHIVE) {
      /* find in archive */
      if (show_what_doing) {
        zfprintf(mesg, "sd: Scanning archive entries\n");
        fflush(mesg);
      }
      for (; filelist; ) {
        if ((r = proc_archive_name(filelist->name, filter_match_case)) != ZE_OK) {
          if (r == ZE_MISS) {
            char *n = NULL;
#ifdef WIN32
            /* Win9x console always uses OEM character coding, and
               WinNT console is set to OEM charset by default, too */
            if ((n = malloc(strlen(filelist->name) + 1)) == NULL)
              ZIPERR(ZE_MEM, "name not matched error");
            INTERN_TO_OEM(filelist->name, n);
#else
            n = filelist->name;
#endif
            zipwarn("not in archive: ", n);
#ifdef WIN32
            free(n);
#endif
          }
          else {
            ZIPERR(r, filelist->name);
          }
        }
        free(filelist->name);
        filearg = filelist;
        filelist = filelist->next;
        free(filearg);
      }
    } else {
      /* try find matching files on OS first then try find entries in archive */
      if (show_what_doing) {
        zfprintf(mesg, "sd: Scanning files\n");
        fflush(mesg);
      }
      for (; filelist; ) {
        no_wild = filelist->verbatim;
#ifdef ETWODD_SUPPORT
        if (etwodd && strcmp(filelist->name, "-") == 0) {
          ZIPERR(ZE_PARMS, "can't use -et (--etwodd) with input from stdin");
        }
#endif
        if ((r = PROCNAME(filelist->name)) != ZE_OK) {
          if (r == ZE_MISS) {
            if (bad_open_is_error) {
              zipwarn("name not matched: ", filelist->name);
              ZIPERR(ZE_OPEN, filelist->name);
            } else {
              zipwarn("name not matched: ", filelist->name);
            }
          } else {
            ZIPERR(r, filelist->name);
          }
        }
        free(filelist->name);
        filearg = filelist;
        filelist = filelist->next;
        free(filearg);
      }
    }
    no_wild = old_no_wild;
  }

#ifdef IZ_CRYPT_AES_WG
  if (encryption_method >= AES_MIN_ENCRYPTION) {
    /*
    time_t pool_init_start;
    time_t pool_init_time;
    */

/* SMSd. */
    int salt_len;

    if (show_what_doing) {
        zfprintf(mesg,
         "sd: Initializing AES_WG encryption random number pool\n");
        fflush(mesg);
    }

    /*
    pool_init_start = time(NULL);
    */

    /* initialize the random number pool */
    aes_rnp.entropy = entropy_fun;
    prng_init(aes_rnp.entropy, &aes_rnp);
    /* and the salt */

/* SMSd. */
#if 0
    if ((zsalt = malloc(32)) == NULL) {
      ZIPERR(ZE_MEM, "Getting memory for salt");
    }
    prng_rand(zsalt, SALT_LENGTH(1), &aes_rnp);
#endif /* 0 */

/* SMSd. */
    salt_len = SALT_LENGTH( encryption_method- (AES_MIN_ENCRYPTION- 1));
    if ((zsalt = malloc( salt_len)) == NULL) {
      ZIPERR(ZE_MEM, "Getting memory for salt");
    }
#ifdef FAKE_SALT
    memset( zsalt, 1, salt_len);        /* Feel free to discard. */
#else /* def FAKE_SALT */
    prng_rand(zsalt, salt_len, &aes_rnp);
#endif /* def FAKE_SALT [else] */

    /*
    pool_init_time = time(NULL) - pool_init_start;
    */

    if (show_what_doing) {
        zfprintf(mesg, "sd: AES_WG random number pool initialized\n");
        /*
        fprintf(mesg, "sd: AES_WG random number pool initialized in %d s\n",
         pool_init_time);
        */
        fflush(mesg);
    }
  }
#endif

#ifdef IZ_CRYPT_AES_WG_NEW
  if (encryption_method >= AES_MIN_ENCRYPTION) {
    time_t pool_init_start;
    time_t pool_init_time;

    if (show_what_doing) {
        zfprintf(mesg, "sd: Initializing AES_WG\n");
        fflush(mesg);
    }

    pool_init_start = time(NULL);

    /* initialise mode and set key  */
    ccm_init_and_key(key,            /* the key value                */
                     key_size,       /* and its length in bytes      */
                     &aesnew_ctx);   /* the mode context             */

    pool_init_time = time(NULL) - pool_init_start;

    if (show_what_doing) {
        zfprintf(mesg, "sd: AES initialized in %d s\n", pool_init_time);
        fflush(mesg);
    }
  }
#endif

  /* recurse from current directory for -R */
  if (recurse == 2) {
#ifdef AMIGA
    if ((r = PROCNAME("")) != ZE_OK)
#else
    if ((r = PROCNAME(".")) != ZE_OK)
#endif
    {
      if (r == ZE_MISS) {
        if (bad_open_is_error) {
          zipwarn("name not matched: ", "current directory for -R");
          ZIPERR(ZE_OPEN, "-R");
        } else {
          zipwarn("name not matched: ", "current directory for -R");
        }
      } else {
        ZIPERR(r, "-R");
      }
    }
  }


  if (show_what_doing) {
    zfprintf(mesg, "sd: Applying filters\n");
    fflush(mesg);
  }
  /* Clean up selections ("3 <= kk <= 5" now) */
  if (kk != 4 && first_listarg == 0 &&
      (action == UPDATE || action == FRESHEN)) {
    /* if -u or -f with no args, do all, but, when present, apply filters */
    for (z = zfiles; z != NULL; z = z->nxt) {
      z->mark = pcount ? filter(z->zname, filter_match_case) : 1;
#ifdef DOS
      if (z->mark) z->dosflag = 1;      /* force DOS attribs for incl. names */
#endif
    }
  }

  /* Currently we only sort the found list for Unix.  Other ports tend to sort
   * it on their own.
   *
   * Now sort function fqcmpz_icfirst() is used for all ports.  This implies
   * that all ports support strcasecmp() (except Windows, which uses
   * _stricmp()).
   *
   * If your port does not support strcasecmp(), update check_dup_sort() as
   * needed.
   */
#ifdef UNIX
  sort_found_list = 1;
#else
  sort_found_list = 0;
#endif

  if (show_what_doing) {
    if (sort_found_list)
      zfprintf(mesg, "sd: Checking dups, sorting found list\n");
    else
      zfprintf(mesg, "sd: Checking dups\n");
    fflush(mesg);
  }

  /* remove duplicates in found list */
  if ((r = check_dup_sort(sort_found_list)) != ZE_OK) {
    if (r == ZE_PARMS) {
      ZIPERR(r, "cannot repeat names in zip file");
    }
    else {
      ZIPERR(r, "was processing list of files");
    }
  }

  if (zcount)
    free((zvoid *)zsort);


/* 2010-10-01 SMS.
 * Disabled the following stuff, to let the real temporary name code do
 * its job.
 */
#if 0

/*
 * XXX make some kind of mktemppath() function for each OS.
 */

#ifndef VM_CMS
/* For CMS, leave tempath NULL.  A-disk will be used as default. */
  /* If -b not specified, make temporary path the same as the zip file */
# if defined(MSDOS) || defined(__human68k__) || defined(AMIGA)
  if (tempath == NULL && ((p = MBSRCHR(zipfile, '/')) != NULL ||
#  ifdef MSDOS
                          (p = MBSRCHR(zipfile, '\\')) != NULL ||
#  endif /* def MSDOS */
                          (p = MBSRCHR(zipfile, ':')) != NULL))
  {
    if (*p == ':')
      p++;
# else /* defined(MSDOS) || defined(__human68k__) || defined(AMIGA) */
#  ifdef RISCOS
  if (tempath == NULL && (p = MBSRCHR(zipfile, '.')) != NULL)
  {
#  else /* def RISCOS */
#   ifdef QDOS
  if (tempath == NULL && (p = LastDir(zipfile)) != NULL)
  {
#   else /* def QDOS */
  if (tempath == NULL && (p = MBSRCHR(zipfile, '/')) != NULL)
  {
#   endif /* def QDOS [else] */
#  endif /* def RISCOS [else] */
# endif /* defined(MSDOS) || defined(__human68k__) || defined(AMIGA) [else] */
    if ((tempath = (char *)malloc((int)(p - zipfile) + 1)) == NULL) {
      ZIPERR(ZE_MEM, "was processing arguments (7)");
    }
    r = *p;  *p = 0;
    strcpy(tempath, zipfile);
    *p = (char)r;
  }
#endif /* ndef VM_CMS */

#endif /* 0 */


#if (defined(IZ_CHECK_TZ) && defined(USE_EF_UT_TIME))
  if (!zp_tz_is_valid) {
    zipwarn("TZ environment variable not found, cannot use UTC times!!","");
  }
#endif /* IZ_CHECK_TZ && USE_EF_UT_TIME */

  /* For each marked entry, if not deleting, check if it exists, and if
     updating or freshening, compare date with entry in old zip file.
     Unmark if it doesn't exist or is too old, else update marked count. */
  if (show_what_doing) {
    zfprintf(mesg, "sd: Scanning files to update\n");
    fflush(mesg);
  }
#ifdef MACOS
  PrintStatProgress("Getting file information ...");
#endif
  diag("stating marked entries");
  Trace((stderr, "zip diagnostic: zfiles=%u\n", (unsigned)zfiles));
  k = 0;                        /* Initialize marked count */
  scan_started = 0;
  scan_count   = 0;
  all_current  = 1;
  for (z = zfiles; z != NULL; z = z->nxt) {
    Trace((stderr, "zip diagnostic: stat file=%s\n", z->zname));
    /* if already displayed Scanning files in newname() then continue dots */
    if (noisy && scan_last) {
      scan_count++;
      if (scan_count % 100 == 0) {
        time_t current = time(NULL);

        if (current - scan_last > scan_dot_time) {
          if (scan_started == 0) {
            scan_started = 1;
            zfprintf(mesg, " ");
            fflush(mesg);
          }
          scan_last = current;
          zfprintf(mesg, ".");
          fflush(mesg);
        }
      }
    }
#ifdef UNICODE_SUPPORT
    /* If existing entry has a uname, then Unicode is primary. */
    if (z->uname)
      z->utf8_path = 1;
#endif

    z->current = 0;
    if (!(z->mark)) {
      /* if something excluded run through the list to catch deletions */
      all_current = 0;
    }

    if (z->mark) {
#ifdef USE_EF_UT_TIME
      iztimes f_utim, z_utim;
      ulg z_tim;
#endif /* USE_EF_UT_TIME */
      /* Be aware that using zname instead of oname could cause improper
         display of name on some ports using non-ASCII character sets or
         ports that do OEM conversions. */
      Trace((stderr, "zip diagnostics: marked file=%s\n", z->zname));

      csize = z->siz;
      usize = z->len;
      if (action == DELETE) {
        /* only delete files in date range */
#ifdef USE_EF_UT_TIME
        z_tim = (get_ef_ut_ztime(z, &z_utim) & EB_UT_FL_MTIME) ?
                unix2dostime(&z_utim.mtime) : z->tim;
#else /* !USE_EF_UT_TIME */
#       define z_tim  z->tim
#endif /* ?USE_EF_UT_TIME */
        if (z_tim < before || (after && z_tim >= after)) {
          /* include in archive */
          z->mark = 0;
        } else {
          /* delete file */
          files_total++;
          /* ignore len in old archive and update to current size */
          z->len = usize;
          if (csize != (uzoff_t) -1 && csize != (uzoff_t) -2)
            bytes_total += csize;
          k++;
        }
      } else if (action == ARCHIVE) {
        /* only keep files in date range */
#ifdef USE_EF_UT_TIME
        z_tim = (get_ef_ut_ztime(z, &z_utim) & EB_UT_FL_MTIME) ?
                unix2dostime(&z_utim.mtime) : z->tim;
#else /* !USE_EF_UT_TIME */
#       define z_tim  z->tim
#endif /* ?USE_EF_UT_TIME */
        if (z_tim < before || (after && z_tim >= after)) {
          /* exclude from archive */
          z->mark = 0;
        } else {
          /* keep file */
          files_total++;
          /* ignore len in old archive and update to current size */
          z->len = usize;
          if (csize != (uzoff_t) -1 && csize != (uzoff_t) -2)
            bytes_total += csize;
          k++;
        }
      } else {
        int isdirname = 0;

        if (z->iname && (z->iname)[strlen(z->iname) - 1] == '/') {
          isdirname = 1;
        }

# ifdef UNICODE_SUPPORT_WIN32
        if (!no_win32_wide) {
          if (z->namew == NULL) {
            if (z->uname != NULL)
              z->namew = utf8_to_wchar_string(z->uname);
            else
              z->namew = local_to_wchar_string(z->name);
          }
        }
# endif

/* AD: Problem: filetime not available for MVS non-POSIX files */
/* A filetime equivalent should be created for this case. */
#ifdef USE_EF_UT_TIME
# ifdef UNICODE_SUPPORT_WIN32
        if ((!no_win32_wide) && (z->namew != NULL))
          tf = filetimew(z->namew, (ulg *)NULL, (zoff_t *)&usize, &f_utim);
        else
          tf = filetime(z->name, (ulg *)NULL, (zoff_t *)&usize, &f_utim);
# else
        tf = filetime(z->name, (ulg *)NULL, (zoff_t *)&usize, &f_utim);
# endif
#else /* !USE_EF_UT_TIME */
# ifdef UNICODE_SUPPORT_WIN32
        if ((!no_win32_wide) && (z->namew != NULL))
          tf = filetimew(z->namew, (ulg *)NULL, (zoff_t *)&usize, NULL);
        else
          tf = filetime(z->name, (ulg *)NULL, (zoff_t *)&usize, NULL);
# else
        tf = filetime(z->name, (ulg *)NULL, (zoff_t *)&usize, NULL);
# endif
#endif /* ?USE_EF_UT_TIME */
        if (tf == 0)
          /* entry that is not on OS */
          all_current = 0;
        if (tf == 0 ||
            tf < before || (after && tf >= after) ||
            ((action == UPDATE || action == FRESHEN) &&
#ifdef USE_EF_UT_TIME
             ((get_ef_ut_ztime(z, &z_utim) & EB_UT_FL_MTIME) ?
              f_utim.mtime <= ROUNDED_TIME(z_utim.mtime) : tf <= z->tim)
#else /* !USE_EF_UT_TIME */
             tf <= z->tim
#endif /* ?USE_EF_UT_TIME */
           ))
        {
          z->mark = comadd ? 2 : 0;
          z->trash = tf && tf >= before &&
                     (after ==0 || tf < after);   /* delete if -um or -fm */
          if (verbose)
            zfprintf(mesg, "zip diagnostic: %s %s\n", z->oname,
                   z->trash ? "up to date" : "missing or early");
          if (logfile)
            zfprintf(logfile, "zip diagnostic: %s %s\n", z->oname,
                   z->trash ? "up to date" : "missing or early");
        }
        else if (diff_mode && tf == z->tim &&
                 ((isdirname && (zoff_t)usize == -1) || (usize == z->len))) {
          /* if in diff mode only include if file time or size changed */
          /* usize is -1 for directories */
          z->mark = 0;
        }
        else {
          /* usize is -1 for directories and -2 for devices */
          if (tf == z->tim &&
              ((z->len == 0 && (zoff_t)usize == -1)
               || usize == z->len)) {
            /* FileSync uses the current flag */
            /* Consider an entry current if file time is the same
               and entry size is 0 and a directory on the OS
               or the entry size matches the OS size */
            z->current = 1;
          } else {
            all_current = 0;
          }
          files_total++;
          if (usize != (uzoff_t) -1 && usize != (uzoff_t) -2)
            /* ignore len in old archive and update to current size */
            z->len = usize;
          else
            z->len = 0;
          if (usize != (uzoff_t) -1 && usize != (uzoff_t) -2)
            bytes_total += usize;
          k++;
        }
      }
    }
  }

  /* Remove entries from found list that do not exist or are too old */
  if (show_what_doing) {
    zfprintf(mesg, "sd: fcount = %u\n", (unsigned)fcount);
    fflush(mesg);
  }

  diag("stating new entries");
  scan_count = 0;
  scan_started = 0;
  Trace((stderr, "zip diagnostic: fcount=%u\n", (unsigned)fcount));
  for (f = found; f != NULL;) {
    Trace((stderr, "zip diagnostic: new file=%s\n", f->oname));

    if (noisy) {
      /* if updating archive and update was quick, scanning for new files
         can still take a long time */
      if (!zip_to_stdout && scan_last == 0 && scan_count % 100 == 0) {
        time_t current = time(NULL);

        if (current - scan_start > scan_delay) {
          zfprintf(mesg, "Scanning files ");
          fflush(mesg);
          mesg_line_started = 1;
          scan_last = current;
        }
      }
      /* if already displayed Scanning files in newname() or above then continue dots */
      if (scan_last) {
        scan_count++;
        if (scan_count % 100 == 0) {
          time_t current = time(NULL);

          if (current - scan_last > scan_dot_time) {
            if (scan_started == 0) {
              scan_started = 1;
              zfprintf(mesg, " ");
              fflush(mesg);
            }
            scan_last = current;
            zfprintf(mesg, ".");
            fflush(mesg);
          }
        }
      }
    }
    tf = 0;
#ifdef UNICODE_SUPPORT
    f->utf8_path = 0;
    /* if Unix port and locale UTF-8, assume paths UTF-8 */
    if (using_utf8) {
      f->utf8_path = 1;
    }
#endif
    if ((action != DELETE) && (action != FRESHEN)
#if defined( UNIX) && defined( __APPLE__)
     && (f->flags == 0)
#endif /* defined( UNIX) && defined( __APPLE__) */
     ) {
#ifdef UNICODE_SUPPORT_WIN32
      if ((!no_win32_wide) && (f->namew != NULL)) {
        tf = filetimew(f->namew, (ulg *)NULL, (zoff_t *)&usize, NULL);
        /* if have namew, assume got it from wide file scan */
        f->utf8_path = 1;
      }
      else
        tf = filetime(f->name, (ulg *)NULL, (zoff_t *)&usize, NULL);
#else
      tf = filetime(f->name, (ulg *)NULL, (zoff_t *)&usize, NULL);
#endif
    }

    if (action == DELETE || action == FRESHEN ||
        ((tf == 0)
#if defined( UNIX) && defined( __APPLE__)
        /* Don't bother an AppleDouble file. */
        && (f->flags == 0)
#endif /* defined( UNIX) && defined( __APPLE__) */
        ) ||
        tf < before || (after && tf >= after) ||
        (namecmp(f->zname, zipfile) == 0 && !zip_to_stdout)
       ) {
      Trace((stderr, "zip diagnostic: ignore file\n"));
      f = fexpel(f);
    }
    else {
      /* ??? */
      files_total++;
      f->usize = 0;
      if (usize != (uzoff_t) -1 && usize != (uzoff_t) -2) {
        bytes_total += usize;
        f->usize = usize;
      }
      f = f->nxt;
    }
  }
  if (mesg_line_started) {
    zfprintf(mesg, "\n");
    mesg_line_started = 0;
  }
#ifdef MACOS
  PrintStatProgress("done");
#endif

  if (show_files) {
    uzoff_t count = 0;
    uzoff_t bytes = 0;

    if (noisy) {
      fflush(mesg);
    }

    if (noisy && (show_files == 1 || show_files == 3 || show_files == 5)) {
      /* sf, su, sU */
      if (mesg_line_started) {
        zfprintf(mesg, "\n");
        mesg_line_started = 0;
      }
      if (kk == 3  && !names_from_file) {
        /* -sf alone */
        zfprintf(mesg, "Archive contains:\n");
        strcpy(action_string, "Archive contains");
      } else if (action == DELETE) {
        zfprintf(mesg, "Would Delete:\n");
        strcpy(action_string, "Would Delete:");
      } else if (action == FRESHEN) {
        zfprintf(mesg, "Would Freshen:\n");
        strcpy(action_string, "Would Freshen");
      } else if (action == ARCHIVE) {
        zfprintf(mesg, "Would Copy:\n");
        strcpy(action_string, "Would Copy");
      } else {
        zfprintf(mesg, "Would Add/Update:\n");
        strcpy(action_string, "Would Add/Update");
      }
      fflush(mesg);
    }

    if (logfile) {
      if (logfile_line_started) {
        zfprintf(logfile, "\n");
        logfile_line_started = 0;
      }
      if (kk == 3 && !names_from_file)
        /* -sf alone */
        zfprintf(logfile, "Archive contains:\n");
      else if (action == DELETE)
        zfprintf(logfile, "Would Delete:\n");
      else if (action == FRESHEN)
        zfprintf(logfile, "Would Freshen:\n");
      else if (action == ARCHIVE)
        zfprintf(logfile, "Would Copy:\n");
      else
        zfprintf(logfile, "Would Add/Update:\n");
      fflush(logfile);
    }

    for (z = zfiles; z != NULL; z = z->nxt) {
      if (z->mark || kk == 3) {
        int mesg_displayed = 0;
        int log_displayed = 0;

        count++;
        if ((zoff_t)z->len > 0)
          bytes += z->len;
#ifdef WINDLL
        if (lpZipUserFunctions->service != NULL)
        {
          char us[100];
          char cs[100];
          long perc = 0;
          char *oname;
          char *uname;

          oname = z->oname;
# ifdef UNICODE_SUPPORT
          uname = z->uname;
# else
          uname = NULL;
# endif

          WriteNumString(z->len, us);
          WriteNumString(z->siz, cs);
          perc = percent(z->len, z->siz);

          if ((*lpZipUserFunctions->service)(oname,
                                             uname,
                                             us,
                                             cs,
                                             z->len,
                                             z->siz,
                                             action_string,
                                             perc))
            ZIPERR(ZE_ABORT, "User terminated operation");
        }
#endif
        if (noisy && (show_files == 1 || show_files == 3)) {
          /* sf, su */
#ifdef UNICODE_SUPPORT
          if (unicode_show && z->uname) {
# if defined(WIN32) && !defined(ZIP_DLL_LIB)
            printf(errbuf, "  %s", z->uname);
            write_console(mesg, errbuf);
# else
            zfprintf(mesg, "  %s", z->uname);
# endif
          } else
#endif
          {
            zfprintf(mesg, "  %s", z->oname);
          }
          mesg_displayed = 1;
        }
        if (logfile && !(show_files == 5 || show_files == 6)) {
          /* not sU or sU- show normal name in log */
#ifdef UNICODE_SUPPORT
          if (log_utf8 && z->uname) {
            zfprintf(logfile, "  %s", z->uname);
          } else {
            zfprintf(logfile, "  %s", z->oname);
          }
          log_displayed = 1;
#else /* def UNICODE_SUPPORT */
          zfprintf(logfile, "  %s\n", z->oname);
#endif /* def UNICODE_SUPPORT [else] */
        }

#ifdef UNICODE_TEST
        /* UNICODE_TEST adds code that extracts a directory tree
           from the archive, handling Unicode names.  The files
           are empty (no processing of contents is done).  This
           is used to verify Unicode processing.  Now that UnZip
           has Unicode support, this code should not be needed
           except when implementing and testing new features.  */
        if (create_files) {
          int r;
          int dir = 0;
          FILE *f;

# ifdef UNICODE_SUPPORT_WIN32
          char *fn = NULL;
          wchar_t *fnw = NULL;

          if (!no_win32_wide) {
            if ((fnw = malloc((wcslen(z->znamew) + 120) * sizeof(wchar_t))) == NULL)
              ZIPERR(ZE_MEM, "sC (1)");
            wcscpy(fnw, L"testdir/");
            wcscat(fnw, z->znamew);
            if (fnw[wcslen(fnw) - 1] == '/')
              dir = 1;
            if (dir)
              r = _wmkdir(fnw);
            else
              f = _wfopen(fnw, L"w");
          } else {
            if ((fn = malloc(strlen(z->zname) + 120)) == NULL)
              ZIPERR(ZE_MEM, "sC (2)");
            strcpy(fn, "testdir/");
            strcat(fn, z->zname);
            if (fn[strlen(fn) - 1] == '/')
              dir = 1;
            if (dir)
              r = mkdir(fn);
            else
              f = fopen(fn, "w");
          }
# else /* def UNICODE_SUPPORT_WIN32 */
          char *fn = NULL;
          if ((fn = malloc(strlen(z->zname) + 120)) == NULL)
            ZIPERR(ZE_MEM, "sC (3)");
          strcpy(fn, "testdir/");
          if (z->uname)
            strcat(fn, z->uname);
          else
            strcat(fn, z->zname);

          if (fn[strlen(fn) - 1] == '/')
            dir = 1;
          if (dir)
            r = mkdir(fn, 0777);
          else
            f = fopen(fn, "w");
# endif /* def UNICODE_SUPPORT_WIN32 */
          if (dir) {
            if (r) {
              if (errno != 17) {
                zprintf(" - could not create directory testdir/%s\n", z->oname);
                zperror("    dir");
              }
            } else {
              zprintf(" - created directory testdir/%s\n", z->oname);
            }
          } else {
            if (f == NULL) {
              zprintf(" - could not open testdir/%s\n", z->oname);
              zperror("    file");
            } else {
              fclose(f);
              zprintf(" - created testdir/%s\n", z->oname);
              if (z->uname)
                zprintf("   u - created testdir/%s\n", z->uname);
            }
          }
        }
#endif /* def UNICODE_TEST */
#ifdef UNICODE_SUPPORT
        if (show_files == 3 || show_files == 4) {
          /* su, su- */
          /* Include escaped Unicode name (if exists) under standard name */
          if (z->ouname) {
            if (noisy && show_files == 3) {
              zfprintf(mesg, "\n     Escaped Unicode:  %s", z->ouname);
              mesg_displayed = 1;
            }
            if (logfile) {
              if (log_utf8) {
                zfprintf(logfile, "     Unicode:  %s", z->uname);
              } else {
                zfprintf(logfile, "\n     Escaped Unicode:  %s", z->ouname);
              }
              log_displayed = 1;
            }

          }
        }
        if (show_files == 5 || show_files == 6) {
          /* sU, sU- */
          /* Display only escaped Unicode name if exists or standard name */
          if (z->ouname) {
            /* Unicode name */
            if (noisy && show_files == 5) {
              zfprintf(mesg, "  %s", z->ouname);
              mesg_displayed = 1;
            }
            if (logfile) {
              if (log_utf8)
                zfprintf(logfile, "  %s", z->uname);
              else
                zfprintf(logfile, "  %s", z->ouname);
              log_displayed = 1;
            }
          } else {
            /* No Unicode name so use standard name */
            if (noisy && show_files == 5) {
              zfprintf(mesg, "  %s", z->oname);
              mesg_displayed = 1;
            }
            if (logfile) {
              if (log_utf8 && z->uname)
                zfprintf(logfile, "  %s", z->uname);
              else
                zfprintf(logfile, "  %s", z->oname);
              log_displayed = 1;
            }
          }
        }
#endif /* def UNICODE_SUPPORT */
        if (sf_usize) {
          WriteNumString(z->len, errbuf);

          if (noisy && mesg_displayed)
            zfprintf(mesg, "  (%s)", errbuf);
          if (logfile && log_displayed)
            zfprintf(logfile, "  (%s)", errbuf);
        }
        if (noisy && mesg_displayed)
          zfprintf(mesg, "\n");
        if (logfile && log_displayed)
          zfprintf(logfile, "\n");
      }
    }
    for (f = found; f != NULL; f = f->nxt) {
      count++;
      if ((zoff_t)f->usize > 0)
        bytes += f->usize;
#ifdef UNICODE_SUPPORT
      if (unicode_escape_all) {
        char *escaped_unicode;
        escaped_unicode = local_to_escape_string(f->zname);
        if (noisy && (show_files == 1 || show_files == 3 || show_files == 5)) {
          /* sf, su, sU */
          zfprintf(mesg, "  %s", escaped_unicode);
        }
        if (logfile) {
          if (log_utf8 && f->uname) {
            zfprintf(logfile, "  %s", f->uname);
          } else {
            zfprintf(logfile, "  %s", escaped_unicode);
          }
        }
        free(escaped_unicode);
      } else {
#endif /* def UNICODE_SUPPORT */
        if (noisy && (show_files == 1 || show_files == 3 || show_files == 5)) {
          /* sf, su, sU */
#ifdef UNICODE_SUPPORT
          if (unicode_show && f->uname) {
# if defined(WIN32) && !defined(ZIP_DLL_LIB)
            sprintf(errbuf, "  %s", f->uname);
            write_console(mesg, errbuf);
# else
            zfprintf(mesg, "  %s", f->uname);
# endif
          }
          else
#endif /* def UNICODE_SUPPORT */
            zfprintf(mesg, "  %s", f->oname);
        }
        if (logfile) {
#ifdef UNICODE_SUPPORT
          if (log_utf8 && f->uname) {
            zfprintf(logfile, "  %s", f->uname);
          } else {
            zfprintf(logfile, "  %s", f->oname);
          }
#else /* def UNICODE_SUPPORT */
          zfprintf(logfile, "  %s", f->oname);
#endif /* def UNICODE_SUPPORT [else] */
        }
#ifdef UNICODE_SUPPORT
      }
#endif /* def UNICODE_SUPPORT */

      if (sf_usize) {
        WriteNumString(f->usize, errbuf);

        if (noisy)
          zfprintf(mesg, "  (%s)", errbuf);
        if (logfile)
          zfprintf(logfile, "  (%s)", errbuf);
      }

#ifdef WINDLL
      if (lpZipUserFunctions->service != NULL)
      {
        char us[100];
        char cs[100];
        long perc = 0;
        char *oname;
        char *uname;

        oname = f->oname;
# ifdef UNICODE_SUPPORT
        uname = f->uname;
# else
        uname = NULL;
# endif
        WriteNumString(f->usize, us);
        strcpy(cs, "");
        perc = 0;

        if ((*lpZipUserFunctions->service)(oname,
                                           uname,
                                           us,
                                           cs,
                                           f->usize,
                                           0,
                                           action_string,
                                           perc))
          ZIPERR(ZE_ABORT, "User terminated operation");
      }
#endif /* def WINDLL */

      if (noisy)
        zfprintf(mesg, "\n");
      if (logfile)
        zfprintf(logfile, "\n");

    }

    WriteNumString(bytes, errbuf);
    if (noisy || logfile == NULL) {
      if (bytes < 1024) {
        zfprintf(mesg, "Total %s entries (%s bytes)\n",
                                            zip_fuzofft(count, NULL, NULL),
                                            zip_fuzofft(bytes, NULL, NULL));
      } else {
        zfprintf(mesg, "Total %s entries (%s bytes (%s))\n",
                                            zip_fuzofft(count, NULL, NULL),
                                            zip_fuzofft(bytes, NULL, NULL),
                                            errbuf);
      }
    }
    if (logfile) {
      if (bytes < 1024) {
        zfprintf(logfile, "Total %s entries (%s bytes)\n",
                                            zip_fuzofft(count, NULL, NULL),
                                            zip_fuzofft(bytes, NULL, NULL));
      } else {
        zfprintf(logfile, "Total %s entries (%s bytes (%s))\n",
                                            zip_fuzofft(count, NULL, NULL),
                                            zip_fuzofft(bytes, NULL, NULL),
                                            errbuf);
      }
    }
#ifdef WINDLL
    if (*lpZipUserFunctions->finish != NULL) {
      char susize[100];
      char scsize[100];
      long p;

      strcpy(susize, zip_fzofft(bytes, NULL, "u"));
      strcpy(scsize, "");
      p = 0;
      (*lpZipUserFunctions->finish)(susize, scsize, bytes, 0, p);
    }
#endif /* def WINDLL */
    RETURN(finish(ZE_OK));
  }

  /* Make sure there's something left to do */
  if (k == 0 && found == NULL && !diff_mode &&
      !(zfiles == NULL && allow_empty_archive) &&
      !(zfiles != NULL &&
        (latest || fix || adjust || junk_sfx || comadd || zipedit))) {
    if (test && (zfiles != NULL || zipbeg != 0)) {
#ifndef ZIP_DLL_LIB
      check_zipfile(zipfile, argv[0]);
#endif
      RETURN(finish(ZE_OK));
    }
    if (action == UPDATE || action == FRESHEN) {
      diag("Nothing left - update/freshen");
      RETURN(finish(zcount ? ZE_NONE : ZE_NAME));
    }
    /* If trying to delete from empty archive, error is empty archive
       rather than nothing to do. */
    else if (zfiles == NULL
             && (latest || fix || adjust || junk_sfx || action == DELETE)) {
      ZIPERR(ZE_NAME, zipfile);
    }
#ifndef ZIP_DLL_LIB
    else if (recurse && (pcount == 0) && (first_listarg > 0)) {
# ifdef VMS
      strcpy(errbuf, "try: zip \"");
      for (i = 1; i < (first_listarg - 1); i++)
        strcat(strcat(errbuf, args[i]), "\" ");
      strcat(strcat(errbuf, args[i]), " *.* -i");
# else /* !VMS */
      strcpy(errbuf, "try: zip");
      for (i = 1; i < first_listarg; i++)
        strcat(strcat(errbuf, " "), args[i]);
#  ifdef AMIGA
      strcat(errbuf, " \"\" -i");
#  else
      strcat(errbuf, " . -i");
#  endif
# endif /* def VMS */
      for (i = first_listarg; i < argc; i++)
        strcat(strcat(errbuf, " "), args[i]);
      diag("Nothing left - error");
      ZIPERR(ZE_NONE, errbuf);
    }
    else {
      diag("Nothing left - zipfile");
      ZIPERR(ZE_NONE, zipfile);
    }
#endif /* !ZIP_DLL_LIB */
  }

  if (filesync && all_current && fcount == 0) {
    zipmessage("Archive is current", "");
    RETURN(finish(ZE_OK));
  }

  /* d true if appending */
  d = (d && k == 0 && (zipbeg || zfiles != NULL));

#ifdef IZ_CRYPT_ANY
  /* Initialize the crc_32_tab pointer, when encryption was requested. */
  if (key != NULL) {
    crc_32_tab = get_crc_table();
#ifdef EBCDIC
    /* convert encryption key to ASCII (ISO variant for 8-bit ASCII chars) */
    strtoasc(key, key);
#endif /* EBCDIC */
  }
#endif /* def IZ_CRYPT_ANY */

  /* Just ignore the spanning signature if a multi-disk archive */
  if (zfiles && total_disks != 1 && zipbeg == 4) {
    zipbeg = 0;
  }

  /* Not sure yet if this is the best place to free args, but seems no need for
     the args array after this.  Suggested by Polo from forum. */
  free_args(args);
  args = NULL;

  /* Before we get carried away, make sure zip file is writeable. This
   * has the undesired side effect of leaving one empty junk file on a WORM,
   * so when the zipfile does not exist already and when -b is specified,
   * the writability check is made in replace().
   */
  if (strcmp(out_path, "-"))
  {
    if (tempdir && zfiles == NULL && zipbeg == 0) {
      zip_attributes = 0;
    } else {
#ifdef BACKUP_SUPPORT
      if (backup_type) have_out = 1;
#endif
      x = (have_out || (zfiles == NULL && zipbeg == 0)) ? zfopen(out_path, FOPW) :
                                                          zfopen(out_path, FOPM);
      /* Note: FOPW and FOPM expand to several parameters for VMS */
      if (x == NULL) {
        ZIPERR(ZE_CREAT, out_path);
      }
      fclose(x);
      zip_attributes = getfileattr(out_path);
      if (zfiles == NULL && zipbeg == 0)
        destroy(out_path);
    }
  }
  else
    zip_attributes = 0;

  /* Throw away the garbage in front of the zip file for -J */
  if (junk_sfx) zipbeg = 0;

  /* Open zip file and temporary output file */
  if (show_what_doing) {
    zfprintf(mesg, "sd: Open zip file and create temp file\n");
    fflush(mesg);
  }
  diag("opening zip file and creating temporary zip file");
  x = NULL;
  tempzn = 0;
  if (strcmp(out_path, "-") == 0)
  {
    zoff_t pos;

#ifdef MSDOS
    /* It is nonsense to emit the binary data stream of a zipfile to
     * the (text mode) console.  This case should already have been caught
     * in a call to zipstdout() far above.  Therefore, if the following
     * failsafe check detects a console attached to stdout, zip is stopped
     * with an "internal logic error"!  */
    if (isatty(fileno(stdout)))
      ZIPERR(ZE_LOGIC, "tried to write binary zipfile data to console!");
    /* Set stdout mode to binary for MSDOS systems */
# ifdef __HIGHC__
    setmode(stdout, _BINARY);
# else
    setmode(fileno(stdout), O_BINARY);
# endif
    y = zfdopen(fileno(stdout), FOPW_STDOUT);  /* FOPW */
#else
    y = stdout;
#endif

    /* See if output file is empty.  On Windows, if using >> we need to
       account for existing data. */
    pos = zftello(y);

    if (pos != 0) {
      /* Have data at beginning.  As we don't intentionally open in append
         mode, this usually only happens when something like
             zip - test.txt >> out.zip
         is used on Windows.  Account for existing data by skipping over it. */
      bytes_this_split = pos;
      current_local_offset = pos;
      tempzn = pos;
    }

    /* tempzip must be malloced so a later free won't barf */
    tempzip = malloc(4);
    if (tempzip == NULL) {
      ZIPERR(ZE_MEM, "allocating temp filename (4)");
    }
    strcpy(tempzip, "-");
  }
  else if (d) /* d true if just appending (-g) */
  {
    Trace((stderr, "zip diagnostic: grow zipfile: %s\n", zipfile));
    if (total_disks > 1) {
      ZIPERR(ZE_PARMS, "cannot grow split archive");
    }

    if ((y = zfopen(zipfile, FOPM)) == NULL) {
      ZIPERR(ZE_NAME, zipfile);
    }
    tempzip = zipfile;
    /*
    tempzf = y;
    */

    if (zfseeko(y, cenbeg, SEEK_SET)) {
      ZIPERR(ferror(y) ? ZE_READ : ZE_EOF, zipfile);
    }
    bytes_this_split = cenbeg;
    tempzn = cenbeg;
  }
  else
  {
    if (show_what_doing) {
      zfprintf(mesg, "sd: Creating new zip file\n");
      fflush(mesg);
    }
    /* See if there is something at beginning of disk 1 to copy.
       If not, do nothing as zipcopy() will open files to read
       as needed. */
    if (zipbeg) {
      in_split_path = get_in_split_path(in_path, 0);

      while ((in_file = zfopen(in_split_path, FOPR_EX)) == NULL) {
        /* could not open split */

        /* Ask for directory with split.  Updates in_path */
        if (ask_for_split_read_path(0) != ZE_OK) {
          ZIPERR(ZE_ABORT, "could not open archive to read");
        }
        free(in_split_path);
        in_split_path = get_in_split_path(in_path, 1);
      }
    }
#if defined(UNIX) && !defined(NO_MKSTEMP)
    {
      int yd;
      int i;

      /* use mkstemp to avoid race condition and compiler warning */

      if (tempath != NULL)
      {
        /* if -b used to set temp file dir use that for split temp */
        if ((tempzip = malloc(strlen(tempath) + 12)) == NULL) {
          ZIPERR(ZE_MEM, "allocating temp filename (5)");
        }
        strcpy(tempzip, tempath);
        if (lastchar(tempzip) != '/')
          strcat(tempzip, "/");
      }
      else
      {
        /* create path by stripping name and appending template */
        if ((tempzip = malloc(strlen(out_path) + 12)) == NULL) {
        ZIPERR(ZE_MEM, "allocating temp filename (6)");
        }
        strcpy(tempzip, out_path);
        for (i = strlen(tempzip); i > 0; i--) {
          if (tempzip[i - 1] == '/')
            break;
        }
        tempzip[i] = '\0';
      }
      strcat(tempzip, "ziXXXXXX");

      if ((yd = mkstemp(tempzip)) == EOF) {
        ZIPERR(ZE_TEMP, tempzip);
      }
      if (show_what_doing) {
        zfprintf(mesg, "sd: Temp file (2u): %s\n", tempzip);
        fflush(mesg);
      }
      if ((y = fdopen(yd, FOPW_TMP)) == NULL) {
        ZIPERR(ZE_TEMP, tempzip);
      }
    }
#else /* defined(UNIX) && !defined(NO_MKSTEMP) */
    if ((tempzip = tempname(out_path)) == NULL) {
      ZIPERR(ZE_TEMP, out_path);
    }
    if (show_what_doing) {
      zfprintf(mesg, "sd: Temp file (2n): %s\n", tempzip);
      fflush(mesg);
    }
    if ((y = zfopen(tempzip, FOPW_TMP)) == NULL) {
      ZIPERR(ZE_TEMP, tempzip);
    }
#endif /* defined(UNIX) && !defined(NO_MKSTEMP) */
  }

#if (!defined(VMS) && !defined(CMS_MVS))
  /* Use large buffer to speed up stdio: */
# if (defined(_IOFBF) || !defined(BUFSIZ))
  zipbuf = (char *)malloc(ZBSZ);
# else
  zipbuf = (char *)malloc(BUFSIZ);
# endif
  if (zipbuf == NULL) {
    ZIPERR(ZE_MEM, tempzip);
  }
# ifdef _IOFBF
  setvbuf(y, zipbuf, _IOFBF, ZBSZ);
# else
  setbuf(y, zipbuf);
# endif /* _IOBUF */
#endif /* !VMS  && !CMS_MVS */

  /* If not seekable set some flags 3/14/05 EG */
  output_seekable = 1;
  if (!is_seekable(y)) {
    output_seekable = 0;
    use_descriptors = 1;
  }

  /* Not needed.  Only need Zip64 when input file is larger than 2 GiB or reading
     stdin and writing stdout.  This is set in putlocal() for each file. */
#if 0
  /* If using descriptors and Zip64 enabled force Zip64 3/13/05 EG */
# ifdef ZIP64_SUPPORT
  if (use_descriptors && force_zip64 != 0) {
    force_zip64 = 1;
  }
# endif
#endif

  /* if archive exists, not streaming and not deleting or growing, copy
     any bytes at beginning */
  if (strcmp(zipfile, "-") != 0 && !d)  /* this must go *after* set[v]buf */
  {
    /* copy anything before archive */
    if (in_file && zipbeg && (r = bfcopy(zipbeg)) != ZE_OK) {
      ZIPERR(r, r == ZE_TEMP ? tempzip : zipfile);
    }
    if (in_file) {
      fclose(in_file);
      in_file = NULL;
      free(in_split_path);
    }
    tempzn = zipbeg;
    if (split_method) {
      /* add spanning signature */
      if (show_what_doing) {
        zfprintf(mesg, "sd: Adding spanning/splitting signature at top of archive\n");
        fflush(mesg);
      }
      /* write the spanning signature at the top of the archive */
      errbuf[0] = 0x50 /*'P' except for EBCDIC*/;
      errbuf[1] = 0x4b /*'K' except for EBCDIC*/;
      errbuf[2] = 7;
      errbuf[3] = 8;
      bfwrite(errbuf, 1, 4, BFWRITE_DATA);
      /* tempzn updated below */
      tempzn += 4;
    }
  }

  o = 0;                                /* no ZE_OPEN errors yet */


  /* Process zip file, updating marked files */
#ifdef DEBUG
  if (zfiles != NULL)
    diag("going through old zip file");
#endif
  if (zfiles != NULL && show_what_doing) {
    zfprintf(mesg, "sd: Going through old zip file\n");
    fflush(mesg);
  }

#ifdef ENABLE_USER_PROGRESS
  u_p_phase = 2;
  u_p_task = "Freshening";
#endif /* def ENABLE_USER_PROGRESS */

  w = &zfiles;
  while ((z = *w) != NULL) {
    if (z->mark == 1)
    {
      uzoff_t len;
      if ((zoff_t)z->len == -1)
        /* device */
        len = 0;
      else
        len = z->len;

      /* if not deleting, zip it up */
      if (action != ARCHIVE && action != DELETE)
      {
        struct zlist far *localz; /* local header */

#ifdef ENABLE_USER_PROGRESS
        u_p_name = z->oname;
#endif /* def ENABLE_USER_PROGRESS */

        if (action == FRESHEN) {
          strcpy(action_string, "Freshen");
        } else if (filesync && z->current) {
          strcpy(action_string, "Current");
        } else if (!(filesync && z->current)) {
          strcpy(action_string, "Update");
        }

        if (verbose || !(filesync && z->current))
          DisplayRunningStats();
        if (noisy)
        {
          if (action == FRESHEN) {
            zfprintf(mesg, "freshening: %s", z->oname);
            mesg_line_started = 1;
            fflush(mesg);
          } else if (filesync && z->current) {
            if (verbose) {
              zfprintf(mesg, "      ok: %s", z->oname);
              mesg_line_started = 1;
              fflush(mesg);
            }
          } else if (!(filesync && z->current)) {
#ifdef UNICODE_SUPPORT
            if (unicode_show && z->uname)
              zfprintf(mesg, "updating: %s", z->uname);
            else
              zfprintf(mesg, "updating: %s", z->oname);
#else
            zfprintf(mesg, "updating: %s", z->oname);
#endif
            mesg_line_started = 1;
            fflush(mesg);
          }
        }
        if (logall)
        {
          if (action == FRESHEN) {
#ifdef UNICODE_SUPPORT
            if (log_utf8 && z->uname)
              zfprintf(logfile, "freshening: %s", z->uname);
            else
#endif
              zfprintf(logfile, "freshening: %s", z->oname);
            logfile_line_started = 1;
            fflush(logfile);
          } else if (filesync && z->current) {
            if (verbose) {
#ifdef UNICODE_SUPPORT
              if (log_utf8 && z->uname)
                zfprintf(logfile, " current: %s", z->uname);
              else
#endif
                zfprintf(logfile, " current: %s", z->oname);
              logfile_line_started = 1;
              fflush(logfile);
            }
          } else {
#ifdef UNICODE_SUPPORT
            if (log_utf8 && z->uname)
              zfprintf(logfile, "updating: %s", z->uname);
            else
#endif
              zfprintf(logfile, "updating: %s", z->oname);
            logfile_line_started = 1;
            fflush(logfile);
          }
        }

        /* Get local header flags and extra fields */
        if (readlocal(&localz, z) != ZE_OK) {
          zipwarn("could not read local entry information: ", z->oname);
          z->lflg = z->flg;
          z->ext = 0;
        } else {
          z->lflg = localz->lflg;
          z->ext = localz->ext;
          z->extra = localz->extra;
          if (localz->nam) free(localz->iname);
          if (localz->nam) free(localz->name);
#ifdef UNICODE_SUPPORT
          if (localz->uname) free(localz->uname);
#endif
          free(localz);
        }

        /* zip up existing entries */

        if (!(filesync && z->current) &&
             (r = zipup(z)) != ZE_OK && r != ZE_OPEN && r != ZE_MISS)
        {
          zipmessage_nl("", 1);
          /*
          if (noisy)
          {
            if (mesg_line_started) {
#if (!defined(MACOS) && !defined(ZIP_DLL_LIB))
              putc('\n', mesg);
              fflush(mesg);
#else
              fprintf(stdout, "\n");
              fflush(stdout);
#endif
              mesg_line_started = 0;
            }
          }
          if (logall) {
            if (logfile_line_started) {
              fprintf(logfile, "\n");
              logfile_line_started = 0;
              fflush(logfile);
            }
          }
          */
          sprintf(errbuf, "was zipping %s", z->name);
          ZIPERR(r, errbuf);
        }
        if (filesync && z->current)
        {
          /* if filesync if entry matches OS just copy */
          if ((r = zipcopy(z)) != ZE_OK)
          {
            sprintf(errbuf, "was copying %s", z->oname);
            ZIPERR(r, errbuf);
          }
          zipmessage_nl("", 1);
          /*
          if (noisy)
          {
            if (mesg_line_started) {
#if (!defined(MACOS) && !defined(ZIP_DLL_LIB))
              putc('\n', mesg);
              fflush(mesg);
#else
              fprintf(stdout, "\n");
              fflush(stdout);
#endif
              mesg_line_started = 0;
            }
          }
          if (logall) {
            if (logfile_line_started) {
              fprintf(logfile, "\n");
              logfile_line_started = 0;
              fflush(logfile);
            }
          }
          */
        }
        if (r == ZE_OPEN || r == ZE_MISS)
        {
          o = 1;
          zipmessage_nl("", 1);

          /*
          if (noisy)
          {
#if (!defined(MACOS) && !defined(ZIP_DLL_LIB))
            putc('\n', mesg);
            fflush(mesg);
#else
            fprintf(stdout, "\n");
#endif
            mesg_line_started = 0;
          }
          if (logall) {
            fprintf(logfile, "\n");
            logfile_line_started = 0;
            fflush(logfile);
          }
          */

          if (r == ZE_OPEN) {
            zipwarn_indent("could not open for reading: ", z->oname);
            zipwarn_indent( NULL, strerror( errno));
            if (bad_open_is_error) {
              sprintf(errbuf, "was zipping %s", z->name);
              ZIPERR(r, errbuf);
            }
          } else {
            zipwarn_indent("file and directory with the same name (1): ",
             z->oname);
          }
          zipwarn_indent("will just copy entry over: ", z->oname);
          if ((r = zipcopy(z)) != ZE_OK)
          {
            sprintf(errbuf, "was copying %s", z->oname);
            ZIPERR(r, errbuf);
          }
          z->mark = 0;
        }
        files_so_far++;
        good_bytes_so_far += z->len;
        bytes_so_far += len;
        w = &z->nxt;
      }
      else if (action == ARCHIVE && cd_only)
      {
        /* for cd_only compression archives just write central directory */
        DisplayRunningStats();
        /* just note the entry */
        strcpy(action_string, "Note entry");
        if (noisy) {
          zfprintf(mesg, " noting: %s", z->oname);
          if (display_usize) {
            zfprintf(mesg, " (");
            DisplayNumString(mesg, z->len );
            zfprintf(mesg, ")");
          }
          mesg_line_started = 1;
          fflush(mesg);
        }
        if (logall)
        {
#ifdef UNICODE_SUPPORT
          if (log_utf8 && z->uname)
            zfprintf(logfile, " noting: %s", z->uname);
          else
#endif
            zfprintf(logfile, " noting: %s", z->oname);
          if (display_usize) {
            zfprintf(logfile, " (");
            DisplayNumString(logfile, z->len );
            zfprintf(logfile, ")");
          }
          logfile_line_started = 1;
          fflush(logfile);
        }

        /* input counts */
        files_so_far++;
        good_bytes_so_far += z->siz;
        bytes_so_far += z->siz;

        w = &z->nxt;

        /* ------------------------------------------- */

#ifdef WINDLL
        /* int64 support in caller */
        if (lpZipUserFunctions->service != NULL)
        {
          char us[100];
          char cs[100];
          long perc = 0;
          char *oname;
          char *uname;

          oname = z->oname;
# ifdef UNICODE_SUPPORT
          uname = z->uname;
# else
          uname = NULL;
# endif

          WriteNumString(z->siz, us);
          WriteNumString(z->len, cs);
          if (z->siz)
            perc = percent(z->len, z->siz);

          if ((*lpZipUserFunctions->service)(oname,
                                             uname,
                                             us,
                                             cs,
                                             z->siz,
                                             z->len,
                                             action_string,
                                             perc))
            ZIPERR(ZE_ABORT, "User terminated operation");
        }
        else
        {
          char *oname;
          char *uname;

          oname = z->oname;
# ifdef UNICODE_SUPPORT
          uname = z->uname;
# else
          uname = NULL;
# endif
          /* no int64 support in caller */
          if (lpZipUserFunctions->service_no_int64 != NULL) {
            filesize64 = z->siz;
            low = (unsigned long)(filesize64 & 0x00000000FFFFFFFF);
            high = (unsigned long)((filesize64 >> 32) & 0x00000000FFFFFFFF);
            if ((*lpZipUserFunctions->service_no_int64)(oname,
                                                        uname,
                                                        low,
                                                        high))
              ZIPERR(ZE_ABORT, "User terminated operation");
          }
        }
/* strange but true: if I delete this and put these two endifs adjacent to
   each other, the Aztec Amiga compiler never sees the second endif!  WTF?? PK */
#endif /* WINDLL */
        /* ------------------------------------------- */

      }
      else if (action == ARCHIVE)
      {
#ifdef DEBUG
        zoff_t here = zftello(y);
#endif

        DisplayRunningStats();
        if (skip_this_disk - 1 != z->dsk)
          /* moved to another disk so start copying again */
          skip_this_disk = 0;
        if (skip_this_disk - 1 == z->dsk) {
          /* skipping this disk */
          strcpy(action_string, "Skip");
          if (noisy) {
            zfprintf(mesg, " skipping: %s", z->oname);
            mesg_line_started = 1;
            fflush(mesg);
          }
          if (logall) {
#ifdef UNICODE_SUPPORT
            if (log_utf8 && z->uname)
              zfprintf(logfile, " skipping: %s", z->uname);
            else
#endif
              zfprintf(logfile, " skipping: %s", z->oname);
            logfile_line_started = 1;
            fflush(logfile);
          }
        } else {
          /* copying this entry */
          strcpy(action_string, "Copy");
          if (noisy) {
            zfprintf(mesg, " copying: %s", z->oname);
            if (display_usize) {
              zfprintf(mesg, " (");
              DisplayNumString(mesg, z->len );
              zfprintf(mesg, ")");
            }
            mesg_line_started = 1;
            fflush(mesg);
          }
          if (logall)
          {
#ifdef UNICODE_SUPPORT
            if (log_utf8 && z->uname)
              zfprintf(logfile, " copying: %s", z->uname);
            else
#endif
              zfprintf(logfile, " copying: %s", z->oname);
            if (display_usize) {
              zfprintf(logfile, " (");
              DisplayNumString(logfile, z->len );
              zfprintf(logfile, ")");
            }
            logfile_line_started = 1;
            fflush(logfile);
          }
        }

        if (skip_this_disk - 1 == z->dsk)
          /* skip entries on this disk */
          z->mark = 0;
        else if ((r = zipcopy(z)) != ZE_OK)
        {
          if (r == ZE_ABORT) {
            ZIPERR(r, "user requested abort");
          } else if (fix != 1) {
            /* exit */
            sprintf(errbuf, "was copying %s", z->oname);
            zipwarn("(try -F to attempt to fix)", "");
            ZIPERR(r, errbuf);
          }
          else /* if (r == ZE_FORM) */ {
#ifdef DEBUG
            zoff_t here = zftello(y);
#endif

            /* seek back in output to start of this entry so can overwrite */
            if (zfseeko(y, current_local_offset, SEEK_SET) != 0){
              ZIPERR(r, "could not seek in output file");
            }
            zipwarn("bad - skipping: ", z->oname);
#ifdef DEBUG
            here = zftello(y);
#endif
            tempzn = current_local_offset;
            bytes_this_split = current_local_offset;
          }
        }
        if (skip_this_disk || !(fix == 1 && r != ZE_OK))
        {
          if (noisy && mesg_line_started) {
            zfprintf(mesg, "\n");
            mesg_line_started = 0;
            fflush(mesg);
          }
          if (logall && logfile_line_started) {
            zfprintf(logfile, "\n");
            logfile_line_started = 0;
            fflush(logfile);
          }
        }
        /* input counts */
        files_so_far++;
        if (r != ZE_OK)
          bad_bytes_so_far += z->siz;
        else
          good_bytes_so_far += z->siz;
        bytes_so_far += z->siz;

        if (r != ZE_OK && fix == 1) {
          /* remove bad entry from list */
          v = z->nxt;                     /* delete entry from list */
          free((zvoid *)(z->iname));
          free((zvoid *)(z->zname));
          free(z->oname);
#ifdef UNICODE_SUPPORT
          if (z->uname) free(z->uname);
#endif /* def UNICODE_SUPPORT */
          if (z->ext)
            /* don't have local extra until zipcopy reads it */
            if (z->extra) free((zvoid *)(z->extra));
          if (z->cext && z->cextra != z->extra)
            free((zvoid *)(z->cextra));
          if (z->com)
            free((zvoid *)(z->comment));
          farfree((zvoid far *)z);
          *w = v;
          zcount--;
        } else {
          w = &z->nxt;
        }

        /* ------------------------------------------- */

#ifdef WINDLL
        /* int64 support in caller */
        if (lpZipUserFunctions->service != NULL)
        {
          char us[100];
          char cs[100];
          long perc = 0;
          char *oname;
          char *uname;

          oname = z->oname;
# ifdef UNICODE_SUPPORT
          uname = z->uname;
# else
          uname = NULL;
# endif

          WriteNumString(z->siz, us);
          WriteNumString(z->len, cs);
          if (z->siz)
            perc = percent(z->len, z->siz);

          if ((*lpZipUserFunctions->service)(oname,
                                             uname,
                                             us,
                                             cs,
                                             z->siz,
                                             z->len,
                                             action_string,
                                             perc))
            ZIPERR(ZE_ABORT, "User terminated operation");
        }
        else
        {
          char *oname;
          char *uname;

          oname = z->oname;
# ifdef UNICODE_SUPPORT
          uname = z->uname;
# else
          uname = NULL;
# endif
          /* no int64 support in caller */
          if (lpZipUserFunctions->service_no_int64 != NULL) {
            filesize64 = z->siz;
            low = (unsigned long)(filesize64 & 0x00000000FFFFFFFF);
            high = (unsigned long)((filesize64 >> 32) & 0x00000000FFFFFFFF);
            if ((*lpZipUserFunctions->service_no_int64)(oname,
                                                        uname,
                                                        low,
                                                        high))
              ZIPERR(ZE_ABORT, "User terminated operation");
          }
        }
/* strange but true: if I delete this and put these two endifs adjacent to
   each other, the Aztec Amiga compiler never sees the second endif!  WTF?? PK */
#endif /* WINDLL */
        /* ------------------------------------------- */

      }
      else
      {
        strcpy(action_string, "Delete");
        DisplayRunningStats();
        if (noisy)
        {
          zfprintf(mesg, "deleting: %s", z->oname);
          if (display_usize) {
            zfprintf(mesg, " (");
            DisplayNumString(mesg, z->len );
            zfprintf(mesg, ")");
          }
          fflush(mesg);
          zfprintf(mesg, "\n");
        }
        if (logall)
        {
#ifdef UNICODE_SUPPORT
          if (log_utf8 && z->uname)
            zfprintf(logfile, "deleting: %s", z->uname);
          else
#endif
            zfprintf(logfile, "deleting: %s", z->oname);
          if (display_usize) {
            zfprintf(logfile, " (");
            DisplayNumString(logfile, z->len );
            zfprintf(logfile, ")");
          }
          zfprintf(logfile, "\n");
          fflush(logfile);
        }
        files_so_far++;
        good_bytes_so_far += z->siz;
        bytes_so_far += z->siz;

        /* ------------------------------------------- */
#ifdef WINDLL
        /* int64 support in caller */
        if (lpZipUserFunctions->service != NULL)
        {
          char us[100];
          char cs[100];
          long perc = 0;
          char *oname;
          char *uname;

          oname = z->oname;
# ifdef UNICODE_SUPPORT
          uname = z->uname;
# else
          uname = NULL;
# endif

          WriteNumString(z->siz, us);
          WriteNumString(z->len, cs);
          if (z->siz)
            perc = percent(z->len, z->siz);

          if ((*lpZipUserFunctions->service)(oname,
                                             uname,
                                             us,
                                             cs,
                                             z->siz,
                                             z->len,
                                             action_string,
                                             perc))
            ZIPERR(ZE_ABORT, "User terminated operation");
        }
        else
        {
          char *oname;
          char *uname;

          oname = z->oname;
# ifdef UNICODE_SUPPORT
          uname = z->uname;
# else
          uname = NULL;
# endif
          /* no int64 support in caller */
          filesize64 = z->siz;
          low = (unsigned long)(filesize64 & 0x00000000FFFFFFFF);
          high = (unsigned long)((filesize64 >> 32) & 0x00000000FFFFFFFF);
          if (lpZipUserFunctions->service_no_int64 != NULL) {
            if ((*lpZipUserFunctions->service_no_int64)(oname,
                                                        uname,
                                                        low,
                                                        high))
                      ZIPERR(ZE_ABORT, "User terminated operation");
          }
        }
/* ZIP64_SUPPORT - I added comments around // comments - does that help below? EG */
/* strange but true: if I delete this and put these two endifs adjacent to
   each other, the Aztec Amiga compiler never sees the second endif!  WTF?? PK */
#endif /* WINDLL */
        /* ------------------------------------------- */

        v = z->nxt;                     /* delete entry from list */
        free((zvoid *)(z->iname));
        free((zvoid *)(z->zname));
        free(z->oname);
#ifdef UNICODE_SUPPORT
        if (z->uname) free(z->uname);
#endif /* def UNICODE_SUPPORT */
        if (z->ext)
          /* don't have local extra until zipcopy reads it */
          if (z->extra) free((zvoid *)(z->extra));
        if (z->cext && z->cextra != z->extra)
          free((zvoid *)(z->cextra));
        if (z->com)
          free((zvoid *)(z->comment));
        farfree((zvoid far *)z);
        *w = v;
        zcount--;
      }
    }
    else
    {
      if (action == ARCHIVE) {
        v = z->nxt;                     /* delete entry from list */
        free((zvoid *)(z->iname));
        free((zvoid *)(z->zname));
        free(z->oname);
#ifdef UNICODE_SUPPORT
        if (z->uname) free(z->uname);
#endif /* def UNICODE_SUPPORT */
        if (z->ext)
          /* don't have local extra until zipcopy reads it */
          if (z->extra) free((zvoid *)(z->extra));
        if (z->cext && z->cextra != z->extra)
          free((zvoid *)(z->cextra));
        if (z->com)
          free((zvoid *)(z->comment));
        farfree((zvoid far *)z);
        *w = v;
        zcount--;
      }
      else
      {
        if (filesync) {
          /* Delete entries if don't match a file on OS */
          BlankRunningStats();
          strcpy(action_string, "Delete");
          if (noisy)
          {
            zfprintf(mesg, "deleting: %s", z->oname);
            if (display_usize) {
              zfprintf(mesg, " (");
              DisplayNumString(mesg, z->len );
              zfprintf(mesg, ")");
            }
            fflush(mesg);
            zfprintf(mesg, "\n");
            mesg_line_started = 0;
          }
          if (logall)
          {
#ifdef UNICODE_SUPPORT
            if (log_utf8 && z->uname)
              zfprintf(logfile, "deleting: %s", z->uname);
            else
#endif
              zfprintf(logfile, "deleting: %s", z->oname);
            if (display_usize) {
              zfprintf(logfile, " (");
              DisplayNumString(logfile, z->len );
              zfprintf(logfile, ")");
            }
            zfprintf(logfile, "\n");

            fflush(logfile);
            logfile_line_started = 0;
          }
        }
        /* copy the original entry */
        else if (!d && !diff_mode && (r = zipcopy(z)) != ZE_OK)
        {
          sprintf(errbuf, "was copying %s", z->oname);
          ZIPERR(r, errbuf);
        }
        w = &z->nxt;
      }
    }
  }


  /* Process the edited found list, adding them to the zip file */
  if (show_what_doing) {
    zfprintf(mesg, "sd: Zipping up new entries\n");
    fflush(mesg);
  }

#ifdef ENABLE_USER_PROGRESS
  u_p_phase = 3;
  u_p_task = "Adding";
#endif /* def ENABLE_USER_PROGRESS */

  diag("zipping up new entries, if any");
  Trace((stderr, "zip diagnostic: fcount=%u\n", (unsigned)fcount));
  for (f = found; f != NULL; f = fexpel(f))
  {
    uzoff_t len;
    /* add a new zfiles entry and set the name */
    if ((z = (struct zlist far *)farmalloc(sizeof(struct zlist))) == NULL) {
      ZIPERR(ZE_MEM, "was adding files to zip file");
    }
    z->nxt = NULL;
    z->name = f->name;
    f->name = NULL;
#ifdef UNICODE_SUPPORT
    z->uname = NULL;          /* UTF-8 name for extra field */
    z->zuname = NULL;         /* externalized UTF-8 name for matching */
    z->ouname = NULL;         /* display version of UTF-8 name with OEM */

# if 0
    /* New AppNote bit 11 allowing storing UTF-8 in path */
    if (utf8_native && f->uname) {
      if (f->iname)
        free(f->iname);
      if ((f->iname = malloc(strlen(f->uname) + 1)) == NULL)
        ZIPERR(ZE_MEM, "Unicode bit 11");
      strcpy(f->iname, f->uname);
#  ifdef WIN32
      if (f->inamew)
        free(f->inamew);
      f->inamew = utf8_to_wchar_string(f->iname);
#  endif
    }
# endif

    /* Only set z->uname if have a non-ASCII Unicode name */
    /* The Unicode path extra field is created if z->uname is not NULL,
       unless on a UTF-8 system, then instead of creating the extra field
       set bit 11 in the General Purpose Bit Flag */
    {
      int is_ascii = 0;

# ifdef WIN32
      if (!no_win32_wide)
        is_ascii = is_ascii_stringw(f->inamew);
      else
        is_ascii = is_ascii_string(f->uname);
# else
      is_ascii = is_ascii_string(f->uname);
# endif

      if (z->uname == NULL) {
        if (!is_ascii)
          z->uname = f->uname;
        else
          free(f->uname);
      } else {
        free(f->uname);
      }
    }
    f->uname = NULL;

    z->utf8_path = f->utf8_path;
#endif /* UNICODE_SUPPORT */

    z->iname = f->iname;
    f->iname = NULL;
    z->zname = f->zname;
    f->zname = NULL;
    z->oname = f->oname;
    f->oname = NULL;
#ifdef UNICODE_SUPPORT_WIN32
    z->namew = f->namew;
    f->namew = NULL;
    z->inamew = f->inamew;
    f->inamew = NULL;
    z->znamew = f->znamew;
    f->znamew = NULL;
#endif
#if defined( UNIX) && defined( __APPLE__)
    z->flags = f->flags;
#endif /* defined( UNIX) && defined( __APPLE__) */

    z->flg = 0;
#ifdef UNICODE_SUPPORT
    if (z->uname && utf8_native)
      z->flg |= UTF8_BIT;
#endif

    z->ext = z->cext = z->com = 0;
    z->extra = z->cextra = NULL;
    z->mark = 1;
    z->dosflag = f->dosflag;
    /* zip it up */
    DisplayRunningStats();

#ifdef ENABLE_USER_PROGRESS
    u_p_name = z->oname;
#endif /* def ENABLE_USER_PROGRESS */

    strcpy(action_string, "Add");

    if (noisy)
    {
#ifdef UNICODE_SUPPORT
      if (unicode_show && z->uname)
        zfprintf(mesg, "  adding: %s", z->uname);
      else
        zfprintf(mesg, "  adding: %s", z->oname);
#else
        zfprintf(mesg, "  adding: %s", z->oname);
#endif
      mesg_line_started = 1;
      fflush(mesg);
    }
    if (logall)
    {
#ifdef UNICODE_SUPPORT
      if (log_utf8 && z->uname)
        zfprintf(logfile, "  adding: %s", z->uname);
      else
#endif
        zfprintf(logfile, "  adding: %s", z->oname);
      logfile_line_started = 1;
      fflush(logfile);
    }
    /* initial scan */

    /* zip up found list */

    len = f->usize;
    if ((r = zipup(z)) != ZE_OK  && r != ZE_OPEN && r != ZE_MISS)
    {
      zipmessage_nl("", 1);
      /*
      if (noisy)
      {
#if (!defined(MACOS) && !defined(ZIP_DLL_LIB))
        putc('\n', mesg);
        fflush(mesg);
#else
        fprintf(stdout, "\n");
#endif
        mesg_line_started = 0;
        fflush(mesg);
      }
      if (logall) {
        fprintf(logfile, "\n");
        logfile_line_started = 0;
        fflush(logfile);
      }
      */
      sprintf(errbuf, "was zipping %s", z->oname);
      ZIPERR(r, errbuf);
    }
    if (r == ZE_OPEN || r == ZE_MISS)
    {
      o = 1;
      zipmessage_nl("", 1);
      /*
      if (noisy)
      {
#if (!defined(MACOS) && !defined(ZIP_DLL_LIB))
        putc('\n', mesg);
        fflush(mesg);
#else
        fprintf(stdout, "\n");
#endif
        mesg_line_started = 0;
        fflush(mesg);
      }
      if (logall) {
        fprintf(logfile, "\n");
        logfile_line_started = 0;
        fflush(logfile);
      }
      */
      if (r == ZE_OPEN) {
        zipwarn_indent("could not open for reading: ", z->oname);
        zipwarn_indent( NULL, strerror( errno));
        if (bad_open_is_error) {
          sprintf(errbuf, "was zipping %s", z->name);
          ZIPERR(r, errbuf);
        }
      } else {
        zipwarn_indent("file and directory with the same name (2): ",
         z->oname);
      }
      files_so_far++;
      bytes_so_far += len;
      bad_files_so_far++;
      bad_bytes_so_far += len;
#ifdef ENABLE_USER_PROGRESS
      u_p_name = NULL;
#endif /* def ENABLE_USER_PROGRESS */
      free((zvoid *)(z->name));
      free((zvoid *)(z->iname));
      free((zvoid *)(z->zname));
      free(z->oname);
#ifdef UNICODE_SUPPORT
      if (z->uname)
        free(z->uname);
# ifdef UNICODE_SUPPORT_WIN32
      if (z->namew)
        free((zvoid *)(z->namew));
      if (z->inamew)
        free((zvoid *)(z->inamew));
      if (z->znamew)
        free((zvoid *)(z->znamew));
# endif
#endif /* def UNICODE_SUPPORT */
      farfree((zvoid far *)z);
    }
    else
    {
      files_so_far++;
      /* current size of file (just before reading) */
      good_bytes_so_far += z->len;
      /* size of file on initial scan */
      bytes_so_far += len;
      *w = z;
      w = &z->nxt;
      zcount++;
    }
  }
  if (key != NULL)
  {
    free((zvoid *)key);
    key = NULL;
  }

  /* final status 3/17/05 EG */

#ifdef ENABLE_USER_PROGRESS
  u_p_phase = 4;
  u_p_task = "Finishing";
#endif /* def ENABLE_USER_PROGRESS */

#ifdef WINDOWS_LONG_PATHS
  if (archive_has_long_path) {
    zipwarn("Archive contains at least one Windows long path", "");
    zipwarn("- Archive may not be readable in some utilities", "");
  }
#endif

  if (bad_files_so_far)
  {
    char tempstrg[100];

    zfprintf(mesg, "\nzip warning: Not all files were readable\n");
    zfprintf(mesg, "  files/entries read:  %lu", files_total - bad_files_so_far);
    WriteNumString(good_bytes_so_far, tempstrg);
    zfprintf(mesg, " (%s bytes)", tempstrg);
    zfprintf(mesg, "  skipped:  %lu", bad_files_so_far);
    WriteNumString(bad_bytes_so_far, tempstrg);
    zfprintf(mesg, " (%s bytes)\n", tempstrg);
    fflush(mesg);
  }
  if (logfile && bad_files_so_far)
  {
    char tempstrg[100];

    zfprintf(logfile, "\nzip warning: Not all files were readable\n");
    zfprintf(logfile, "  files/entries read:  %lu", files_total - bad_files_so_far);
    WriteNumString(good_bytes_so_far, tempstrg);
    zfprintf(logfile, " (%s bytes)", tempstrg);
    zfprintf(logfile, "  skipped:  %lu", bad_files_so_far);
    WriteNumString(bad_bytes_so_far, tempstrg);
    zfprintf(logfile, " (%s bytes)", tempstrg);
  }

  /* Get one line comment for each new entry */
  if (show_what_doing) {
    zfprintf(mesg, "sd: Get comment if any\n");
    fflush(mesg);
  }
#if defined(AMIGA) || defined(MACOS)
  if (comadd || filenotes)
  {
    if (comadd)
#else
  if (comadd && !include_stream_ef)
  {
#endif
    {
      if (comment_stream == NULL) {
#ifndef RISCOS
        comment_stream = (FILE*)fdopen(fileno(stderr), "r");
#else
        comment_stream = stderr;
#endif
      }
      if ((e = malloc(MAXCOM + 1)) == NULL) {
        ZIPERR(ZE_MEM, "was reading comment lines (1)");

      }
    }

#ifdef __human68k__
    setmode(fileno(comment_stream), O_TEXT);
#endif
#ifdef MACOS
    if (noisy) zfprintf(mesg, "\nStart commenting files ...\n");
#endif
    for (z = zfiles; z != NULL; z = z->nxt)
      if (z->mark)
#if defined(AMIGA) || defined(MACOS)
        if (filenotes && (p = GetComment(z->zname)))
        {
          if (z->comment = malloc(k = strlen(p)+1))
          {
            z->com = k;
            strcpy(z->comment, p);
          }
          else
          {
            free((zvoid *)e);
            ZIPERR(ZE_MEM, "was reading filenotes");
          }
        }
        else if (comadd)
#endif /* AMIGA || MACOS */
        {
#if defined(ZIPLIB) || defined(ZIPDLL)
          ecomment(z);
#else
          if (noisy) {
            if (z->com && z->comment) {
              zfprintf(mesg, "\nCurrent comment for %s:\n %s", z->oname, z->comment);
              zfprintf(mesg, "\nEnter comment (hit ENTER to keep, TAB ENTER to remove) for %s:\n ", z->oname);
            } else {
              zfprintf(mesg, "\nEnter comment for %s:\n ", z->oname);
            }
          }
          if (fgets(e, MAXCOM+1, comment_stream) != NULL)
          {
            if (strlen(e) > 1) {
              if (strlen(e) == 2 && e[0] == '\t')
                e[0] = '\0';
              if ((p = (char *)malloc((comment_size = strlen(e))+1)) == NULL)
              {
                free((zvoid *)e);
                ZIPERR(ZE_MEM, "was reading comment lines (s2)");
              }
              strcpy(p, e);
              if (p[comment_size - 1] == '\n')
                p[--comment_size] = 0;
              if (z->com && z->comment) {
                free(z->comment);
                z->com = 0;
              }
              z->comment = p;
              if (comment_size == 0) {
                free(z->comment);
                z->comment = NULL;
              }
              /* zip64 support 09/05/2003 R.Nausedat */
              z->com = comment_size;
            }
          }
#endif /* defined(ZIPLIB) || defined(ZIPDLL) */

#if 0
          if (noisy)
            zfprintf(mesg, "Enter comment for %s:\n", z->oname);
          if (fgets(e, MAXCOM+1, comment_stream) != NULL)
          {
            if ((p = malloc((comment_size = strlen(e))+1)) == NULL)
            {
              free((zvoid *)e);
              ZIPERR(ZE_MEM, "was reading comment lines (2)");
            }
            strcpy(p, e);
            if (p[comment_size - 1] == '\n')
              p[--comment_size] = 0;
            z->comment = p;
            /* zip64 support 09/05/2003 R.Nausedat */
            z->com = comment_size;
          }
#endif
        }
#ifdef MACOS
    if (noisy) zfprintf(mesg, "\n...done");
#endif
#if defined(AMIGA) || defined(MACOS)
    if (comadd)
      free((zvoid *)e);
    GetComment(NULL);           /* makes it free its internal storage */
#else
    free((zvoid *)e);
#endif
  }


/* --- start Archive Comment code --- */

  /* Get (possibly multi-line) archive comment. */
  if (zipedit)
  {

#ifdef ZIP_DLL_LIB
    acomment(zcomlen);
# if 0
    if ((p = malloc(strlen(szCommentBuf)+1)) == NULL) {
      ZIPERR(ZE_MEM, "was setting comments to null (2)");
    }
    if (szCommentBuf[0] != '\0')
       lstrcpy(p, szCommentBuf);
    else
       p[0] = '\0';
    free((zvoid *)zcomment);
    zcomment = NULL;
    GlobalUnlock(hStr);
    GlobalFree(hStr);
    zcomment = p;
    zcomlen = strlen(zcomment);
# endif

# if 0
    if ((p = malloc(zcomlen+1)) == NULL) {
      ZIPERR(ZE_MEM, "was setting comments to null (2)");
    }
    if (szCommentBuf[0] != '\0')
       lstrcpy(p, szCommentBuf);
    else
       p[0] = '\0';
    free((zvoid *)zcomment);
    zcomment = NULL;
    zcomment = p;
    zcomlen = strlen(zcomment);
# endif

#else /* def ZIP_DLL_LIB */

    /* not LIB or DLL */

    /* Try to get new comment first, then replace old comment (if any). - EG */
    char *new_zcomment;
    int new_zcomlen;
    int new_len;
    int keep_current = 0;
    int new_read;

    if (comment_stream == NULL) {
# ifndef RISCOS
      comment_stream = (FILE*)fdopen(fileno(stderr), "r");
# else
      comment_stream = stderr;
# endif
    }
    if (noisy)
    {
      fputs("\n", mesg);
      fputs("---------------------------------------\n", mesg);
    }
    if (noisy && zcomlen)
    {
      /* Display old archive comment, if any. */
      fputs("Current zip file comment is:\n", mesg);
      fputs("----------\n", mesg);
      fwrite(zcomment, 1, zcomlen, mesg);
      if (zcomment[zcomlen-1] != '\n')
        putc('\n', mesg);
      fputs("----------\n", mesg);
    }
    if (noisy)
    {
      if (zcomlen)
        fputs(
  "Enter new zip file comment (end with . line) or hit ENTER to keep existing:\n",
        mesg);
      else
        fputs("Enter new zip file comment (end with . line):\n", mesg);
      fputs("----------\n", mesg);
    }

# if (defined(AMIGA) && (defined(LATTICE)||defined(__SASC)))
    flushall();  /* tty input/output is out of sync here */
# endif
# ifdef __human68k__
    setmode(fileno(comment_stream), O_TEXT);
# endif
# ifdef MACOS
    /* 2014-04-15 SMS.
     * Apparently, on old MacOS we accept only one line.
     * The code looks sub-optimal.
     */
    if ((e = malloc(MAXCOM + 1)) == NULL) {
      ZIPERR(ZE_MEM, "was reading comment lines (3)");
    }
    if (zcomment) {
      free(zcomment);
      zcomment = NULL;
    }
    zprintf("\n enter new zip file comment \n");
    if (fgets(e, MAXCOM+1, comment_stream) != NULL) {
        if ((p = malloc((k = strlen(e))+1)) == NULL) {
            free((zvoid *)e);
            ZIPERR(ZE_MEM, "was reading comment lines (4)");
        }
        strcpy(p, e);
        if (p[k-1] == '\n') p[--k] = 0;
        zcomment = p;
    }
    free((zvoid *)e);
    /* if fgets() fails, zcomment is undefined */
    if (!zcomment) {
      zcomlen = 0;
    } else {
      zcomlen = strlen(zcomment);
    }
# else /* !MACOS */
    /* 2014-04-15 SMS.
     * Changed to stop adding "\r\n" within lines longer than MAXCOM.
     *
     * Now:
     * Read comment text lines until ".\n" or EOF.
     * Allocate (additional) storage in increments of MAXCOM+3.  (MAXCOM = 256)
     * Read pieces up to MAXCOM+1.
     * Convert a read-terminating "\n" character to "\r\n".
     * (If too long, truncate at maximum allowed length (and complain)?)
     */
    /* Keep old comment until got new one.  Should be a way to display the
       old comment and keep it.  */
    new_zcomment = NULL;
    new_zcomlen = 0;
    while (1)
    {
      /* The first line of the comment (up to the first CR + LF) is the
         one-line description reported by some utilities. */

      new_len = new_zcomlen + MAXCOM + 3;
      /* The total allowed length of the End Of Central Directory Record
         is 65535 bytes and the archive comment can be up to 65535 - 22 =
         65513 bytes of this.  We need to ensure the total comment length
         is no more than this. */
      if (new_len > 32767) {
        new_len = 32767;
        if (new_len == new_zcomlen) {
          break;
        }
      }
      /* Allocate (initial or more) space for the file comment. */
      if ((new_zcomment = realloc(new_zcomment, new_len)) == NULL)
      {
        ZIPERR(ZE_MEM, "was reading comment lines (5)");
#  if 0
        new_zcomlen = 0;
        break;
#  endif
      }

      /* Read up to (MAXCOM + 1) characters.  Quit if none available. */
      if (fgets((new_zcomment + new_zcomlen), (MAXCOM + 1), comment_stream) == NULL)
      {
        if (new_zcomlen == 0)
          keep_current = 1;
        break;
      }

      new_read = strlen(new_zcomment + new_zcomlen);
      
      /* If the first line is empty or just a newline, keep current comment */
      if (new_zcomlen == 0 &&
          (new_read == 0 || strcmp((new_zcomment), "\n") == 0))
      {
        keep_current = 1;
        break;
      }

      /* Detect ".\n" comment terminator in new line read.  Quit, if found. */
      if (new_zcomlen &&
          (new_read == 0 || strcmp((new_zcomment + new_zcomlen), ".\n") == 0))
        break;

      /* Calculate the new length (old_length + newly_read). */
      new_zcomlen += new_read;

      /* Convert (bare) terminating "\n" to "\r\n". */
      if (*(new_zcomment + new_zcomlen - 1) == '\n')
      { /* Have terminating "\n". */
        if ((new_zcomlen <= 1) || (*(new_zcomment + new_zcomlen - 2) != '\r'))
        {
          /* "\n" is not already preceded by "\r", so insert "\r". */
          *(new_zcomment + new_zcomlen) = '\r';
          new_zcomlen++;
          *(new_zcomment + new_zcomlen) = '\n';
        }
      }
    } /* while (1) */

    if (keep_current)
    {
      free(new_zcomment);
    }
    else
    {
      if (zcomment)
      {
        /* Free old archive comment. */
        free(zcomment);
      }

      /* Use new comment. */
      zcomment = new_zcomment;
      zcomlen = new_zcomlen;

      /* If unsuccessful, make tidy.
       * If successful, terminate the file comment string as desired.
       */
      if (zcomlen == 0)
      {
        if (zcomment)
        {
          /* Free comment storage.  (Empty line read?) */
          free(zcomment);
          zcomment = NULL;
        }
      }
      else
      {
        /* If it's missing, add a final "\r\n".
          * (Do we really want this?  We could add one only if we've seen
          * one before, so that a normal one- or multi-line comment would
          * always end with our usual line ending, but one-line,
          * EOF-terminated input would not.  (Need to add a flag.))
          */
        /* As far as is known current utilities don't expect a terminating
            CR + LF so don't add one.  Some testing of other utilities may
            clarify this. */
#  if 0
        if (*(zcomment+ zcomlen) != '\n')
        {
          *(zcomment+ (zcomlen++)) = '\r';
          *(zcomment+ zcomlen) = '\n';
        }
#  endif
        /* We could NUL-terminate the string here, but no one cares. */
        /* This could be useful for debugging purposes */
        *(zcomment + zcomlen + 1) = '\0';
      }
    }
    if (noisy)
      fputs("----------\n", mesg);

#  if 0
    /* SMSd. */
    fprintf( stderr, " zcl = %d, zc: >%.*s<.\n", zcomlen, zcomlen, zcomment);
#  endif /* 0 */

# endif /* ?MACOS */

#endif /* def ZIP_DLL_LIB [else] */
  }

/* --- end Archive Comment code --- */


  if (display_globaldots) {
#ifndef ZIP_DLL_LIB
    putc('\n', mesg);
#else
    zfprintf(stdout,"%c",'\n');
#endif
    mesg_line_started = 0;
  }

  /* Write central directory and end header to temporary zip */

#ifdef ENABLE_USER_PROGRESS
  u_p_phase = 5;
  u_p_task = "Done";
#endif /* def ENABLE_USER_PROGRESS */

  if (show_what_doing) {
    zfprintf(mesg, "sd: Writing central directory\n");
    fflush(mesg);
  }
  diag("writing central directory");
  k = 0;                        /* keep count for end header */
  c = tempzn;                   /* get start of central */
  n = t = 0;
  for (z = zfiles; z != NULL; z = z->nxt)
  {
    if (z->mark || !(diff_mode || filesync)) {
      if ((r = putcentral(z)) != ZE_OK) {
        ZIPERR(r, tempzip);
      }
      tempzn += 4 + CENHEAD + z->nam + z->cext + z->com;
      n += z->len;
      t += z->siz;
      k++;
    }
  }

  if (k == 0)
    zipwarn("zip file empty", "");
  if (verbose) {
    zfprintf(mesg, "total bytes=%s, compressed=%s -> %d%% savings\n",
            zip_fzofft(n, NULL, "u"), zip_fzofft(t, NULL, "u"), percent(n, t));
    fflush(mesg);
  }
  if (logall) {
    zfprintf(logfile, "total bytes=%s, compressed=%s -> %d%% savings\n",
            zip_fzofft(n, NULL, "u"), zip_fzofft(t, NULL, "u"), percent(n, t));
    fflush(logfile);
  }

#ifdef WINDLL
  if (*lpZipUserFunctions->finish != NULL) {
    char susize[100];
    char scsize[100];
    long p;

    WriteNumString(n, susize);
    WriteNumString(t, scsize);
    p = percent(n, t);
    (*lpZipUserFunctions->finish)(susize, scsize, n, t, p);
  }
#endif

  if (cd_only) {
    zipwarn("cd_only mode: archive has no data - use only for diffs", "");
  }

  t = tempzn - c;               /* compute length of central */
  diag("writing end of central directory");
  if (show_what_doing) {
    zfprintf(mesg, "sd: Writing end of central directory\n");
    fflush(mesg);
  }

  if ((r = putend(k, t, c, zcomlen, zcomment)) != ZE_OK) {
    ZIPERR(r, tempzip);
  }

  /*
  tempzf = NULL;
  */
  if (fclose(y)) {
    ZIPERR(d ? ZE_WRITE : ZE_TEMP, tempzip);
  }
  y = NULL;
  if (in_file != NULL) {
    fclose(in_file);
    in_file = NULL;
  }
  /*
  if (x != NULL)
    fclose(x);
  */

  /* Free some memory before spawning unzip */
#ifdef USE_ZLIB
  zl_deflate_free();
#else
  lm_free();
#endif
#ifdef BZIP2_SUPPORT
  bz_compress_free();
#endif


#ifndef ZIP_DLL_LIB
  /* Test new zip file before overwriting old one or removing input files */
  if (test)
    check_zipfile(tempzip, argv[0]);
#endif


  /* Replace old zip file with new zip file, leaving only the new one */
  if (strcmp(out_path, "-") && !d)
  {
    diag("replacing old zip file with new zip file");
    if (show_what_doing) {
      zfprintf(mesg, "sd: Replacing old zip file\n");
      fflush(mesg);
    }
    if ((r = replace(out_path, tempzip)) != ZE_OK)
    {
      zipwarn("new zip file left as: ", tempzip);
      free((zvoid *)tempzip);
      tempzip = NULL;
      ZIPERR(r, "was replacing the original zip file");
    }
    free((zvoid *)tempzip);
  }
  tempzip = NULL;
  if (zip_attributes && strcmp(zipfile, "-")) {
    setfileattr(out_path, zip_attributes);
#ifdef VMS
    /* If the zip file existed previously, restore its record format: */
    if (x != NULL)
      (void)VMSmunch(out_path, RESTORE_RTYPE, NULL);
#endif
  }
  if (strcmp(zipfile, "-")) {
    if (show_what_doing) {
      zfprintf(mesg, "sd: Setting file type\n");
      fflush(mesg);
    }

    set_filetype(out_path);
  }


#ifdef BACKUP_SUPPORT
  /* if using the -BT backup option, output updated control/status file */
  if (backup_type) {
    struct filelist_struct *apath;
    FILE *fcontrol;
    struct tm *now;
    time_t clocktime;
    char end_datetime[20];

    if (show_what_doing) {
      zfprintf(mesg, "sd: Creating -BT control file\n");
      fflush(mesg);
    }

    if ((fcontrol = fopen(backup_control_path, "w")) == NULL) {
      zipwarn("Could not open for writing:  ", backup_control_path);
      ZIPERR(ZE_WRITE, "error writing to backup control file");
    }

    /* get current time */
    time(&clocktime);
    now = localtime(&clocktime);
    sprintf(end_datetime, "%04d%02d%02d_%02d%02d%02d",
      now->tm_year + 1900, now->tm_mon + 1, now->tm_mday, now->tm_hour,
      now->tm_min, now->tm_sec);

    zfprintf(fcontrol, "info: Zip Backup Control File (DO NOT EDIT)\n");
    if (backup_type == BACKUP_FULL) {
      zfprintf(fcontrol, "info: backup type:  full\n");
    }
    else if (backup_type == BACKUP_DIFF) {
      zfprintf(fcontrol, "info: backup type:  differential\n");
    }
    else if (backup_type == BACKUP_INCR) {
      zfprintf(fcontrol, "info: backup type:  incremental\n");
    }
    zfprintf(fcontrol, "info: Start date/time:  %s\n", backup_start_datetime);
    zfprintf(fcontrol, "info: End date/time:    %s\n", end_datetime);
    zfprintf(fcontrol, "path: %s\n", backup_control_path);
    zfprintf(fcontrol, "full: %s\n", backup_full_path);

    if (backup_type == BACKUP_DIFF) {
      zfprintf(fcontrol, "diff: %s\n", backup_output_path);
    }
    for (; apath_list; ) {
      if (backup_type == BACKUP_INCR) {
        zfprintf(fcontrol, "incr: %s\n", apath_list->name);
      }
      free(apath_list->name);
      apath = apath_list;
      apath_list = apath_list->next;
      free(apath);
    }

    if (backup_type == BACKUP_INCR) {
      /* next backup this will be an incremental archive to include */
      zfprintf(fcontrol, "incr: %s\n", backup_output_path);
    }

    if (backup_start_datetime) {
      free(backup_start_datetime);
      backup_start_datetime = NULL;
    }
    if (backup_dir) {
      free(backup_dir);
      backup_dir = NULL;
    }
    if (backup_name) {
      free(backup_name);
      backup_name = NULL;
    }
    if (backup_control_path) {
      free(backup_control_path);
      backup_control_path = NULL;
    }
    if (backup_full_path) {
      free(backup_full_path);
      backup_full_path = NULL;
    }
    if (backup_output_path) {
      free(backup_output_path);
      backup_output_path = NULL;
    }
  }
#endif /* BACKUP_SUPPORT */


#if defined(WIN32)
  /* All looks good so, if requested, clear the DOS archive bits */
  if (clear_archive_bits) {
    if (noisy)
      zipmessage("Clearing archive bits...", "");
    for (z = zfiles; z != NULL; z = z->nxt)
    {
# ifdef UNICODE_SUPPORT_WIN32
      if (z->mark) {
        if (!no_win32_wide) {
          if (!ClearArchiveBitW(z->namew)){
            zipwarn("Could not clear archive bit for: ", z->oname);
          }
        } else {
          if (!ClearArchiveBit(z->name)){
            zipwarn("Could not clear archive bit for: ", z->oname);
          }
        }
      }
# else
      if (!ClearArchiveBit(z->name)){
        zipwarn("Could not clear archive bit for: ", z->oname);
      }
# endif
    }
  }
#endif /* defined(WIN32) */

#ifdef IZ_CRYPT_AES_WG
  /* close random pool */
  if (encryption_method >= AES_MIN_ENCRYPTION) {
    if (show_what_doing) {
      zfprintf(mesg, "sd: Closing AES_WG random pool\n");
      fflush(mesg);
    }
    prng_end(&aes_rnp);
    free(zsalt);
  }
#endif /* def IZ_CRYPT_AES_WG */

#ifdef IZ_CRYPT_AES_WG_NEW
  /* clean up and end operation   */
  ccm_end(&aesnew_ctx);  /* the mode context             */
#endif /* def IZ_CRYPT_AES_WG_NEW */

  /* finish logfile (it gets closed in freeup() called by finish()) */
  if (logfile) {
      struct tm *now;
      time_t clocktime;

      zfprintf(logfile, "\nTotal %ld entries (", files_total);
      if (good_bytes_so_far != bytes_total) {
        zfprintf(logfile, "planned ");
        DisplayNumString(logfile, bytes_total);
        zfprintf(logfile, " bytes, actual ");
        DisplayNumString(logfile, good_bytes_so_far);
        zfprintf(logfile, " bytes)");
      } else {
        DisplayNumString(logfile, bytes_total);
        zfprintf(logfile, " bytes)");
      }

      /* get current time */

      time(&clocktime);
      now = localtime(&clocktime);
      zfprintf(logfile, "\nDone %s", asctime(now));
  }

  /* Finish up (process -o, -m, clean up).  Exit code depends on o. */
#if (!defined(VMS) && !defined(CMS_MVS))
  free((zvoid *) zipbuf);
#endif /* !VMS && !CMS_MVS */
  RETURN(finish(o ? ZE_OPEN : ZE_OK));
}


/***************  END OF ZIP MAIN CODE  ***************/


/*
 * VMS (DEC C) initialization.
 */
#ifdef VMS
# include "decc_init.c"
#endif



/* Ctrl/T (VMS) AST or SIGUSR1 handler for user-triggered progress
 * message.
 * UNIX: arg = signal number.
 * VMS:  arg = Out-of-band character mask.
 */
#ifdef ENABLE_USER_PROGRESS

# ifndef VMS
#  include <limits.h>
#  include <time.h>
#  include <sys/times.h>
#  include <sys/utsname.h>
#  include <unistd.h>
# endif /* ndef VMS */

USER_PROGRESS_CLASS void user_progress( arg)
int arg;
{
  /* VMS Ctrl/T automatically puts out a line like:
   * ALP::_FTA24: 07:59:43 ZIP       CPU=00:00:59.08 PF=2320 IO=52406 MEM=333
   * (host::tty local_time program cpu_time page_faults I/O_ops phys_mem)
   * We do something vaguely similar on non-VMS systems.
   */
# ifndef VMS

#  ifdef CLK_TCK
#   define clk_tck CLK_TCK
#  else /* def CLK_TCK */
  long clk_tck;
#  endif /* def CLK_TCK [else] */
#  define U_P_NODENAME_LEN 32

  static int not_first = 0;                             /* First time flag. */
  static char u_p_nodename[ U_P_NODENAME_LEN+ 1];       /* "host::tty". */
  static char u_p_prog_name[] = "zip";                  /* Program name. */

  struct utsname u_p_utsname;
  struct tm u_p_loc_tm;
  struct tms u_p_tms;
  char *cp;
  char *tty_name;
  time_t u_p_time;
  float stime_f;
  float utime_f;

  /* On the first time through, get the host name and tty name, and form
   * the "host::tty" string (in u_p_nodename) for the intro line.
   */
  if (not_first == 0)
  {
    not_first = 1;
    /* Host name.  (Trim off any domain info.  (Needed on Tru64.)) */
    uname( &u_p_utsname);
    if (u_p_utsname.nodename == NULL)
    {
      *u_p_nodename = '\0';
    }
    else
    {
      strncpy( u_p_nodename, u_p_utsname.nodename, (U_P_NODENAME_LEN- 8));
      u_p_nodename[ 24] = '\0';
      cp = strchr( u_p_nodename, '.');
      if (cp != NULL)
        *cp = '\0';
    }

    /* Terminal name.  (Trim off any leading "/dev/"). */
    tty_name = ttyname( 0);
    if (tty_name != NULL)
    {
      cp = strstr( tty_name, "/dev/");
      if (cp != NULL)
        tty_name += 5;

      strcat( u_p_nodename, "::");
      strncat( u_p_nodename, tty_name,
       (U_P_NODENAME_LEN- strlen( u_p_nodename)));
    }
  }

  /* Local time.  (Use reentrant localtime_r().) */
  u_p_time = time( NULL);
  localtime_r( &u_p_time, &u_p_loc_tm);

  /* CPU time. */
  times( &u_p_tms);
#  ifndef CLK_TCK
  clk_tck = sysconf( _SC_CLK_TCK);
#  endif /* ndef CLK_TCK */
  utime_f = ((float)u_p_tms.tms_utime)/ clk_tck;
  stime_f = ((float)u_p_tms.tms_stime)/ clk_tck;

  /* Put out intro line. */
  zfprintf( stderr, "%s %02d:%02d:%02d %s CPU=%.2f\n",
   u_p_nodename,
   u_p_loc_tm.tm_hour, u_p_loc_tm.tm_min, u_p_loc_tm.tm_sec,
   u_p_prog_name,
   (stime_f+ utime_f));

# endif /* ndef VMS */

  if (u_p_task != NULL)
  {
    if (u_p_name == NULL)
      u_p_name = "";

    zfprintf( stderr, "   %s: %s\n", u_p_task, u_p_name);
  }

# ifndef VMS
  /* Re-establish this SIGUSR1 handler.
   * (On VMS, the Ctrl/T handler persists.) */
  signal( SIGUSR1, user_progress);
# endif /* ndef VMS */
}

#endif /* def ENABLE_USER_PROGRESS */

