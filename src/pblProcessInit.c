/*
 * pblProcessInit.c - Process handling functions of the ARpoise net distribution server.
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
#include <stdio.h>
#include <memory.h>
#include <stdarg.h>
#include <errno.h>

#include "pblProcess.h"

#define PBL_PROCESS__MAXLOCKFILES     512   /* maximum of lockfiles to test       */

PblProcess pblProcess = { 0 };

void (*pblProcessExitProc)(int) = NULL;
int pblProcessLogOn = 0;

int pblProcessLockfileFd = -1;
char* process__lockfile_name = NULL;

static void processSigAlrmHandler(int sig);
static void processSigChldHandler(int sig);
static int pblProcessDetach(int argc, char* argv[]);

/*
 * Return the name of the process.
 */
char* pblProcessName(char* name, unsigned short port)
{
	if (port)
	{
		return pblProcessPrintf(NULL, "%s %u", name, (unsigned int)port);
	}
	else
	{
		return pblProcessStrdup(NULL, name);
	}
}

/*
 * Return the name and port of the process.
 */
char* pblProcessNamePort(char* name, unsigned short port)
{
	if (port)
	{
		return pblProcessPrintf(NULL, "%c%c%u", name[0] ? name[0] : '_', name[1] ? name[1] : '_', (unsigned int)port);
	}
	else
	{
		return pblProcessStrdup(NULL, name);
	}
}

void pblProcessSigTermHandler(int sig)
{
	LOG_TRACE(("Received SIGTERM/SIGINT\n"));

	pblProcess.doWork = 0;
	errno = EINTR;
}

void pblProcessSigPipeHandler(int sig)
{
	LOG_INFO(("Received SIGPIPE\n"));
	errno = EINTR;
}

static void processSigAlrmHandler(int sig)
{
	errno = EINTR;
}

#ifndef _WIN32

static void processSigChldHandler(int sig)
{
	int pid = 1;
	int status;

	while (pid > 0)
	{
		pid = waitpid(-1, &status, WNOHANG);
	}
	errno = EINTR;
}

void pblProcessSigHupHandler(int sig)
{
	if (!pblProcessLogOn)
	{
		return;
	}

	LOG_INFO(("Received a SIGHUP. Closing log!\n"));

	(void)pblProcessLogReopen();
	errno = EINTR;
}

void pblProcessSigUsr2Handler(int sig)
{
	LOG_TRACE(("Tracing turned off!\n"));

	/*
	 * Toggle the tracing flag
	 */
	pblProcess.traceIsOn = !(pblProcess.traceIsOn);

	LOG_TRACE(("Tracing turned on!\n"));
	errno = EINTR;
}

#endif

/*
 * Reopen the log file.
 */
