/*
 * pblProcessLog - Functions for logging of the ARpoise net distribution server.
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
#include <stdarg.h>
#include "pblProcess.h"

extern int pblProcessLogOn;

/*
 * Print a character to the log file.
 */
void pblLogChar(char c)
{
	FILE* outfile = pblProcessLogOn ? stderr : stdout;
	fputc(c, outfile);
	if ('\n' == c)
	{
		fflush(outfile);
	}

#ifdef _WIN32
	if (pblProcessLogOn)
	{
		fputc(c, stdout);
		if ('\n' == c)
		{
			fflush(stdout);
		}
	}
#endif
}

/*
 * Print an error text to the log file.
 */
void pblLogError(char* format, ...)
{
	FILE* outfile = pblProcessLogOn ? stderr : stdout;

	if (pblProcessLogOn)
	{
		struct timeval tvNow = { 0 };
		gettimeofday(&tvNow, NULL);
		time_t t = tvNow.tv_sec;
		struct tm* tm = localtime(&t);

		fprintf(outfile, "E%02d%02d%02d-%02d%02d%02d.%03ld ",
			tm->tm_year % 100,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			tvNow.tv_usec / 1000L
		);
		fprintf(outfile, "PID %lu: ETEXT=", (unsigned long)getpid());

#ifdef _WIN32
		fprintf(stdout, "E%02d%02d%02d-%02d%02d%02d.%03ld ",
			tm->tm_year % 100,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			tvNow.tv_usec / 1000L
		);
		fprintf(stdout, "PID %lu: ETEXT=", (unsigned long)getpid());
#endif
	}
	else
	{
		fprintf(outfile, "%s: ", pblProcess.name);
	}

	va_list ap;
	va_start(ap, format);
	vfprintf(outfile, format, ap);
	va_end(ap);

	fflush(outfile);

#ifdef _WIN32
	if (pblProcessLogOn)
	{
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		va_end(ap);

		fflush(stdout);
	}
#endif
}

/*
 * Print an information text to the log file.
 */
void pblLogInfo(char* format, ...)
{
	FILE* outfile = pblProcessLogOn ? stderr : stdout;

	if (pblProcessLogOn)
	{
		struct timeval tvNow = { 0 };
		gettimeofday(&tvNow, NULL);
		time_t t = tvNow.tv_sec;
		struct tm* tm = localtime(&t);

		fprintf(outfile, "L%02d%02d%02d-%02d%02d%02d.%03ld ",
			tm->tm_year % 100,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			(long)tvNow.tv_usec / 1000L
		);
	}
	else
	{
		fprintf(outfile, "%s: ", pblProcess.name);
	}

	va_list ap;
	va_start(ap, format);
	vfprintf(outfile, format, ap);
	va_end(ap);

	fflush(outfile);

#ifdef _WIN32
	if (pblProcessLogOn)
	{
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		va_end(ap);

		fflush(stdout);
	}
#endif
}

/*
 * Print a text to the trace file.
 */
void pblLogTrace(char* format, ...)
{
	FILE* outfile = stderr;
	if (pblProcessLogOn)
	{
		if (pblProcess.traceFile)
		{
			outfile = pblProcess.traceFile;
		}
	}

	struct timeval tvNow = { 0 };
	gettimeofday(&tvNow, NULL);
	time_t t = tvNow.tv_sec;
	struct tm* tm = localtime(&t);

	fprintf(outfile, "T%02d%02d%02d-%02d%02d%02d.%03ld ",
		tm->tm_year % 100,
		tm->tm_mon + 1,
		tm->tm_mday,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec,
		tvNow.tv_usec / 1000L
	);
	fprintf(outfile, "PID %lu: ", (unsigned long)getpid());

	va_list ap;
	va_start(ap, format);
	vfprintf(outfile, format, ap);
	va_end(ap);

	fflush(outfile);

#ifdef _WIN32
	if (outfile != stdout)
	{
		fprintf(stdout, "T%02d%02d%02d-%02d%02d%02d.%03ld ",
			tm->tm_year % 100,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			tvNow.tv_usec / 1000L
		);
		fprintf(stdout, "PID %lu: ", (unsigned long)getpid());

		va_start(ap, format);
		vfprintf(stdout, format, ap);
		va_end(ap);

		fflush(stdout);
	}
#endif
}
