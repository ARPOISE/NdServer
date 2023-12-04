/*
 * ndConnectionMap.c - Manage the connection map.
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
#include "pbl.h"
#include "ndConnection.h"

PblMap* ndConnectionMap = NULL;
int ndConnectionsAdded = 0;
int ndConnectionsRemoved = 0;

/*
 * Return the number of open connections.
 */
int ndConnectionMapNofConnections()
{
	return ndConnectionMap ? pblMapSize(ndConnectionMap) : 0;
}

/*
 * Find a connection for a given socket.
 *
 * Returns NULL if no connection is found.
 */
NdConnection* ndConnectionMapFind(int socket)
{
	if (ndConnectionMap)
	{
		char* key = (char*)1 + socket;
		NdConnection** connPtr = (NdConnection**)pblMapGet(ndConnectionMap, &key, sizeof(void*), NULL);
		return connPtr ? *connPtr : NULL;
	}
	return NULL;
}

/*
 * Iterate to next connection.
 *
 * Returns NULL if no more connection is found.
 */
NdConnection* ndConnectionMapNext(PblIterator* iterator)
{
	void* iterated;
	if ((iterated = pblIteratorNext(iterator)) != (void*)-1)
	{
		NdConnection** connPtr = (NdConnection**)pblMapEntryValue(iterated);
		return connPtr ? *connPtr : NULL;
	}
	return NULL;
}

/*
 * Add a connection to the map.
 *
 * int rc = 0: Connection successfully added.
 * int rc < 0: Cannot insert entry into the map.
 */
int ndConnectionMapAdd(NdConnection* conn)
{
	static char* function = "ndConnectionMapAdd";

	if (!ndConnectionMap)
	{
		ndConnectionMap = pblMapNewHashMap();
		if (!ndConnectionMap)
		{
			LOG_ERROR(("%s: could not create connection map, out of memory, pbl_errno %d.\n",
				function, pbl_errno));
			return -1;
		}
	}

	char* key = (char*)1 + conn->tcpSocket;
	void* result = pblMapPut(ndConnectionMap, &key, sizeof(void*), &conn, sizeof(NdConnection*), NULL);
	if (result == (void*)-1)
	{
		LOG_ERROR(("%s: failed to insert connection for socket %d into the map, pbl_errno %d.\n",
			function, conn->tcpSocket, pbl_errno));
		return -1;
	}
	else if (result)
	{
		LOG_INFO(("%s: connection for socket %d already existed in map.\n",
			function, conn->tcpSocket));
		ndConnectionClose(result);
	}
	ndConnectionsAdded++;
	return 0;
}

/*
 * Remove a connection from the map.
 *
 * int rc = 0: Connection successfully removed.
 * int rc < 0: Cannot remove entry from the map.
 */
int ndConnectionMapRemove(int socket)
{
	if (socket < 0 || !ndConnectionMap)
	{
		return 0;
	}
	NdConnection* conn = ndConnectionMapFind(socket);
	if (!conn)
	{
		return 0;
	}

	char* key = (char*)1 + conn->tcpSocket;
	void* result = pblMapRemove(ndConnectionMap, &key, sizeof(void*), NULL);
	if (result == (void*)-1)
	{
		return -1;
	}
	ndConnectionsRemoved++;
	return 0;
}