int pblProcessLogReopen()
{
	char* ptr;

	LOG_INFO(("Reopening log file %s!\n", pblProcess.logFilename));

	/*
	 * If we are using a logfile at all
	 */
	if (!pblProcessLogOn)
	{
		LOG_INFO(("Cannot reopen log file, not a file!\n"));
		return 0;
	}

#ifdef _WIN32

	fflush(stderr);

	int fd = sopen(pblProcess.logFilename, O_RDWR | O_APPEND | O_CREAT | O_TRUNC, SH_DENYNO, S_IREAD | S_IWRITE);
	if (fd >= 0)
	{
		if ((freopen(pblProcess.logFilename, "a", stderr) == NULL) ||
			(dup2(fd, fileno(stderr)) != 0))
		{
			/*
			 * We do not have a file to write to any more, try a last attempt and go down
			 */
			ptr = "Got a SIGHUP, but could not dup2 to stderr, going down\n";
			write(fd, ptr, (unsigned int)strlen(ptr));
			write(fd, strerror(errno), (unsigned int)strlen(strerror(errno)));
			write(fd, ptr, (unsigned int)strlen(ptr));

			pblProcessExit(-1);
		}
		close(fd);

		LOG_INFO(("STARTED new log, running since %s.\n", ctime(&(pblProcess.startTime))));
		LOG_TRACE(("STARTED new log, trace is on.\n"));
	}
	else
	{
		LOG_ERROR(("Tried to reopen log file. But open %s failed! Errmsg %s\n",
			pblProcess.logFilename, strerror(errno)
			));
	}

#else   /* UNIX */

	int fd = open(pblProcess.logFilename, O_WRONLY | O_APPEND | O_CREAT, (mode_t)0664);
	if (fd >= 0)
	{
		if (fd != 2)
		{
			int rc = dup2(fd, 2);
			if (rc < 0)
			{
				/*
				 * We do not have a file to write to any more
				 * try a last attempt and go down
				 */
				ptr = "Got a SIGHUP, but could not dup2 to stderr, going down\n";
				if (write(fd, ptr, strlen(ptr)) > 0)
				{
					if (write(fd, strerror(errno), strlen(strerror(errno))) > 0)
					{
						if (write(fd, ptr, strlen(ptr)) > 0)
						{
							pblProcessExit(-1);
						}
					}
				}
				pblProcessExit(-1);
			}
			close(fd);
		}

		LOG_INFO(("STARTED new log, running since %s", ctime(&(pblProcess.startTime))));
		LOG_TRACE(("STARTED new log, trace is on.\n"));
	}
	else
	{
		LOG_ERROR(("Tried to reopen log file. But open %s failed! Errmsg %s\n",
			pblProcess.logFilename, strerror(errno)
			));
	}
#endif
	return 0;
}

/*
 * Detach a process from control terminals and so on.
 *
 * int rc == PBL_PROCESS_RET_OK, the call went ok
 * int rc == PBL_PROCESS_ERR_FORK, the function could not do a fork, see errno
 * int rc == PBL_PROCESS_ERR_SETPGID, the function could not do a setpgid, see errno
 * int rc == PBL_PROCESS_ERR_SIGACTION, the function could not do a sigaction,
*/
static int pblProcessDetach(int argc, char* argv[])
{
#ifndef _WIN32

	int childpid;            /* return value of fork               */
	int rc;                  /* generic return code                */
	int fd;                  /* generic file descriptor            */
	int i;                   /* generic counter                    */

	fd = -1;

	/*
	 * see whether we really should do a fork
	 * or called with the debug or D option
	 */
	for (i = 0; i < argc; i++)
	{
		if ((strcmp(argv[i], "-D") == 0) || (strcmp(argv[i], "-debug") == 0))
		{
			return PBL_PROCESS_RET_OK;
		}
	}

	/*
	 * Ignore terminal stop signals
	 */
#ifdef SIGTTOU

	rc = pblProcessSignalHandlerSet(SIGTTOU, SIG_IGN);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGTTOU, SIG_IGN ) failed!\n"));
		return rc;
	}

#endif
#ifdef SIGTTIN

	rc = pblProcessSignalHandlerSet(SIGTTIN, SIG_IGN);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGTTIN, SIG_IGN ) failed!\n"));
		return rc;
	}

#endif
#ifdef SIGTSTP

	rc = pblProcessSignalHandlerSet(SIGTSTP, SIG_IGN);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGTSTP, SIG_IGN ) failed!\n"));
		return rc;
	}

#endif

	/*
	 * Fork and let the parent exit. This also guarantees the first child is not a process group leader
	 */
	childpid = fork();
	if (childpid < 0)
	{
		LOG_ERROR(("First fork failed! errmsg: %s!\n", strerror(errno)));
		return PBL_PROCESS_ERR_FORK;
	}
	else if (childpid > 0)
	{
		/*
		 * the parent does an exit
		 */
		exit(0);
	}

	/*
	 * First child process!!!
	 *
	 * Disassociate from controlling terminal and process group.
	 */
	rc = setpgid((int)0, (int)getpid());
	if (rc)
	{
		/*
		 * setpgid error
		 */
		LOG_ERROR(("setpgid( 0, %lu ) failed! errmsg: %s!\n",
			(unsigned long)getpid(),
			strerror(errno)
			));
		return PBL_PROCESS_ERR_SETPGID;
	}

