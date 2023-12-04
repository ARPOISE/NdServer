/*
 * ndConnection.c - Manage the connections.
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

#define ND_TIMEOUT_SECONDS (3 * 60)

extern PblMap* ndConnectionMap;
extern NdConnection* ndConnectionMapNext(PblIterator* iterator);

unsigned long ndConnectionsTotal = 0;
unsigned long ndConnectionsAdded = 0;

static fd_set _CurrentMask;
static int _MaxSocket;
static int _NofArguments = 0;
static char* _Arguments[ND_RECEIVE_BUFFER_LENGTH + 1];
static char _SendBuffer[ND_RECEIVE_BUFFER_LENGTH + 1];

char** ndArguments = _Arguments;

/*
 * Parse a packet into an array of char* variables.
 */
unsigned int ndConnectionParseArguments(NdConnection* conn)
{
	int n = 0;
	int start = ND_DATA_OFFSET;

	// Parse the entire buffer received
	//
	for (int offset = start; offset < conn->packetLength; offset++)
	{
		// A '\0' byte terminates the argument string
		//
		if (!conn->receiveBuffer[offset])
		{
			if (offset == start)
			{
				ndArguments[n++] = "";
			}
			else
			{
				ndArguments[n++] = conn->receiveBuffer + start;
			}
			offset = start = offset + 1;
		}
	}
	return _NofArguments = n;
}

/*
 * Send some bytes on a TCP socket.
 *
 * If a packet cannot be sent completely, it is buffered.
 * If there is already some buffered data that cannot be sent,
 * the entire new packet is dropped.
 *
 * rc = 0: ok, the packet was handled
 * rc < 0: there was an error. The connection has been closed.
 */
