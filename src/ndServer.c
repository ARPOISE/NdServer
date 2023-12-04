/*
 * ndServer.c - The main of the ARpoise net distribution server.
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
#include "pblProcess.h"
#include "ndServer.h"
#include "tcpPacket.h"

 /*
  * The exit function
  */
void ndServerExit(int exitrc)
{
#ifdef _WIN32
	WSACleanup();
#endif

	LOG_INFO((">> Exit Server, rc = %d\n", exitrc));
}

/*
 * The option -TRACE enables traces to the logfile.
 * This option can be toggled sending a kill -SIGUSR2 to the process.
 *
 * The option -D prevents the process from disconnecting from the control terminal for debug purposes.
 *
 * The environment variable ROOTDIR has to be set.
 * The process assumes existence of the directories ROOTDIR/log and ROOTDIR/status.
 *
 * The process can be terminated by kill -SIGTERM
 */
int main(int argc, char* argv[])
{
	pblProcessExitProc = ndServerExit;
	if (pblProcessInit(&argc, argv, 1, 1) != PBL_PROCESS_RET_OK)
	{
		pblProcessExit(101);
	}

	LOG_INFO((">>\n"));
	LOG_INFO(("FILE      = %s\n", argv[0]));
	LOG_INFO(("VERSION   = %s\n", PBL_PROCESS_VERSION));
	LOG_INFO(("COMPILED  = %s, %s\n", PBL_PROCESS_TIME, PBL_PROCESS_DATE));

	/*
	 * log the parameters
	 */
	for (int i = 1; i < argc; i++)
	{
		LOG_INFO(("ARGV[ %d ] = %s\n", i, argv[i]));
	}

	if (pblProcess.port == 0)
	{
		fprintf(stderr, "No port given for server!\n");
		fprintf(stderr, "usage: %s -p port\n", argv[0]);
		pblProcessExit(102);
	}

#ifdef _WIN32
	int rc;
	WSADATA WSAData;
	if ((rc = WSAStartup(MAKEWORD(1, 1), &WSAData)) == 0)
	{
		LOG_INFO(("WSAStartup: Description '%s', Status '%s'\n",
			(char*)WSAData.szDescription,
			(char*)WSAData.szSystemStatus));
	}
	else
	{
		LOG_ERROR(("WSAStartup failed! rc %d, error %d\n",
			rc, WSAGetLastError()));
		pblProcessExit(103);
	}
#endif

	if (ndDispatchCreateListenSocket() < 0)
	{
		pblProcessExit(104);
	}

	ndDispatchLoop();
	ndDispatchExit();

	LOG_INFO((">> Going down!\n"));
	pblProcessExit(0);
	return 0;
}