#ifdef TIOCNOTTY        /* BSD */
	if ((fd = open(PBL_PROCESS_TTY_DEVICE, O_RDWR)) >= 0)
	{
		ioctl(fd, TIOCNOTTY, NULL); /* loose controlling terminal */
		close(fd);
	};
#endif

	/*
	 * Immune to pgrp leader death
	 */
	rc = pblProcessSignalHandlerSet(SIGHUP, SIG_IGN);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGHUP, SIG_IGN ) failed!\n"));
		return rc;
	}

	childpid = fork();
	if (childpid < 0)
	{
		LOG_ERROR(("second fork failed! errmsg: %s!\n", strerror(errno)));
		return PBL_PROCESS_ERR_FORK;
	}
	else if (childpid > 0)
	{
		/*
		 * The parent does an exit
		 */
		exit(0);
	}

	/*
	 * Second child !!!
	 */

	 /*
	  * Ignore alarms
	  */
	rc = pblProcessSignalHandlerSet(SIGALRM, processSigAlrmHandler);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGALRM, processSigAlrmHandler ) failed!\n"));
		return rc;
	}

	/*
	 * On SIGHUP, we reopen the log file
	 */
	rc = pblProcessSignalHandlerSet(SIGHUP, pblProcessSigHupHandler);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGHUP, pblProcessSigHupHandler ) failed!\n"));
		return rc;
	}

	/*
	 * On SIGCHLD, we wait for all children that exit in order to avoid zombies
	 */
	rc = pblProcessSignalHandlerSet(SIGCHLD, processSigChldHandler);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGCHLD, processSigChldHandler ) failed!\n"));
		return rc;
	}


	/*
	 * Move current directory off mounted filesystem to "/"
	 */
	if (chdir(PBL_PROCESS_SYSROOT) < 0)
	{
		LOG_ERROR(("chdir( %s ) failed! errmsg: %s!\n",
			PBL_PROCESS_SYSROOT,
			strerror(errno)
			));
		return PBL_PROCESS_ERR_CHDIR;
	}

	/*
	 * clear inherited file mode creation mask
	 */
	umask(0);

#endif
	return PBL_PROCESS_RET_OK;
}

/*
 * Set a signal handler function for a specific signal
 *
 * int rc == PBL_PROCESS_RET_OK        ok, positive return value
 * int rc == PBL_PROCESS_ERR_SIGACT    sigaction() failed, see errno
 */
int pblProcessSignalHandlerSet(int sig, void (*handler)(int))
{
#if defined( _WIN32 )

	signal(sig, handler);

#else

	struct sigaction new_action;        /* buffers for calling sigaction   */
	struct sigaction old_action;

	/*
	 * Don't block additional signals
	 */
	sigemptyset(&new_action.sa_mask);

	/*
	 * we want to see the other signals
	 */
	new_action.sa_flags = 0;

	/*
	 * set handler
	 */
	new_action.sa_handler = handler;
	if (sigaction(sig, &new_action, &old_action) < 0)
	{
		LOG_ERROR(("sigaction( %d, handler ) failed! errmsg: %s!\n",
			sig, strerror(errno)
			));
		return PBL_PROCESS_ERR_SIGACTION;
	}
#endif
	return PBL_PROCESS_RET_OK;
}

