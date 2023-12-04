/*
 * pblProcess.h - Include file for all processes of the ARpoise net distribution server.
 *
 * Copyright (C) 2023, Tamiko Thiel and Peter Graf - All Rights Reserved
 *
 * ARpoise/NdServer - Augmented Reality point of interest service environment / Net Distribution Server
 *
 * This file is part of ARpoise.
 *
 *  ARpoise is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ARpoise is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ARpoise.  If not, see <https://www.gnu.org/licenses/>.
 *
 * For more information on
 *
 * Tamiko Thiel, see www.TamikoThiel.com/
 * Peter Graf, see www.mission-base.com/peter/
 * ARpoise, see www.ARpoise.com/
 */

#ifndef _PBL_PROCESS_H_
#define _PBL_PROCESS_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32

#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <process.h>
#include <share.h>
#include <ctype.h>
#include <winsock.h>
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/locking.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <string.h>
#include <stdio.h>

#else /* UNIX */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>

#endif

#define pblRand() ((rand()<<24) ^ (rand()<<16) ^ (rand()<<8) ^ rand())

#ifdef _WIN32

#define snprintf     _snprintf

#define PBL_PROCESS_PATH_LENGTH    1024
#define PBL_PROCESS_PATHSEP_CHR    '\\'
#define PBL_PROCESS_PATHSEP_STR    "\\"
#define PBL_PROCESS_SYSROOT        PBL_PROCESS_PATHSEP_STR

#else /* UNIX */

#define PBL_PROCESS_PATH_LENGTH    1024
#define PBL_PROCESS_NULL_DEVICE    "/dev/null"
#define PBL_PROCESS_TTY_DEVICE     "/dev/tty"
#define PBL_PROCESS_PATHSEP_CHR    '/'
#define PBL_PROCESS_PATHSEP_STR    "/"
#define PBL_PROCESS_SYSROOT        PBL_PROCESS_PATHSEP_STR

#endif

#define PBL_PROCESS_ROOT          "ROOTDIR"

#ifdef _WIN32

#define PBL_LOG_INFO_DIR            "\\log"
#define PBL_PROCESS_STATUS_DIR      "\\status"

#else /* UNIX */

#define PBL_LOG_INFO_DIR            "/log"
#define PBL_PROCESS_STATUS_DIR      "/status"

#endif

#define PBL_PROCESS_RET_OK           0     /* ok, positive return value           */
#define PBL_PROCESS_ERR_FORK        -1     /* fork() failed, see errno            */
#define PBL_PROCESS_ERR_SETPGID     -2     /* setpgid() failed, see errno         */
#define PBL_PROCESS_ERR_MALLOC      -3     /* malloc() failed, see errno          */
#define PBL_PROCESS_ERR_DUP2        -4     /* dup2() failed, see errno            */
#define PBL_PROCESS_ERR_SIGACTION   -5     /* sigaction() failed, see errno       */

#define PBL_PROCESS_ERR_ROOT       -10     /* ROOTDIR variable not set            */
#define PBL_PROCESS_ERR_LOGFILE    -11     /* open on logfile failed, see errno   */
#define PBL_PROCESS_ERR_LOCKFILE   -12     /* open on lockfile failed, see errno  */
#define PBL_PROCESS_ERR_NPROCESS   -13     /* process already running too often   */
#define PBL_PROCESS_ERR_PARAM      -14     /* function called with illegal params */
#define PBL_PROCESS_ERR_CHDIR      -15     /* change working directory failed     */

#define PBL_PROCESS_STATUS_RUNNING   1     /* pblProcess is running                 */
#define PBL_PROCESS_STATUS_DIED      2     /* pblProcess has died                   */

#ifndef LOG_ERROR
#define LOG_ERROR(X) pblLogError X
#endif

#ifndef LOG_INFO
#define LOG_INFO(X) pblLogInfo X
#endif

#ifndef LOG_CHAR
#define LOG_CHAR(X) pblLogChar X
#endif

#ifndef LOG_TRACE
#define LOG_TRACE(X) if(pblProcess.traceIsOn) pblLogTrace X
#endif

#define PBL_PROCESS_VERSION    "1.00"

#if !defined (PBL_PROCESS_DATE)

#if !defined (__DATE__)
#define __DATE__ "Unknown"
#endif

#define PBL_PROCESS_DATE __DATE__
#endif

#if !defined (PBL_PROCESS_TIME)

#if !defined (__TIME__)
#define __TIME__ "unknown"
#endif
#define PBL_PROCESS_TIME __TIME__
#endif

#define PBL_PROCESS_FREE(ptr) {if(ptr){free(ptr); ptr = NULL;}} 

	typedef struct PblProcess_s
	{
		char* name;
		unsigned short port;
		time_t startTime;
		char* rootDir;
		int status;
		volatile int doWork;
		FILE* traceFile;
		volatile int traceIsOn;
		volatile int exitCode;
		char* logFilename;
		char* nameAndPort;

	} PblProcess;

	extern PblProcess pblProcess;

	extern int pblProcessLogOn;
	extern int pblProcessLogReopen();
	extern int pblProcessLockfileFd;
	extern int pblProcessInit(int* argc, char* argv[], int pdetach, int plogon);
	extern int pblProcessSignalHandlerSet(int sig, void (*handler)(int));
	extern void pblProcessSigHupHandler(int sig);
	extern void pblProcessSigTermHandler(int sig);
	extern void pblProcessSigUsr2Handler(int sig);
	extern void pblProcessSigPipeHandler(int sig);
	extern void pblProcessExit(int exitcode);
	extern void (*pblProcessExitProc)(int);
	extern void* pblProcessMemdup(char* tag, const void* m, size_t size);
	extern void* pblProcessStrdup(char* tag, const char* s);
	extern void* pblProcessMalloc(char* tag, size_t size);
	extern void* pblProcessPrintf(char* tag, const char* format, ...);
	extern void pblLogError(char* format, ...);
	extern void pblLogInfo(char* format, ...);
	extern void pblLogChar(char c);
	extern void pblLogTrace(char* format, ...);

#ifdef _WIN32
	extern int gettimeofday();
#endif
#ifdef __cplusplus
}
#endif
#endif
