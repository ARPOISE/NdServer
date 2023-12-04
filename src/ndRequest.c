/*
 * ndRequest.c - Handle all requests received by the ARpoise net distribution server.
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
#include "ndConnection.h"
#include "pbl.h"

 /*
  * Handle a SET request.
  *
  * rc = 0: success
  * rc < 0: error
  */
static int ndHandleSet(NdConnection* conn)
{
	static char* function = "ndHandleSet";
	NdScene* scene = NULL;

	if (conn->SCU)
	{
		scene = ndSceneFind(conn->SCU);
	}
	if (!scene)
	{
		return 0;
	}

	char* key = NULL;
	char* value = NULL;
	char* scid = NULL;
	int nArguments = ndConnectionParseArguments(conn);

	for (int i = 4; i < nArguments; i++)
	{
		if (!strcmp(ndArguments[i], "SCID") && i < nArguments - 1)
		{
			scid = ndArguments[++i];
		}
		else if (!strcmp(ndArguments[i], "CHID") && i < nArguments - 1)
		{
			++i;
		}
		else if (i < nArguments - 1)
		{
			key = ndArguments[i];
			value = ndArguments[++i];
		}
	}

	if (scid == NULL)
	{
		LOG_ERROR(("%s: Missing SCID in RQ SET.\n", function));
		return 0;
	}

	if (strcmp(scid, scene->id))
	{
		LOG_ERROR(("%s: Bad SCID '%s' in RQ SET.\n", function, scid));
		return 0;
	}

	if (!key)
	{
		LOG_ERROR(("%s: Missing key in RQ SET.\n", function));
		return 0;
	}

	if (!*key)
	{
		LOG_ERROR(("%s: Empty key in RQ SET.\n", function));
		return 0;
	}

	if (!value)
	{
		LOG_ERROR(("%s: Missing value in RQ SET.\n", function));
		return 0;
	}

	ndArguments[0] = "AN";
	ndArguments[3] = "OK";

	int rc = ndConnectionSendArguments(conn, ndArguments, 4);
	if (rc < 0)
	{
		return rc;
	}

	ndArguments[0] = "RQ";
	ndArguments[3] = "SET";
	ndArguments[4] = "SCID";
	ndArguments[5] = scid;
	ndArguments[6] = key;
	ndArguments[7] = value;

	PblIterator iterator;
	if (pblIteratorInit(scene->connectionSet, &iterator))
	{
		LOG_ERROR(("%s: failed to initialize iterator for connection set, pbl_errno %d.\n",
			function, pbl_errno));
		return -1;
	}
	char* ptr;
	while ((ptr = pblIteratorNext(&iterator)) != (void*)-1)
	{
		int socket = (int)(ptr - (char*)1);
		conn = ndConnectionMapFind(socket);
		if (conn)
		{
			ndConnectionUpdateRequestId(conn);
			ndArguments[1] = conn->requestId;
			if (!ndArguments[1])
			{
				ndArguments[1] = "42";
			}
			ndArguments[2] = conn->id;
			rc = ndConnectionSendArguments(conn, ndArguments, 8);
			if (rc < 0)
			{
				return rc;
			}
		}
	}
	return 0;
}

/*
 * Handle a BYE request, a client is leaving.
 *
 * rc = 0: success
 * rc < 0: error
 */
static int ndHandleBye(NdConnection* conn)
{
	NdScene* scene = NULL;

	if (conn->SCU)
	{
		scene = ndSceneFind(conn->SCU);
	}
	if (!scene)
	{
		return 0;
	}

	char* clid = NULL;
	int nArguments = ndConnectionParseArguments(conn);

	for (int i = 4; i < nArguments; i++)
	{
		if (!strcmp(ndArguments[i], "CLID") && i < nArguments - 1)
		{
			clid = ndArguments[++i];
		}
	}

	if (clid == NULL || strcmp(clid, conn->clientId))
	{
		return 0;
	}

	ndArguments[0] = "AN";
	int rc = ndConnectionSendArguments(conn, ndArguments, 4);
	PBL_PROCESS_FREE(conn->SCU);
	PBL_PROCESS_FREE(conn->forwardInetAddr);
	return rc;
}

/*
 * Handle a ENTER request, a client is entering.
 *
 * rc = 0: success
 * rc < 0: error
 */