/*
 * Initialize an PBL process or command.
 *
 *  PBL_PROCESS_RET_OK                  call went ok
 *  PBL_PROCESS_ERR_FORK                fork() failed, see errno
 *  PBL_PROCESS_ERR_SETPGID             setpgid() failed, see errno
 *  PBL_PROCESS_ERR_MALLOC              malloc() failed, see errno
 *  PBL_PROCESS_ERR_DUP2                dup2() failed, see errno
 *  PBL_PROCESS_ERR_ROOT                ROOTDIR variable not set
 *  PBL_PROCESS_ERR_LOGFILE             open on logfile failed, see errno
 *  PBL_PROCESS_ERR_LOCKFILE            open on lockfile failed, see errno
 *  PBL_PROCESS_ERR_NPROCESS            process already running too often
 *  PBL_PROCESS_ERR_PARAM               the pathnames derived from the ROOTDIR									directory and the processname are to long
 */
int pblProcessInit(int* pargc, char* argv[], int pdetach, int plogon)
{
	static char* function = "pblProcessInit";
	static int first = 1;
	int i;
	int argc;
	char* lockFilename;
	char buffer[PBL_PROCESS_PATH_LENGTH];
	int rc;
	int fd;
	time_t now;

	int tmp_file_fd = -1;
	int oflag = oflag = O_RDWR | O_CREAT;

	if (pargc)
	{
		argc = *pargc;
	}
	else
	{
		argc = 1;
	}

	if (first)
	{
		first = 0;
		memset(&pblProcess, 0, sizeof(pblProcess));
		pblProcess.doWork = 1;

		for (i = 1; i < argc; i++)
		{
			char* arg = argv[i];

			if (!strcmp(arg, "-p") && i < argc - 1)
			{
				pblProcess.port = atoi(argv[++i]);
				continue;
			}
			if (!strcmp(arg, "-ROOTDIR") && i < argc - 1)
			{
				pblProcess.rootDir = argv[++i];
				continue;
			}
			if (!strcmp(arg, "-TRACE"))
			{
				pblProcess.traceIsOn = 1;
				continue;
			}
		}
	}
	else
	{
		return PBL_PROCESS_ERR_NPROCESS;
	}

	char* ptr;
	for (ptr = argv[0] + strlen(argv[0]); ptr > argv[0]; ptr--)
	{
		if (*(ptr - 1) == PBL_PROCESS_PATHSEP_CHR)
			break;
#ifdef _WIN32
		if (*(ptr - 1) == '/')
		{
			break;
		}
#endif
	}

#ifdef _WIN32
	char* ptr2 = strrchr(ptr, '.');
	if (ptr2)
	{
		*ptr2 = '\0';
	}
	ptr2 = strstr(ptr, ".exe");
	if (ptr2)
	{
		*ptr2 = '\0';
	}
	ptr2 = strstr(ptr, ".EXE");
	if (ptr2)
	{
		*ptr2 = '\0';
	}
#endif

	pblProcess.name = pblProcessName(ptr, pblProcess.port);
	if (!pblProcess.name)
	{
		LOG_ERROR(("Cannot malloc %d bytes! errmsg: %s!\n",
			strlen(ptr) + 1, strerror(errno)));
		return PBL_PROCESS_ERR_MALLOC;
	}

	pblProcess.nameAndPort = pblProcessNamePort(ptr, pblProcess.port);
	if (!pblProcess.nameAndPort)
	{
		LOG_ERROR(("Cannot malloc %d bytes! errmsg: %s!\n",
			strlen(ptr) + 1, strerror(errno)));
		return PBL_PROCESS_ERR_MALLOC;
	}

	if (!pblProcess.rootDir)
	{
		pblProcess.rootDir = getenv(PBL_PROCESS_ROOT);
		if (!pblProcess.rootDir)
		{
			LOG_ERROR(("Environment variable %s not set!\n", PBL_PROCESS_ROOT));
			return PBL_PROCESS_ERR_ROOT;
		}
	}

	if ((strlen(pblProcess.rootDir) + strlen(PBL_LOG_INFO_DIR) +
		strlen(PBL_PROCESS_PATHSEP_STR) + strlen(pblProcess.nameAndPort) + 5)
		>= PBL_PROCESS_PATH_LENGTH)
	{
		LOG_ERROR(("Path names too long ! rootDir %s name %s\n",
			pblProcess.rootDir, pblProcess.nameAndPort));
		return PBL_PROCESS_ERR_PARAM;
	}

	i = (int)strlen(pblProcess.rootDir)
		+ (int)strlen(PBL_LOG_INFO_DIR)
		+ (int)strlen(PBL_PROCESS_PATHSEP_STR)
		+ (int)strlen(pblProcess.nameAndPort)
		+ 64;

	pblProcess.logFilename = malloc(i);
	if (!pblProcess.logFilename)
	{
		LOG_ERROR(("Cannot malloc %d bytes! errmsg: %s!\n",
			i, strerror(errno)));
		return PBL_PROCESS_ERR_MALLOC;
	}

	strcpy(pblProcess.logFilename, pblProcess.rootDir);
	strcat(pblProcess.logFilename, PBL_LOG_INFO_DIR);
	strcat(pblProcess.logFilename, PBL_PROCESS_PATHSEP_STR);
	strcat(pblProcess.logFilename, pblProcess.nameAndPort);

	if (!strchr(pblProcess.nameAndPort, '.'))
	{
		strcat(pblProcess.logFilename, ".log");
	}

	if ((strlen(pblProcess.rootDir) + strlen(PBL_LOG_INFO_DIR) +
		strlen(PBL_PROCESS_PATHSEP_STR) + strlen(pblProcess.nameAndPort))
		>= PBL_PROCESS_PATH_LENGTH)
	{
		LOG_ERROR(("Path names too long ! rootDir %s name %s\n",
			pblProcess.rootDir, pblProcess.nameAndPort));
		return PBL_PROCESS_ERR_PARAM;
	}

	if (pdetach || plogon)
	{
#ifdef _WIN32
		fd = sopen(pblProcess.logFilename, O_RDWR | O_APPEND | O_CREAT, SH_DENYNO, S_IREAD | S_IWRITE);
#else
		fd = open(pblProcess.logFilename,
			O_WRONLY | O_APPEND | O_CREAT,
			(mode_t)0664
		);
#endif
		if (fd < 0)
		{
			LOG_ERROR(("Cannot open or create file %s ! errmsg: %s!\n",
				pblProcess.logFilename,
				strerror(errno)
				));
			return PBL_PROCESS_ERR_LOGFILE;
		}

#ifdef _WIN32                   
		if (freopen(pblProcess.logFilename, "a", stderr) == NULL)
		{
			LOG_ERROR(("Cannot freopen stderr ! errmsg: %s!\n", strerror(errno)));
			return PBL_PROCESS_ERR_DUP2;
		}

		rc = dup2(fd, fileno(stderr));
		if (rc != 0)
		{
			LOG_ERROR(("Cannot dup2 stderr ! errmsg: %s!\n", strerror(errno)));
			return PBL_PROCESS_ERR_DUP2;
		}
#else
		rc = dup2(fd, 2);
		if (rc != 2)
		{
			LOG_ERROR(("Cannot dup2 stderr ! errmsg: %s!\n", strerror(errno)));
			return PBL_PROCESS_ERR_DUP2;
		}
		if ((fd = open("/dev/null", O_WRONLY)) >= 0)
		{
			rc = dup2(fd, 1);
			if (rc != 1)
			{
				LOG_ERROR(("Cannot dup2 stdout ! errmsg: %s!\n", strerror(errno)));
				return PBL_PROCESS_ERR_DUP2;
			}
		}
#ifndef _POSIX_OPEN_MAX
#define _POSIX_OPEN_MAX          16
#endif
		for (fd = 3; fd < _POSIX_OPEN_MAX; fd++)
		{
			close(fd);
		}
#endif
		pblProcessLogOn = 1;
	}

	if (pdetach)
	{
		rc = pblProcessDetach(argc, argv);
		if (rc != PBL_PROCESS_RET_OK)
		{
			return rc;
		}
	}

	pblProcess.traceFile = stderr;
	pblProcess.startTime = (time_t)time((time_t*)0);

	struct timeval tvNow = { 0 };
	gettimeofday(&tvNow, (struct timezone*)NULL);
	srand(getpid() ^ pblRand() ^ tvNow.tv_sec ^ tvNow.tv_usec);

	if (pblProcessLogOn)
	{
		LOG_INFO(("STARTED with pid %d at %s", getpid(), ctime(&(pblProcess.startTime))));
		LOG_TRACE(("STARTED tracing at %s", ctime(&(pblProcess.startTime))));
	}

	if ((strlen(pblProcess.rootDir) + strlen(PBL_PROCESS_STATUS_DIR) +
		strlen(PBL_PROCESS_PATHSEP_STR) + strlen(pblProcess.nameAndPort) + 3)
		>= PBL_PROCESS_PATH_LENGTH)
	{
		LOG_ERROR(("Path names too long ! rootDir %s name %s\n",
			pblProcess.rootDir, pblProcess.nameAndPort));
		return PBL_PROCESS_ERR_PARAM;
	}

	for (i = 1; i <= PBL_PROCESS__MAXLOCKFILES; i++)
	{
		lockFilename = pblProcessPrintf(function, "%s%s%s%s.%d",
			pblProcess.rootDir,
			PBL_PROCESS_STATUS_DIR,
			PBL_PROCESS_PATHSEP_STR,
			pblProcess.nameAndPort,
			i
		);
		if (!lockFilename)
		{
			LOG_ERROR(("Cannot malloc %d bytes! errmsg: %s!\n",
				strlen(pblProcess.rootDir) + strlen(PBL_PROCESS_STATUS_DIR) +
				strlen(PBL_PROCESS_PATHSEP_STR) + strlen(pblProcess.nameAndPort) + 3,
				strerror(errno)));
			return PBL_PROCESS_ERR_MALLOC;
		}

		if (tmp_file_fd < 0)
		{
			tmp_file_fd = open(pblProcess.logFilename,
				O_WRONLY | O_CREAT,
#ifdef _WIN32
				S_IREAD | S_IWRITE
#else
				(mode_t)0664
#endif
			);
		}

		pblProcessLockfileFd = open(lockFilename, oflag,
#ifdef _WIN32
			S_IREAD | S_IWRITE
#else
			(mode_t)0664
#endif
		);
		if (pblProcessLockfileFd < 0)
		{
#ifndef _WIN32
			if (errno == EWOULDBLOCK)
				continue;
#else
			if (errno == EACCES)
			{
				continue;
			}
#endif

			LOG_ERROR(("Cannot open or create file '%s'! errmsg: %s!\n",
				lockFilename, strerror(errno)));
			return PBL_PROCESS_ERR_LOCKFILE;
		}

#ifndef _WIN32
		struct flock flockbuf;
		flockbuf.l_type = F_WRLCK;
		flockbuf.l_whence = SEEK_SET;
		flockbuf.l_start = (off_t)0;
		flockbuf.l_len = (off_t)0;
		flockbuf.l_pid = (int)0;

		rc = fcntl(pblProcessLockfileFd, F_SETLK, &flockbuf);
		if (rc == -1)
		{
			close(pblProcessLockfileFd);
			pblProcessLockfileFd = -1;
			continue;
		}
		else
#endif
		{
			pblProcess.status = PBL_PROCESS_STATUS_RUNNING;
			memset(buffer, 0, sizeof(buffer));
			if (read(pblProcessLockfileFd, buffer, sizeof(buffer) - 1) > 0)
			{
				ptr = strchr(buffer, ' ');
				if (ptr)
					*ptr = 0;
				if (*buffer)
				{
					int pid = atoi(buffer);
					if (pid)
					{
						pblProcess.status = PBL_PROCESS_STATUS_DIED;
					}
				}
				lseek(pblProcessLockfileFd, 0L, SEEK_SET);
			}

			now = (time_t)time((time_t*)0);
			sprintf(buffer, "%08lu %s", (unsigned long)getpid(), ctime(&now));

			rc = write(pblProcessLockfileFd, buffer, (unsigned int)strlen(buffer));
			if (rc != (unsigned int)strlen(buffer))
			{
				LOG_ERROR(("Could not write to the lock file %s! errmsg: %s\n",
					lockFilename, strerror(errno)));
			}
			else
			{
				if (process__lockfile_name)
				{
					free(process__lockfile_name);
				}
				process__lockfile_name = lockFilename;
			}
			break;
	}
}

	/*
	 * close the temporary workaround file if it is open
	 */
	if (tmp_file_fd >= 0)
	{
		close(tmp_file_fd);
		tmp_file_fd = -1;
	}

	/*
	 * On SIGTERM, we stop working
	 */
	rc = pblProcessSignalHandlerSet(SIGTERM, pblProcessSigTermHandler);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGTERM, pblProcessSigTermHandler ) failed!\n"));
		return rc;
	}

	/*
	 * On SIGINT, we stop working
	 */
	rc = pblProcessSignalHandlerSet(SIGINT, pblProcessSigTermHandler);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGINT, pblProcessSigTermHandler ) failed!\n"));
		return rc;
	}

