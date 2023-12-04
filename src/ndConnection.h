/*
 * ndConnection.h - Manage the TCP connections.
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
#ifndef _TCP_CONNECTION_H_
#define _TCP_CONNECTION_H_

#ifdef __cplusplus
extern "C" {
#endif

#define ND_DATA_OFFSET 10
#define ND_ID_LENGTH 8
#define ND_RECEIVE_BUFFER_LENGTH (8 * 1024)

	typedef struct NdConnection_s
	{
		/* infrastructure */
		int  tcpSocket;
		char id[ND_ID_LENGTH + 1];
		char clientId[ND_ID_LENGTH + 1];
		char requestId[ND_ID_LENGTH + 1];

		/* connection attributes */
		int protocolNumber;
		int requestCode;

		/* client attributes */
		unsigned int clientIp;
		unsigned short clientPort;
		char* clientInetAddr;

		/* client values */
		char* NNM;
		char* SCN;
		char* SCU;

		/* forward attributes, only when forwarding is active */
		unsigned int forwardIp;
		unsigned short forwardPort;
		char* forwardInetAddr;

		/* keep alive */
		time_t startTime;
		time_t lastReceiveTime;
		time_t lastSendTime;

		/* attributes for non-blocking reading */
		char receiveBuffer[ND_RECEIVE_BUFFER_LENGTH];
		int packetLength;
		int bytesRead;
		int bytesExpected;

		/* attributes for non-blocking writing */
		char* sendBuffer;
		int sendBufferLength;
		int sendBufferStart;

		/* attributes for statistics */
		unsigned long packetsReceived;
		unsigned long bytesReceived;
		unsigned long packetsSent;
		unsigned long bytesSent;

	} NdConnection;

	extern int ndConnectionsAdded;
	extern int ndConnectionsRemoved;

	extern NdConnection* ndConnectionCreate(int listenSocket);
	extern NdConnection* ndConnectionMapFind(int socket);
	extern int ndConnectionMapAdd(NdConnection* conn);
	extern int ndConnectionMapRemove(int socket);
	extern int ndConnectionMapNofConnections();
	extern void ndConnectionInit();
	extern void ndConnectionExit();
	extern void ndConnectionClose(NdConnection* conn);
	extern void ndConnectionCheckIdleConnections();
	extern void ndConnectionUpdateRequestId(NdConnection* conn);
	extern int ndConnectionPrepareSocketMask(fd_set* rdmask);
	extern int ndConnectionPrepareWriteSocketMask(fd_set* wrmask);
	extern int ndConnectionSend(NdConnection* conn, char* buf, int size);
	extern int ndConnectionSendArguments(NdConnection* conn, char** arguments, unsigned int nArguments);
	extern int ndConnectionRead(NdConnection* conn, char* buf, int size);
	extern int ndConnectionReadPacket(NdConnection* conn);
	extern unsigned int ndConnectionParseArguments(NdConnection* conn);

#ifdef __cplusplus
}
#endif
#endif
