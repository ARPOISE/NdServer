/*
 * ndDispatch.c - Listen to the server listen socket for new incoming connections.
 *                Listen to all TCP sockets and handles the data.
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
#include <sys/types.h>
#if !defined( _WIN32 )
#include <sys/socket.h>
#endif

#include "pblProcess.h"
#include "ndServer.h"
#include "tcpPacket.h"
#include "ndConnection.h"

#define ND_PERIODIC_SECONDS                 60 

static int _ListenSocket = -1;

/*
 * Dispatch packets received.
 *
 * rc = 0:   Success.
 * rc < 0:   Connection has been closed.
 */
static int ndDispatchPacket(NdConnection* conn)
{
	static char* function = "ndDispatchPacket";
	int retCode = -1;

	/*
	 * Read an entire packet
	 */
	int rc = ndConnectionReadPacket(conn);
	if (rc <= 0)
	{
		return rc;
	}

	/*
	 * A packet was read, extract the data
	 */
	if (conn->packetLength <= ND_DATA_OFFSET)
	{
		LOG_ERROR(("%d %s:%d not enough TCP data %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, conn->packetLength));
		ndConnectionClose(conn);
		return -1;
	}
	char* ptr = conn->receiveBuffer + sizeof(short);

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

	tcpPacketExtract4Byte(&(conn->forwardIp), &ptr);
	tcpPacketExtract2Byte(&(conn->forwardPort), &ptr);
	if (!conn->forwardInetAddr)
	{
		conn->forwardInetAddr = pblProcessStrdup(function, tcpPacketInetNtoa(htonl(conn->forwardIp)));
		if (!conn->forwardInetAddr)
		{
			LOG_ERROR(("%s: could not create forward internet address, out of memory, pbl_errno %d.\n",
				function, pbl_errno));

			ndConnectionClose(conn);
			return -1;
		}
		LOG_TRACE(("%d %s:%d forward internet address %s:%d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, conn->forwardInetAddr, conn->forwardPort));
	}

	int dataLength = conn->packetLength - ND_DATA_OFFSET;
	if (dataLength <= 3) // ARpoise always sends RQ\0 or AN\0
	{
		LOG_ERROR(("%d %s:%d not enough data %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, dataLength));
		ndConnectionClose(conn);
		return -1;
	}

	LOG_TRACE(("%d %s:%d %d bytes\n",
		conn->tcpSocket, conn->clientInetAddr, conn->clientPort, dataLength));

	int byte1 = ptr[0];
	int byte2 = ptr[1];
	int byte3 = ptr[2];
	if (byte3 != '\0')
	{
		LOG_ERROR(("%d %s:%d bad third byte %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, byte3));
		ndConnectionClose(conn);
		return -1;
	}

	if (byte1 == 'R' && byte2 == 'Q')
	{
		LOG_INFO(("< %s:%d %d ", conn->clientInetAddr, conn->clientPort, conn->packetLength));
		for (int i = ND_DATA_OFFSET; i < conn->packetLength; i++)
		{
			char c = conn->receiveBuffer[i];
			LOG_CHAR((c < ' ' ? ' ' : c));
		}
		LOG_CHAR(('\n'));

		if (ndRequestHandle(conn) < 0)
		{
			ndConnectionClose(conn);
			return -1;
		}
	}
	else if (byte1 == 'A' && byte2 == 'N')
	{
		LOG_INFO(("< %s:%d %d ", conn->clientInetAddr, conn->clientPort, conn->packetLength));
		for (int i = ND_DATA_OFFSET; i < conn->packetLength; i++)
		{
			char c = conn->receiveBuffer[i];
			LOG_CHAR((c < ' ' ? ' ' : c));
		}
		LOG_CHAR(('\n'));
	}
	else
	{
		LOG_ERROR(("%d %s:%d bad first two bytes %d %d\n",
			conn->tcpSocket, conn->clientInetAddr, conn->clientPort, byte1, byte2));
		ndConnectionClose(conn);
		return -1;
	}
	return retCode;
}

/*
 * Initialize the Dispatcher.
 */
void ndDispatchInit()
{
#if defined( _WIN32 ) || defined( CP_ONE_PROCESS )
	_ListenSocket = -1;
#endif

	ndConnectionInit();        /* initialize Connection Manager */
}

/*
 * Close the listen socket.
 */
void ndDispatchExit()
{
	/* Close all open connections */
	ndConnectionExit();

	if (_ListenSocket != -1)
	{
		LOG_INFO(("S %d listening socket closed\n", _ListenSocket));
		tcpPacketCloseSocket(_ListenSocket);
		_ListenSocket = -1;
	}
}

/*
 * Create the listen socket.
 *
 * int rc >= 0: Socket fd of successfully created listen socket.
 * int rc < 0: An error occured.
 */
int ndDispatchCreateListenSocket()
{
	_ListenSocket = tcpPacketCreateListenSocket(pblProcess.port, TRUE);
	if (_ListenSocket < 0)
	{
		LOG_ERROR(("Cannot create listen socket on TCP port %d, rc %d, errno %d, going down!\n",
			pblProcess.port, _ListenSocket, TCP_ERRNO));
		_ListenSocket = -1;
		return -1;
	}

	LOG_TRACE(("S %d listening socket\n", _ListenSocket));
	return _ListenSocket;
}

/*
 * This is the main loop, it waits for incoming connection calls and handles TCP packets.
 */
void ndDispatchLoop()
{
	static char* function = "ndDispatchLoop";
	fd_set writeMask = { 0 };
	struct timeval timeout = { 0 };
	time_t lastPeriodicTime = time(NULL);
	time_t now;

	struct timeval tvNow = { 0 };

	NdConnection* conn = NULL;

	while (pblProcess.doWork)
	{
		gettimeofday(&tvNow, (struct timezone*)NULL);
		now = time(NULL);

		if ((now - lastPeriodicTime) >= ND_PERIODIC_SECONDS)
		{
			lastPeriodicTime = now;

			int n = ndConnectionMapNofConnections();
			LOG_INFO(("C %d A %lu D %lu T %lu S %lu\n",
				n, ndConnectionsAdded, ndConnectionsRemoved, ndConnectionsTotal, ndScenesTotal));

			if (n > 0 || ndConnectionsAdded > 0 || ndConnectionsRemoved > 0)
			{
				ndConnectionsAdded = 0;
				ndConnectionsRemoved = 0;
				tcpPacketWriteStatistics();
			}
			ndConnectionCheckIdleConnections();
		}

		fd_set readMask = { 0 };
		FD_ZERO(&readMask);
		int maxSocket;
		int maxReadSocket = maxSocket = ndConnectionPrepareSocketMask(&readMask);

		FD_SET(_ListenSocket, &readMask);
		if (_ListenSocket > maxSocket)
		{
			maxSocket = _ListenSocket;
		}

		fd_set* writeMaskPtr = &writeMask;
		int maxWriteSocket = ndConnectionPrepareWriteSocketMask(writeMaskPtr);
		if (maxWriteSocket < 0)
		{
			writeMaskPtr = NULL;
		}
		else if (maxWriteSocket > maxSocket)
		{
			maxSocket = maxWriteSocket;
		}

		/*
		 * Wait for 100 milliseconds for incoming packets or new connections
		 */
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;
		errno = 0;
		int nSockets = select(maxSocket + 1, &readMask, writeMaskPtr, (fd_set*)NULL, &timeout);
		if (!pblProcess.doWork)
		{
			break;
		}
		if (nSockets == 0)
		{
			tcpPacketReadStatistics(-1);
			tcpPacketSentStatistics(-1);
			continue;
		}
#ifdef _WIN32
		else if (nSockets == SOCKET_ERROR)
#else
		else if (nSockets < 0)
#endif
		{
			if (TCP_ERRNO == TCP_EINTR)
			{
				continue;
			}
			LOG_ERROR(("%s: select failed, max %d, rc %d, errno %d\n",
				function, maxSocket, nSockets, TCP_ERRNO));
			break;
		}

		/*
		 * Check listen socket for new connections
		 */
		if (FD_ISSET(_ListenSocket, &readMask))
		{
			--nSockets;
			conn = ndConnectionCreate(_ListenSocket);
			if (!conn)
			{
				continue;
			}
			LOG_INFO(("S %d %s:%d, N %d\n",
				conn->tcpSocket, conn->clientInetAddr,
				conn->clientPort, ndConnectionMapNofConnections()));
		}

		if (writeMaskPtr)
		{
			for (int socket = 0; nSockets > 0 && socket <= maxWriteSocket; socket++)
			{
				if (FD_ISSET(socket, writeMaskPtr))
				{
					--nSockets;
					conn = ndConnectionMapFind(socket);
					if (!conn)
					{
#if defined( _WIN32 )
						break;
#else
						LOG_ERROR(("%s: select write event on unknown socket %d, errno %d\n",
							function, socket, TCP_ERRNO));
						pblProcess.doWork = FALSE;
						break;
#endif
					}
					if (ndConnectionSend(conn, NULL, 0) < 0)
					{
						ndConnectionClose(conn);
						/*
						 * Check for new events since connections has been closed
						 */
						nSockets = 0;
						break;
					}
				}
			}
		}

		for (int socket = 0; nSockets > 0 && socket <= maxReadSocket; socket++)
		{
			if (_ListenSocket == socket)
			{
				continue;
			}
			if (FD_ISSET(socket, &readMask))
			{
				--nSockets;
				conn = ndConnectionMapFind(socket);
				if (!conn)
				{
#if defined( _WIN32 )
					break;
#else
					LOG_ERROR(("%s: select read event on unknown socket %d, errno %d\n",
						function, socket, TCP_ERRNO));
					pblProcess.doWork = FALSE;
					break;
#endif
				}
				conn->lastReceiveTime = time(NULL);
				if (ndDispatchPacket(conn) < 0)
				{
					/*
					 * Check for new events since connections may have been closed
					 */
					break;
				}
			}
		}
	}
}