#ifndef _WIN32
	/*
	 * we catch SIGPIPE
	 */
	rc = pblProcessSignalHandlerSet(SIGPIPE, pblProcessSigPipeHandler);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGPIPE, pblProcessSigPipeHandler ) failed!\n"));
		return rc;
	}

	/*
	 * on SIGUSR2, we toggle the pblProcess.traceIsOn flag
	 */
	rc = pblProcessSignalHandlerSet(SIGUSR2, pblProcessSigUsr2Handler);
	if (rc < 0)
	{
		LOG_ERROR(("signal( SIGUSR2, pblProcessSigUsr2Handler ) failed!\n"));
		return rc;
	}
#endif
	return PBL_PROCESS_RET_OK;
}

/*
 * An executable that was started by pblProcessInit is exited by this call.
 */
void pblProcessExit(int pexitcode)
{
	static char* function = "pblProcessExit";
	int rc;
	time_t now = (time_t)time((time_t*)0);
	char buffer[PBL_PROCESS_PATH_LENGTH + 1] = { 0 };
	char* ptr;

#ifdef _WIN32
	if (pexitcode < 0)
	{
		pexitcode *= -1;
	}
#endif

	if (pblProcessLockfileFd >= 0)
	{
		buffer[0] = '\0';
		if (lseek(pblProcessLockfileFd, (off_t)0, SEEK_SET) == (off_t)0)
		{
			rc = read(pblProcessLockfileFd, buffer, sizeof(buffer) - 1);
			if (rc >= 0)
			{
				buffer[rc] = '\0';
				for (ptr = buffer; *ptr; ptr++)
				{
					if (*ptr == ' ')
					{
						*ptr = '\0';
						break;
					}
				}
			}
		}

		if (atol(buffer) == getpid())
		{
			if (lseek(pblProcessLockfileFd, (off_t)0, SEEK_SET) == (off_t)0)
			{
				char* line = pblProcessPrintf(function, "%08ld %s", (long)0, ctime(&now));
				if (line)
				{
					rc = write(pblProcessLockfileFd, buffer, (unsigned int)strlen(buffer));
					if (rc != (unsigned int)strlen(buffer))
					{
						LOG_INFO(("Could not write to the lock file! errmsg: %s\n",
							strerror(errno)
							));
					}
					else
					{
						if (process__lockfile_name
							&& *process__lockfile_name
							&& '2' == process__lockfile_name[
								strlen(process__lockfile_name) - 1
							])
						{
							unlink(process__lockfile_name);
						}
					}
				}
			}
		}

		close(pblProcessLockfileFd);
		pblProcessLockfileFd = -1;
	}

	/*
	 * If we have a log file open we write a shutdown message
	 */
	if (pblProcessLogOn)
	{
		if (pexitcode)
		{
			LOG_ERROR(("Process going down with an error!; EXITCODE=%d;\n", pexitcode));
		}
		else
		{
			LOG_INFO(("GOING DOWN! exitcode %d at %s", pexitcode, ctime(&now)));
		}
	}

	pblProcess.exitCode = pexitcode;
	if (pblProcessExitProc)
	{
		pblProcessExitProc(pexitcode);
	}

#ifdef _WIN32

	WSACleanup();

#endif

	if (pblProcessLogOn)
	{
		if (pblProcess.exitCode)
		{
			LOG_ERROR(("EXITING! exitcode %d at %s\n", pblProcess.exitCode, ctime(&now)));
		}
		else
		{
			LOG_INFO(("EXITING! exitcode %d at %s\n", pexitcode, ctime(&now)));
		}
	}
	exit(pblProcess.exitCode);
	LOG_ERROR(("pblProcessExit: exit returned!\n"));
}