int ndConnectionSend(NdConnection* conn, char* buffer, int size)
{
	int rc;
	int length;

	if (conn->tcpSocket < 0)
	{
		return 0;
	}

	/*
	 * If there are some bytes buffered for this connection
	 */
	if (conn->sendBuffer && (length = conn->sendBufferLength - conn->sendBufferStart))
	{
		rc = tcpPacketSend(conn->tcpSocket, conn->sendBuffer + conn->sendBufferStart, length);
		LOG_TRACE(("%d %s:%d sent %d, rc %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, length, rc));

		if (rc > 0)
		{
			conn->lastSendTime = time(NULL);
			conn->bytesSent += rc;
		}

		if (rc == length)
		{
			PBL_PROCESS_FREE(conn->sendBuffer);
			conn->sendBufferLength = 0;
			conn->sendBufferStart = 0;

			conn->packetsSent++;
			tcpPacketSentStatistics(rc);
			return 0;
		}
		else if (rc >= 0)
		{
			conn->sendBufferStart += rc;

			/*
			 * Because the buffer is not empty,
			 * we drop the packet we'd have to send now
			 */
			return 0;
		}
		else
		{
			switch (rc)
			{
			case TCP_ERR_EWOULDBLOCK:
			case TCP_EWOULDBLOCK:
				LOG_TRACE(("%d %s TCP send would block\n", conn->tcpSocket, conn->clientInetAddr));
				return 0;

			case TCP_ERR_EINTR:
			case TCP_EINTR:
				return 0;

			default:
				LOG_ERROR(("%d %s:%d TCP send failed %d, errno %d\n",
					conn->tcpSocket, conn->clientInetAddr, conn->clientPort, rc, TCP_ERRNO));
				return rc;
			}
		}
		return rc;
	}

	if (size < 1 || !buffer)
	{
		return 0;
	}

	rc = tcpPacketSend(conn->tcpSocket, buffer, size);
	LOG_TRACE(("%d %s:%d sent %d, rc %d\n",
		conn->tcpSocket, conn->clientInetAddr, conn->clientPort, size, rc));

	if (rc > 0)
	{
		conn->lastSendTime = time(NULL);
		conn->bytesSent += rc;
	}

	if (rc == size)
	{
		/*
		 * All bytes sent
		 */
		conn->packetsSent++;
		tcpPacketSentStatistics(rc);
		return 0;
	}
	else if (rc >= 0)
	{
		/*
		 * Buffer the bytes that were not sent
		 */
		conn->sendBuffer = pblProcessMemdup(NULL, buffer + rc, size - rc);
		if (conn->sendBuffer)
		{
			conn->sendBufferStart = 0;
			conn->sendBufferLength = size - rc;
			LOG_TRACE(("%d %s:%d buffered %d bytes,\n",
				conn->tcpSocket, conn->clientInetAddr, conn->clientPort, conn->sendBufferLength));
		}
		return 0;
	}

	switch (rc)
	{
	case TCP_ERR_EWOULDBLOCK:
	case TCP_EWOULDBLOCK:
		LOG_TRACE(("%d %s:%d TCP send would block\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort));
		return 0;

	case TCP_ERR_EINTR:
	case TCP_EINTR:
		return 0;

	default:
		LOG_ERROR(("%d %s:%d TCP send failed %d, errno %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, rc, TCP_ERRNO));
		return rc;
	}
	return rc;
}

/*
 * Send N arguments as one packet
 */
int ndConnectionSendArguments(NdConnection* conn, char** arguments, unsigned int nArguments)
{
	static char* function = "ndConnectionSendArguments";

	char* ptr = _SendBuffer;
	ptr += sizeof(short);
	*ptr++ = 1; // protocol number
	*ptr++ = 10; // request code
	tcpPacketAppend4Byte(conn->forwardIp, &ptr);
	tcpPacketAppend2Byte(conn->forwardPort, &ptr);

	for (unsigned int i = 0; i < nArguments; i++)
	{
		size_t length = arguments[i] ? 1 + strlen(arguments[i]) : 1;
		if (ptr - _SendBuffer + length < sizeof(_SendBuffer) - 1)
		{
			if (arguments[i])
			{
				memcpy(ptr, arguments[i], length);
				ptr += length;
			}
			else
			{
				*ptr++ = '\0';
			}
		}
		else
		{
			LOG_ERROR(("%s: %d %s:%d TCP send buffer overflow %d\n",
				function, conn->tcpSocket, conn->clientInetAddr, conn->clientPort, ptr - _SendBuffer + length));
			return -1;
		}
	}
	short length = (short)(ptr - _SendBuffer);

	int outputLength = length;
	if (outputLength > 64 + ND_DATA_OFFSET)
	{
		outputLength = 64 + ND_DATA_OFFSET;
	}

	LOG_INFO(("> %s:%d %d ", conn->clientInetAddr, conn->clientPort, length));
	for (int i = ND_DATA_OFFSET; i < outputLength; i++)
	{
		char c = _SendBuffer[i];
		LOG_CHAR((c < ' ' ? ' ' : c));
	}
	LOG_CHAR(('\n'));

	ptr = _SendBuffer;
	tcpPacketAppend2Byte(length - 2, &ptr);
	return ndConnectionSend(conn, _SendBuffer, length);
}

/*
 * Receive some bytes on a TCP socket.
 *
 * rc > 0: number of bytes received
 * rc = 0: nothing read. Function returned with EINTR or EWOULDBLOCK -> retry
 * rc < 0: there was an error. Connection has been closed.
 */
int ndConnectionRead(NdConnection* conn, char* buf, int size)
{
	if (conn->tcpSocket < 0)
	{
		return 0;
	}

	int rc = tcpPacketRead(conn->tcpSocket, buf, size);
	if (rc < 0)
	{
		if ((rc == TCP_ERR_EINTR) || (rc == TCP_ERR_EWOULDBLOCK))
		{
			return 0;
		}

		LOG_ERROR(("%d %s:%d TCP receive failed %d, errno %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, rc, TCP_ERRNO));

		ndConnectionClose(conn);
	}
	else if (rc == 0)
	{
		LOG_TRACE(("%d %s:%d closed by foreign host, now %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, ndConnectionMapNofConnections()));

		ndConnectionClose(conn);
		return -1;
	}
	else
	{
		conn->bytesRead += rc;
		conn->bytesReceived += rc;
	}
	return rc;
}

/*
 * Receive one packet on a TCP socket.
 *
 * rc > 0: number of bytes received
 * rc = 0: nothing read. Function returned with EINTR or EWOULDBLOCK -> retry
 * rc < 0: there was an error. Connection has been closed.
 */
int ndConnectionReadPacket(NdConnection* conn)
{
	int bytesMissing = 0;
	unsigned short packetLengthAsShort;
	conn->packetLength = 0;

	if (conn->bytesExpected)
	{
		bytesMissing = conn->bytesExpected - conn->bytesRead;
	}
	else
	{
		bytesMissing = 4 - conn->bytesRead;
	}

	if (bytesMissing < 0)
	{
		LOG_ERROR(("%d %s:%d missing bytes is negative %d, bytes read %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, bytesMissing, conn->bytesRead));
		ndConnectionClose(conn);
		return -1;
	}

	if (conn->bytesRead + bytesMissing >= (int)sizeof(conn->receiveBuffer) - 1)
	{
		LOG_ERROR(("%d %s:%d bytes read plus missing bytes too large %d, bytes read %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, conn->bytesRead + bytesMissing, conn->bytesRead));
		ndConnectionClose(conn);
		return -1;
	}

	int rc = ndConnectionRead(conn, conn->receiveBuffer + conn->bytesRead, bytesMissing);
	if (rc <= 0)
	{
		return rc;
	}

	if (!conn->bytesExpected)
	{
		if (conn->bytesRead < 4)
		{
			/*
			 * We don't even have the length field, wait for more data
			 */
			return 0;
		}

		char* ptr = conn->receiveBuffer;
		tcpPacketExtract2Byte(&packetLengthAsShort, &ptr);

		// ARpoise always sends the protocol number followed by 10
		//
		conn->protocolNumber = *ptr++;
		if (conn->protocolNumber != 1)
		{
			LOG_ERROR(("%d %s:%d bad protocol number %d\n",
				conn->tcpSocket, conn->clientInetAddr, conn->clientPort, conn->protocolNumber));
			ndConnectionClose(conn);
			return -1;
		}
		conn->requestCode = *ptr++;
		if (conn->requestCode != 10)
		{
			LOG_ERROR(("%d %s:%d bad request code %d\n",
				conn->tcpSocket, conn->clientInetAddr, conn->clientPort, conn->requestCode));
			ndConnectionClose(conn);
			return -1;
		}

		/*
		 * Try to read the complete packet
		 */
		conn->bytesExpected = 2 + packetLengthAsShort;
		if (conn->bytesExpected >= (int)sizeof(conn->receiveBuffer) - 1)
		{
			LOG_ERROR(("%d %s:%d packet too large %d, bytes read %d\n",
				conn->tcpSocket, conn->clientInetAddr, conn->clientPort, conn->bytesExpected, conn->bytesRead));
			ndConnectionClose(conn);
			return -1;
		}
		if (conn->bytesExpected < 0)
		{
			LOG_ERROR(("%d %s:%d expected bytes is negative %d, bytes read %d\n",
				conn->tcpSocket, conn->clientInetAddr, conn->clientPort, bytesMissing, conn->bytesRead));
			ndConnectionClose(conn);
			return -1;
		}

		bytesMissing = conn->bytesExpected - conn->bytesRead;
		if (bytesMissing < 0)
		{
			LOG_ERROR(("%d %s:%d missing bytes is negative %d, bytes read %d\n",
				conn->tcpSocket, conn->clientInetAddr, conn->clientPort, bytesMissing, conn->bytesRead));
			ndConnectionClose(conn);
			return -1;
		}

		rc = ndConnectionRead(conn, conn->receiveBuffer + conn->bytesRead, bytesMissing);
		if (rc <= 0)
		{
			return rc;
		}
	}

	/*
	 * If not all bytes are read
	 */
	if (conn->bytesRead < conn->bytesExpected)
	{
		return 0;
	}

	conn->packetsReceived++;
	conn->receiveBuffer[conn->bytesRead] = 0;
	tcpPacketReadStatistics(conn->packetLength = conn->bytesRead);

	/*
	 * Reset the buffer structure
	 */
	conn->bytesRead = 0;
	conn->bytesExpected = 0;

	return conn->packetLength;
}

/*
 * Initializes the connection manager.
 */
void ndConnectionInit()
{
	FD_ZERO(&_CurrentMask);
	_MaxSocket = 0;
}

/*
 * Close the connection. Do not use the connection pointer afterwards!
 */
void ndConnectionClose(NdConnection* conn)
{
	static char* function = "ndConnectionClose";
	int doRecalc = FALSE;

	char* hostnameForLog = pblProcessStrdup(function, conn->clientInetAddr);
	int clientPort = conn->clientPort;

	int tcpSocket = -1;
	unsigned long packetsReceived;
	unsigned long bytesReceived;
	unsigned long packetsSent;
	unsigned long bytesSent;
	time_t startTime;
	NdScene* scene = NULL;

	if (conn->tcpSocket >= 0)
	{
		char* key = NULL;
		if (conn->SCU && *conn->SCU)
		{
			scene = ndSceneFind(conn->SCU);
			if (scene)
			{
				key = (char*)1 + conn->tcpSocket;
				pblSetRemoveElement(scene->connectionSet, key);
			}
		}
		tcpSocket = conn->tcpSocket;
		packetsReceived = conn->packetsReceived;
		bytesReceived = conn->bytesReceived;
		packetsSent = conn->packetsSent;
		bytesSent = conn->bytesSent;
		startTime = conn->startTime;

		TCP_FD_CLR(tcpSocket, &_CurrentMask);
		if (tcpSocket == _MaxSocket) /* is highest descriptor ? */
		{
			doRecalc = TRUE;
		}

		ndConnectionMapRemove(tcpSocket);
		tcpPacketCloseSocket(tcpSocket);
		conn->tcpSocket = -1;
	}

	LOG_INFO(("L DEL CONN ID %s CLID %s\n",
		conn->id ? conn->id : "?",
		conn->clientId ? conn->clientId : "?"));

	PBL_PROCESS_FREE(conn->NNM);
	PBL_PROCESS_FREE(conn->SCN);
	PBL_PROCESS_FREE(conn->SCU);
	PBL_PROCESS_FREE(conn->clientInetAddr);
	PBL_PROCESS_FREE(conn->forwardInetAddr);
	PBL_PROCESS_FREE(conn->sendBuffer);
	PBL_PROCESS_FREE(conn);

	if (tcpSocket >= 0)
	{
		LOG_INFO(("S %d %s:%d D %ld PR %ld BR %ld PS %ld BS %ld, N %d\n",
			tcpSocket, hostnameForLog ? hostnameForLog : "", clientPort,
			(long)(time(NULL) - startTime),
			packetsReceived, bytesReceived, packetsSent, bytesSent,
			ndConnectionMapNofConnections()));
	}
	PBL_PROCESS_FREE(hostnameForLog);

	if (scene && ndSceneNofConnections(scene) < 1)
	{
		tcpSceneClose(scene);
	}

	if (doRecalc && ndConnectionMap)
	{
		_MaxSocket = 0;
		PblIterator iterator;
		if (pblIteratorInit(ndConnectionMap, &iterator))
		{
			LOG_ERROR(("%s: failed to initialize iterator for map, pbl_errno %d.\n",
				function, pbl_errno));
			return;
		}
		while ((conn = ndConnectionMapNext(&iterator)))
		{
			if (conn->tcpSocket > _MaxSocket)
			{
				_MaxSocket = conn->tcpSocket;
			}
		}
	}
}

/*
 * Accept a connection request on the listen socket.
 *
 * int rc != NULL: New connection successfully created
 * int rc == NULL: Cannot create connection
 */
NdConnection* ndConnectionCreate(int listenSocket)
{
	static char* function = "ndConnectionCreate";

	unsigned int clientIp = 0;
	unsigned short clientPort = 0;
	char* clientInetAddr = NULL;
	int newSocket = -1;

	if ((newSocket = tcpPacketAccept(listenSocket, &clientIp, &clientPort, &clientInetAddr)) < 0)
	{
		if (newSocket != TCP_ERR_EINTR)
		{
			LOG_ERROR(("%s: accept error on socket %d, errno %d\n",
				function, listenSocket, TCP_ERRNO));
		}
		return NULL;
	}

	NdConnection* conn = pblProcessMalloc(function, sizeof(NdConnection));
	if (!conn)
	{
		return NULL;
	}

	conn->startTime = conn->lastReceiveTime = time(NULL);
	conn->tcpSocket = newSocket;
	pbl_LongToHexString((unsigned char*)conn->id, conn->tcpSocket);
	conn->clientIp = clientIp;
	conn->clientPort = clientPort;

	conn->clientInetAddr = pblProcessStrdup(function, clientInetAddr);
	if (!conn->clientInetAddr)
	{
		LOG_ERROR(("%s: could not create client internet address, out of memory, pbl_errno %d.\n",
			function, pbl_errno));

		ndConnectionClose(conn);
		return NULL;
	}

	if (tcpPacketSocketSetNonBlocking(conn->tcpSocket, TRUE))
	{
		LOG_ERROR(("%s: failed to set socket %d to non blocking, errno %d\n",
			function, conn->tcpSocket, TCP_ERRNO));

		ndConnectionClose(conn);
		return NULL;
	}

	if (ndConnectionMapAdd(conn) < 0)
	{
		ndConnectionClose(conn);
		return NULL;
	}

	/*
	 * Add socket to read mask
	 */
	FD_SET(conn->tcpSocket, &_CurrentMask);
	if (conn->tcpSocket > _MaxSocket)
	{
		_MaxSocket = conn->tcpSocket;
	}

	ndConnectionsTotal++;
	ndConnectionsAdded++;
	return conn;
}

static unsigned int _requestId = 0x10000;
/*
 * Update the request id for a connection.
 */
void ndConnectionUpdateRequestId(NdConnection* conn)
{
	pbl_LongToHexString((unsigned char*)conn->requestId, ++_requestId);
}

/*
 * Check each TCP connection if there were no more packets since a certain time.
 * Close these connections since the client probably has crashed.
 */
void ndConnectionCheckIdleConnections()
{
	static char* function = "ndConnectionCheckIdleConnections";

	while (ndConnectionMapNofConnections() > 0)
	{
		PblIterator iterator;
		if (pblIteratorInit(ndConnectionMap, &iterator))
		{
			LOG_ERROR(("%s: failed to initialize iterator for map, pbl_errno %d.\n",
				function, pbl_errno));
			return;
		}
		int connTimeout = 0;
		time_t now = time(NULL);
		NdConnection* conn = NULL;
		while ((conn = ndConnectionMapNext(&iterator)))
		{
			if (now - conn->lastReceiveTime > ND_TIMEOUT_SECONDS / 4
				&& now - conn->lastSendTime > ND_TIMEOUT_SECONDS / 4)
			{
				ndConnectionUpdateRequestId(conn);
				char* arguments[5] = { 0 };
				arguments[0] = "RQ";
				arguments[1] = conn->requestId;
				arguments[2] = conn->id;
				arguments[3] = "PING";
				arguments[4] = NULL;
				ndConnectionSendArguments(conn, arguments, 4);
				conn->lastSendTime = time(NULL);
			}
			if (now - conn->lastReceiveTime > ND_TIMEOUT_SECONDS)
			{
				LOG_INFO(("S %d %s:%d idle timeout\n",
					conn->tcpSocket, conn->clientInetAddr, conn->clientPort));
				ndConnectionClose(conn);
				conn = NULL;
				connTimeout = 1;
				break;
			}
		}
		if (!connTimeout)
		{
			break;
		}
	}
}

/*
 * Initialize select() mask with all open socket fds.
 *
 * int rc: The highest socket.
 */
int ndConnectionPrepareSocketMask(fd_set* rdmask)
{
	memcpy(rdmask, &_CurrentMask, sizeof(fd_set));
	return _MaxSocket;
}

/*
 * Initialize select() mask with all open socket fds that want to write.
 *
 * int rc: The highest socket.
 */
int ndConnectionPrepareWriteSocketMask(fd_set* writeMask)
{
	static char* function = "ndConnectionPrepareWriteSocketMask";
	int maxWriteSocket = -1;

	FD_ZERO(writeMask);

	if (ndConnectionMap)
	{
		PblIterator iterator;
		if (pblIteratorInit(ndConnectionMap, &iterator))
		{
			LOG_ERROR(("%s: failed to initialize iterator for map, pbl_errno %d.\n",
				function, pbl_errno));
			return maxWriteSocket;
		}
		NdConnection* conn = NULL;
		while ((conn = ndConnectionMapNext(&iterator)))
		{
			if (conn->tcpSocket >= 0 && conn->sendBuffer && (conn->sendBufferLength - conn->sendBufferStart))
			{
				FD_SET(conn->tcpSocket, writeMask);

				if (conn->tcpSocket > maxWriteSocket)
				{
					maxWriteSocket = conn->tcpSocket;
				}
			}
		}
	}
	return maxWriteSocket;
}

/*
 * Close all current connections and release memory of all connection entries.
 */
void ndConnectionExit()
{
	static char* function = "ndConnectionExit";

	/* Close all running connections */
	while (ndConnectionMapNofConnections() > 0)
	{
		PblIterator iterator;
		if (pblIteratorInit(ndConnectionMap, &iterator))
		{
			LOG_ERROR(("%s: failed to initialize iterator for map, pbl_errno %d.\n",
				function, pbl_errno));
			return;
		}
		NdConnection* conn;
		if ((conn = ndConnectionMapNext(&iterator)))
		{
			ndConnectionClose(conn);
		}
		else
		{
			break;
		}
	}
	if (ndConnectionMap)
	{
		pblMapFree(ndConnectionMap);
		ndConnectionMap = NULL;
	}
	FD_ZERO(&_CurrentMask);
	_MaxSocket = 0;
}