static int ndHandleEnter(NdConnection* conn)
{
	static char* function = "ndHandleEnter";

	if (conn->SCU)
	{
		return 0;
	}

	PBL_PROCESS_FREE(conn->NNM);
	PBL_PROCESS_FREE(conn->SCU);
	PBL_PROCESS_FREE(conn->SCN);

	int nArguments = ndConnectionParseArguments(conn);
	for (int i = 4; i < nArguments; i++)
	{
		if (!strcmp(ndArguments[i], "NNM") && i < nArguments - 1)
		{
			conn->NNM = pblProcessStrdup(function, ndArguments[++i]);
		}
		else if (!strcmp(ndArguments[i], "SCU") && i < nArguments - 1)
		{
			conn->SCU = pblProcessStrdup(function, ndArguments[++i]);
		}
		else if (!strcmp(ndArguments[i], "SCN") && i < nArguments - 1)
		{
			conn->SCN = pblProcessStrdup(function, ndArguments[++i]);
		}
	}

	if (!conn->NNM || !*conn->NNM)
	{
		LOG_ERROR(("%s: NNM missing in RQ ENTER.\n", function));
		return -1;
	}

	char c = *conn->NNM;
	if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
	{
		LOG_ERROR(("%s: NNM '%s' does not start with a letter in RQ ENTER.\n", function, conn->NNM));
		return -1;
	}

	if (!conn->SCN || !*conn->SCN)
	{
		LOG_ERROR(("%s: SCN missing in RQ ENTER.\n", function));
		return -1;
	}

	c = *conn->SCN;
	if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
	{
		LOG_ERROR(("%s: SCN '%s' does not start with a letter in RQ ENTER.\n", function, conn->SCN));
		return -1;
	}

	if (!conn->SCU || !*conn->SCU)
	{
		LOG_ERROR(("%s: SCU missing in RQ ENTER.\n", function));
		return -1;
	}

	c = *conn->SCU;
	if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
	{
		LOG_ERROR(("%s: SCU '%s' does not start with a letter in RQ ENTER.\n", function, conn->SCU));
		return -1;
	}

	pbl_LongToHexString((unsigned char*)conn->clientId, pblRand());
	LOG_INFO(("L NEW CONN ID %s CLID %s\n", conn->id, conn->clientId));

	NdScene* scene = ndSceneFind(conn->SCU);
	if (!scene)
	{
		scene = tcpSceneCreate(conn);
		if (!scene)
		{
			return -1;
		}
		LOG_INFO(("L NEW SCEN ID %s SCU %s SCN %s\n", scene->id, scene->sceneUrl, scene->sceneName));
	}
	else
	{
		char* key = (char*)1 + conn->tcpSocket;
		if (pblSetAdd(scene->connectionSet, key) < 0)
		{
			LOG_ERROR(("%s: could not add connection to scene, pbl_errno %d.\n",
				function, pbl_errno));
			tcpSceneClose(scene);
			return -1;
		}
	}
	ndArguments[0] = "AN";
	ndArguments[2] = conn->id;
	ndArguments[3] = "HI";
	ndArguments[4] = "CLID";
	ndArguments[5] = conn->clientId;
	ndArguments[6] = "SCID";
	ndArguments[7] = scene->id;
	ndArguments[8] = "NNM";
	ndArguments[9] = conn->NNM;

	return ndConnectionSendArguments(conn, ndArguments, 10);
}

/*
 * Handle a request.
 *
 * rc = 0: success
 * rc < 0: error
 */
int ndRequestHandle(NdConnection* conn)
{
	unsigned int nArguments = ndConnectionParseArguments(conn);

	if (nArguments < 4)
	{
		return -1;
	}

	if (strcmp("RQ", ndArguments[0]))
	{
		return -1;
	}

	char* packetId = ndArguments[1];
	if (!*packetId)
	{
		return -1;
	}

	char* connectionId = ndArguments[2];
	if (!*connectionId)
	{
		return -1;
	}

	char* tag = ndArguments[3];
	if (!*tag)
	{
		return -1;
	}

	if (!strcmp("SET", tag))
	{
		return ndHandleSet(conn);
	}
	if (!strcmp("ENTER", tag))
	{
		return ndHandleEnter(conn);
	}
	if (!strcmp("PING", tag))
	{
		ndArguments[0] = "AN";
		ndArguments[3] = "PONG";

		return ndConnectionSendArguments(conn, ndArguments, 4);
	}
	if (!strcmp("BYE", tag))
	{
		return ndHandleBye(conn);
	}
	return 0;
}