#ifdef _WIN32

int gettimeofday(struct timeval* tv, struct timezone* tz)
{
	time_t now = (time_t)time((time_t*)0);
	SYSTEMTIME Time;
	GetSystemTime(&Time);
	if (tv)
	{
		tv->tv_sec = (long)now;
		tv->tv_usec = Time.wMilliseconds * 1000;
	}
	return 0;
}
#endif

/*
 * Duplicate some memory, similar to strdup.
 *
 * void * retptr == NULL: out of memory
 * void * retptr != NULL: pointer to size bytes of memory
 */
void* pblProcessMemdup(char* tag, const void* m, size_t size)
{
	if (!tag)
	{
		tag = "pblProcessMemdup";
	}

	if (size < 1)
	{
		LOG_ERROR(("%s: malloc 0 bytes is illegal.\n", tag));
		return NULL;
	}

	void* ptr = malloc(size);
	if (!ptr)
	{
		LOG_ERROR(("%s: malloc %d bytes failed! errmsg %s\n", tag, size, strerror(errno)));
		return NULL;
	}

	if (m)
	{
		memcpy(ptr, m, size);
	}
	else
	{
		memset(ptr, 0, size);
	}
	return ptr;
}

/*
 * Duplicate a string, similar to strdup
 *
 * void * retptr == NULL: out of memory
 * void * retptr != NULL: pointer to the enw string
 */
