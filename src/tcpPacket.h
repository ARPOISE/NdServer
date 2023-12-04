/*
 * tcpPacket.h - Include file for TCP functions of the ARpoise net distribution server.
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
#ifndef _TCP_PACKET_H_
#define _TCP_PACKET_H_

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_ERR_SOCKET       -1001   /* socket() call failed               */
#define TCP_ERR_BIND         -1002   /* bind() call failed                 */
#define TCP_ERR_CONNECTION   -1003   /* Invalid socket given as parameter  */
#define TCP_ERR_EINTR        -1004   /* function interrupted by a signal   */
#define TCP_ERR_TIMEOUT      -1005   /* Timeout waiting with select        */
#define TCP_ERR_RECV         -1006   /* Network read error in receive call */
#define TCP_ERR_LISTEN       -1007   /* listen() call failed               */
#define TCP_ERR_ACCEPT       -1008   /* accept() call failed               */
#define TCP_ERR_EWOULDBLOCK  -1009   /* operation (send,recv) would block  */

#ifdef _WIN32

#define TCP_SLEEP(n)		Sleep(1000 * (n))
#define TCP_FD_CLR(sock, mask) FD_CLR((SOCKET)sock, mask)
#define TCP_ERRNO			WSAGetLastError()
#define TCP_EINTR			WSAEINTR
#define TCP_EWOULDBLOCK		WSAEWOULDBLOCK
#define TCP_ECONNABORTED	WSAECONNABORTED
#define TCP_ECONNRESET		WSAECONNRESET
#define TCP_ESHUTDOWN 		WSAESHUTDOWN

#else

#define TCP_SLEEP(n)		sleep(n)
#define TCP_FD_CLR(sock, mask) FD_CLR(sock, mask)
#define TCP_ERRNO			errno
#define TCP_EINTR			EINTR
#define TCP_EWOULDBLOCK		EWOULDBLOCK
#define TCP_ECONNABORTED	ECONNABORTED
#define TCP_ECONNRESET		ECONNRESET
#define TCP_ESHUTDOWN 		ESHUTDOWN

#endif

#define TCP_INTERVAL_SECONDS 61

	extern char* tcpPacketInetNtoa(unsigned int ip);
	extern int tcpPacketCreateListenSocket(unsigned short port, int reUse);
	extern int tcpPacketAccept(int listenSocket, unsigned int* pIp, unsigned short* pPort, char** hostname);
	extern int tcpPacketSocketSetNonBlocking(int socket, int nonBlocking);
	extern int tcpPacketSend(int socket, char* buffer, int length);
	extern int tcpPacketRead(int socket, char* buffer, int length);
	extern void tcpPacketSrvadrToIpPort(char* pSockadr, unsigned int* pIp, unsigned short* pPort);
	extern void tcpPacketExtract2Byte(unsigned short* value, char** buffer);
	extern void tcpPacketExtract4Byte(unsigned int* value, char** buffer);
	extern void tcpPacketAppend2Byte(unsigned short value, char** buffer);
	extern void tcpPacketAppend4Byte(unsigned int value, char** buffer);
	extern void tcpPacketCloseSocket(int socket);
	extern void tcpPacketSentStatistics(int nBytes);
	extern void tcpPacketReadStatistics(int nBytes);
	extern void tcpPacketWriteStatistics();

#ifdef __cplusplus
}
#endif
#endif