void* pblProcessStrdup(char* tag, const char* s)
{
	return s ? pblProcessMemdup(tag, s, strlen(s) + 1) : pblProcessMalloc(tag, 1);
}

/*
 * Private version of malloc, memory is initialized with 0s.
 *
 * void * retptr == NULL: out of memory
 * void * retptr != NULL: pointer to size bytes of memory
 */
void* pblProcessMalloc(char* tag, size_t size)
{
	return pblProcessMemdup(tag ? tag : "pblProcessMalloc", NULL, size);
}

/**
 * Similar to sprintf, but returns a pointer to a string that is allocated
 *
 * void * retptr == NULL: out of memory
 * void * retptr != NULL: pointer to size bytes of memory
 */
void* pblProcessPrintf(char* tag, const char* format, ...)
{
	static char* function = "pblProcessPrintf";
	if (!format)
	{
		return pblProcessMalloc(tag ? tag : function, 1);
	}

	size_t size = 4096 - 1;
	char buffer[4096];

	va_list args;
	va_start(args, format);
	int rc = vsnprintf(buffer, size, format, args);
	va_end(args);
	if (rc < 0 || rc >= size)
	{
		LOG_ERROR(("%s: vsnprintf failed! %d, errmsg %s\n", function, rc, strerror(errno)));
		return NULL;
	}
	return pblProcessStrdup(tag ? tag : function, buffer);
}
