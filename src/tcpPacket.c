/*
 * tcpPacket.c - Functions on TCP sockets and packets.
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
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "pblProcess.h"
#include "tcpPacket.h"

typedef struct
{
	time_t          second;
	unsigned long   nPacketsReceived;
	unsigned long   nBytesReceived;
	unsigned long   nPacketsSent;
	unsigned long   nBytesSent;

} TcpPacketStatisticPerSecond;

static TcpPacketStatisticPerSecond tcpPacketStatisticsPerSecond[TCP_INTERVAL_SECONDS];

#ifdef _WIN32

#define socket_close closesocket

char tcpPacketErrBuffer[32];
#define TCP_ERRMSG (snprintf(tcpPacketErrBuffer, sizeof(tcpPacketErrBuffer) - 1,\
                    "%d", WSAGetLastError()) ? tcpPacketErrBuffer : tcpPacketErrBuffer)
#else

#define socket_close close
#define TCP_ERRMSG strerror( errno )

#endif

/*
 * Get the port and the ip from a socket address structure.
 */
void tcpPacketSrvadrToIpPort(char* pSockadr, unsigned int* pIp, unsigned short* pPort)
{
	struct sockaddr_in* addr = (struct sockaddr_in*)pSockadr;
	if (pIp)
	{
		memcpy(pIp, &addr->sin_addr.s_addr, sizeof(unsigned int));
	}
	if (pPort)
	{
		memcpy(pPort, &addr->sin_port, sizeof(unsigned short));
	}
}

/*
 * Convert the internet host address given in network byte order to a string.
 */
char* tcpPacketInetNtoa(unsigned int ip)
{
	struct in_addr in;
	memset(&in, 0, sizeof(in));
	in.s_addr = ip;
	return inet_ntoa(in);
}

/*
 * Extract a 2-byte value in host format from a receive buffer.
 */
void tcpPacketExtract2Byte(unsigned short* value, char** buffer)
{
	unsigned short netValue;
	memcpy(&netValue, *buffer, sizeof(unsigned short));
	*value = ntohs(netValue);
	(*buffer) += sizeof(unsigned short);
}

/*
 * Extract a 4-byte value in host format from a receive buffer.
 */
void tcpPacketExtract4Byte(unsigned int* value, char** buffer)
{
	unsigned int netValue;
	memcpy(&netValue, *buffer, sizeof(unsigned int));
	*value = ntohl(netValue);
	(*buffer) += sizeof(unsigned int);
}

/*
 * Append a 2-byte value in network format to a send buffer.
 */
void tcpPacketAppend2Byte(unsigned short value, char** buffer)
{
	unsigned short netValue = htons(value);
	memcpy(*buffer, &netValue, sizeof(unsigned short));
	(*buffer) += sizeof(unsigned short);
}

/*
 * Append a 4-byte value in network format to a send buffer.
 */
void tcpPacketAppend4Byte(unsigned int value, char** buffer)
{
	unsigned int netValue = htonl(value);
	memcpy(*buffer, &netValue, sizeof(unsigned int));
	(*buffer) += sizeof(unsigned int);
}

/*
 * Check if there is a 'waiting' error condition on the socket.
 *
 * int rc >= 0: No error was waiting
 * int rc <  0: There was an error condition on the socket
*/
static int tcpPacketClearSocket(int socket, char* tag)
{
	int socketError = 0;
	int optlen = sizeof(int);
	errno = 0;

	/*
	 * We clear any error condition on the socket
	 */
	if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (char*)&socketError, (unsigned int*)&optlen))
	{
		int myErrno;
		myErrno = TCP_ERRNO;

		if ((myErrno != EPIPE)
			&& (myErrno != TCP_ESHUTDOWN)
			&& (myErrno != TCP_ECONNABORTED)
			&& (myErrno != TCP_ECONNRESET))
		{
			LOG_ERROR(("%s: getsockopt failed! %s\n", tag, TCP_ERRMSG));
		}
	}

	/*
	 * Give out the error message if there was one
	 */
	if (socketError)
	{
		LOG_TRACE(("%s: cleared error %s for socket %d\n", tag, strerror(socketError), socket));
		return -1;
	}
	return 0;
}

/*
 * Read one packet from a TCP socket.
 *
 * int rc > 0: Size of the packet received
 * int rc = 0: Connection lost
 * int rc < 0: An error occured
 *  TCP_ERR_RECV:        Network read error in receive call
 *  TCP_ERR_EINTR:       The function has been interrupted by a signal
 *  TCP_ERR_EWOULDBLOCK: No more data available
*/
int tcpPacketRead(int socket, char* buffer, int length)
{
	static char* function = "tcpPacketRead";
	errno = 0;

	if (socket < 0)
	{
		LOG_ERROR(("%s: Attempt to read from  illegal TCP socket %d\n", function, socket));
		return TCP_ERR_CONNECTION;
	}

	if (tcpPacketClearSocket(socket, function))
	{
		return TCP_ERR_EINTR;
	}

	int rc = recv(socket, buffer, length, 0);
	if (rc == 0)
	{
		return 0;
	}
	else if (rc < 0)
	{
		int myErrno;
		myErrno = TCP_ERRNO;

		tcpPacketClearSocket(socket, function);

		switch (myErrno)
		{
		case TCP_EINTR:
			return TCP_ERR_EINTR;

		case TCP_EWOULDBLOCK:
			return TCP_ERR_EWOULDBLOCK;
		}

		if (myErrno == TCP_ECONNRESET || myErrno == TCP_ECONNABORTED)
		{
			return 0;
		}
		else if (myErrno == TCP_ESHUTDOWN)
		{
			return 0;
		}

#ifdef _WIN32
		LOG_ERROR(("%s: recv on socket %d failed! rc %d, %d\n",
			function, socket, rc, myErrno));
#else
		LOG_ERROR(("%s: recv on socket %d failed! rc %d, %s\n",
			function, socket, rc, strerror(myErrno)));
#endif
		return TCP_ERR_RECV;
	}
	return rc;
}

/*
 * Send a packet via a send socket call.
 *
 * int rc >= 0:  number of bytes successfully sent
 * int rc <  0:  cannot send packet
 *  TCP_ERR_EINTR:       The function has been interrupted by a signal
 *  TCP_ERR_EWOULDBLOCK: No more buffer space available
*/
int tcpPacketSend(int socket, char* buffer, int length)
{
	static char* function = "tcpPacketSend";
	errno = 0;

	if (socket < 0)
	{
		LOG_ERROR(("%s: Attempt to send to illegal TCP socket %d\n", function, socket));
		return TCP_ERR_CONNECTION;
	}

	tcpPacketClearSocket(socket, function);

	if (length < 1)
	{
		return 0;
	}

	int rc = send(socket, buffer, length, 0);
	if (rc >= 0)
	{
		return rc;
	}
	else
	{
		int myErrno;
		myErrno = TCP_ERRNO;

		tcpPacketClearSocket(socket, function);

		switch (myErrno)
		{
		case TCP_EINTR:
			return TCP_ERR_EINTR;

		case TCP_EWOULDBLOCK:
			return TCP_ERR_EWOULDBLOCK;
		}

#ifdef _WIN32
		LOG_INFO(("%s: send on socket %d failed! length %d, rc %d, %d\n",
			function, socket, length, rc, myErrno));
#else
		if (myErrno == EPIPE)
		{
			LOG_TRACE(("%s: send on socket %d failed! length %d, rc %d, %s\n",
				function, socket, length, rc, strerror(myErrno)));
		}
		else
		{
			LOG_INFO(("%s: send on socket %d failed! length %d, rc %d, %s\n",
				function, socket, length, rc, strerror(myErrno)));
		}
#endif
		return TCP_ERR_EINTR;
	}
	return TCP_ERR_EINTR;
}

/*
 * Opens a TCP socket for a given port number and sets the listen queue length.
 *
 * Depending on parameter reUse the socket option SO_REUSEADDR is set before the bind call.
 *
 * This function can be used in server programs that want to
 * work on a specific port and listen for incoming TCP connections.
 *
 * int rc >= 0: A socket fd to work with
 * int rc <  0: An error occured
 *  TCP_ERR_SOCKET        socket() call failed
 *  TCP_ERR_BIND          bind() call failed
 *  TCP_ERR_LISTEN        listen() call failed
 */
int tcpPacketCreateListenSocket(unsigned short port, int reUse)
{
	static char* function = "tcpPacketCreateListenSocket";
	struct sockaddr_in serv_addr;
	errno = 0;

	/*
	 * Open a TCP socket
	 */
	int sockedFd = (int)socket(AF_INET, SOCK_STREAM, 0);

#ifdef _WIN32
	if (sockedFd == INVALID_SOCKET)
#else
	if (sockedFd < 0)
#endif
	{
		LOG_ERROR(("%s: socket(AF_INET, SOCK_STREAM, 0) failed! %s!\n", function, TCP_ERRMSG));
		return TCP_ERR_SOCKET;
	}

	/*
	 * If we are asked to make the socket reusable
	 */
	if (reUse)
	{
		int optval = 1;
		if (setsockopt(sockedFd, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval)))
		{
			LOG_ERROR(("%s: setsockopt() failed! %s\n", function, TCP_ERRMSG));
		}
	}

	/*
	 * Bind the address
	 */
	memset((char*)&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(((short)port));

	if (bind(sockedFd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
	{
		LOG_ERROR(("%s: bind(socket, port %u) failed! %s!\n", function, port, TCP_ERRMSG));
		socket_close(sockedFd);
		return TCP_ERR_BIND;
	}

	/*
	 * Init the listen queue
	 */
	int queueLength = 511;
	if (listen(sockedFd, queueLength) < 0)
	{
		LOG_ERROR(("%s: listen(sockedFd, ...) failed! %s!\n", function, TCP_ERRMSG));
		socket_close(sockedFd);
		return TCP_ERR_LISTEN;
	}

	LOG_INFO(("TCPSOCKET %d bound to port %d, listen queue length %d\n",
		sockedFd, port, queueLength));
	return sockedFd;
}

/*
 * Shutdown a TCP socket, the socket is closed immediately and pending data is dropped.
 *
 * int rc = 0: packet successfully send
 * int rc < 0: cannot send packet. Error logged
 */
void tcpPacketCloseSocket(int socket)
{
	static char* function = "tcpPacketCloseSocket";
	struct timeval tvStart = { 0 };
	struct timeval tvEnd = { 0 };
	struct linger  param;
#ifdef _WIN32
	param.l_onoff = 1;         /* linger */
	param.l_linger = 0;        /* but only 0 seconds */
#else
	param.l_onoff = 0;         /* drop data */
	param.l_linger = 0;        /* return at once */
#endif

	errno = 0;
	int rc = setsockopt(socket, SOL_SOCKET, SO_LINGER, (const char*)&param, sizeof(struct linger));
	if (rc != 0)
	{
		int myErrno = TCP_ERRNO;

		if ((myErrno != EPIPE)
			&& (myErrno != TCP_ESHUTDOWN)
			&& (myErrno != TCP_ECONNABORTED)
			&& (myErrno != TCP_ECONNRESET))
		{
#ifdef _WIN32
			LOG_TRACE(("%s: setopt on socket %d failed! %d\n",
				function, socket, myErrno));
#else
			LOG_TRACE(("%s: setopt on socket %d failed! %s\n",
				function, socket, strerror(myErrno)));
#endif
		}
	}

	gettimeofday(&tvStart, NULL);
	socket_close(socket);
	gettimeofday(&tvEnd, NULL);

	if (tvEnd.tv_usec < tvStart.tv_usec)
	{
		tvEnd.tv_usec += 1000000;
		tvEnd.tv_sec -= 1;
	}

	struct timeval tvDiff = { 0 };
	tvDiff.tv_sec = tvEnd.tv_sec - tvStart.tv_sec;
	tvDiff.tv_usec = tvEnd.tv_usec - tvStart.tv_usec;

	if (tvDiff.tv_sec || tvDiff.tv_usec > 100000)
	{
		LOG_INFO(("%s: took %ld.%06ld seconds\n", function, tvDiff.tv_sec, tvDiff.tv_usec));
	}
}

/*
 * Accept an incoming connection request and establish a new connection on a new TCP socket.
 *
 * Returned ip and port are in host format.
 *
 * int rc >= 0: The new TCP socket
 * int rc <  0: An error occured, the error is logged in stderr
 *  TCP_ERR_ACCEPT        accept() call failed
 *  TCP_ERR_EINTR:        The function has been interrupted by a signal
 *  TCP_ERR_EWOULDBLOCK:  No connection pending
 */
int tcpPacketAccept(int listenSocket, unsigned int* pIp, unsigned short* pPort, char** pInetAddr)
{
	static char* function = "tcpPacketAccept";
	struct sockaddr_in client_addr;
	unsigned int       ip = 0;
	unsigned short     port = 0;

	memset(&client_addr, 0, sizeof(client_addr));
	int addrlen = sizeof(client_addr);

	tcpPacketClearSocket(listenSocket, function);

	errno = 0;
	int socket = (int)accept(listenSocket, (struct sockaddr*)&client_addr, (unsigned int*)&addrlen);
	if (socket < 0)
	{
		int myErrno = TCP_ERRNO;

		tcpPacketClearSocket(listenSocket, function);

		switch (myErrno)
		{
		case TCP_EINTR:
			return TCP_ERR_EINTR;

		case TCP_EWOULDBLOCK:
			return TCP_ERR_EWOULDBLOCK;
		}

#ifdef _WIN32
		LOG_ERROR(("%s: accept(listenSocket %d, ...) failed! %d!\n",
			function, listenSocket, myErrno));
#else
		switch (myErrno)
		{
		case ECONNRESET:
		case ETIMEDOUT:
		case EHOSTUNREACH:
		case ECONNABORTED:
			LOG_INFO(("%s: accept(listenSocket %d, ...) failed! %s!\n",
				function, listenSocket, strerror(myErrno)));

			tcpPacketClearSocket(listenSocket, function);
			return TCP_ERR_EINTR;

		default:
			LOG_ERROR(("%s: accept(listenSocket %d, ...) failed! %s!\n",
				function, listenSocket, strerror(myErrno)));
		}

#endif   
		tcpPacketClearSocket(listenSocket, function);
		return TCP_ERR_ACCEPT;
	}

	/* extract ip and port out of socket addr struct */
	tcpPacketSrvadrToIpPort((char*)&client_addr, &ip, &port);
	if (pIp)
	{
		*pIp = ntohl(ip);
	}
	if (pPort)
	{
		*pPort = ntohs(port);
	}
	if (pInetAddr)
	{
		*pInetAddr = tcpPacketInetNtoa(ip);
	}
	return socket;
}

/*
 * Set a socket either to blocking or non blocking mode.
 *
 * int rc = 0: success
 * int rc < 0: cannot switch socket mode
 */
int tcpPacketSocketSetNonBlocking(int socket, int nonBlocking)
{
	int  arg;
	int  rc;
	static char* function = "tcpPacketSocketSetNonBlocking";

#ifdef _WIN32
	arg = (nonBlocking) ? 1 : 0;
	rc = ioctlsocket(socket, FIONBIO, &arg);
	if (rc != 0)
	{
		LOG_ERROR(("%s: ioctlsocket on socket %d failed! %d\n", function, socket, WSAGetLastError()));
		return -1;
	}

#else
	errno = 0;
	arg = fcntl(socket, F_GETFL);
	if (arg < 0)
	{
		LOG_ERROR(("%s: fnctl_get on socket %d failed! %s\n", function, socket, strerror(errno)));
		return -1;
	}

	if (nonBlocking)
	{
		arg |= O_NONBLOCK;
	}
	else
	{
		arg &= ~O_NONBLOCK;
	}
	errno = 0;
	rc = fcntl(socket, F_SETFL, arg);
	if (rc < 0)
	{
		LOG_ERROR(("%s: fnctl_set on socket %d failed! %s\n",
			function, socket, strerror(errno)));
		return -1;
	}
#endif
	return 0;
}

/*
 * Count statistics when a packet is read
 */
void tcpPacketReadStatistics(int nBytes)
{
	time_t now = time((time_t*)NULL);
	TcpPacketStatisticPerSecond* statistics = tcpPacketStatisticsPerSecond + (now % TCP_INTERVAL_SECONDS);

	if (statistics->second != now)
	{
		statistics->second = now;
		statistics->nPacketsReceived = statistics->nBytesReceived =
			statistics->nPacketsSent = statistics->nBytesSent = 0;
	}

	if (nBytes >= 0)
	{
		statistics->nBytesReceived += nBytes;
		statistics->nPacketsReceived++;
	}
}

/*
 * Count statistics when a packet is sent
 */
void tcpPacketSentStatistics(int nBytes)
{
	time_t now = time((time_t*)NULL);
	TcpPacketStatisticPerSecond* statistics = tcpPacketStatisticsPerSecond + (now % TCP_INTERVAL_SECONDS);

	if (statistics->second != now)
	{
		statistics->second = now;
		statistics->nPacketsReceived = statistics->nBytesReceived =
			statistics->nPacketsSent = statistics->nBytesSent = 0;
	}

	if (nBytes >= 0)
	{
		statistics->nBytesSent += nBytes;
		statistics->nPacketsSent++;
	}
}

/*
 * Calculate statistics for the last n seconds.
 */
static void tcpPacketStatisticsPerNSeconds(
	int nSeconds,
	unsigned long* pPacketsReceived,
	unsigned long* pBytesReceived,
	unsigned long* pPacketsSent,
	unsigned long* pBytesSent
)
{
	time_t now = time((time_t*)NULL);
	int i;
	TcpPacketStatisticPerSecond* statistics;
	unsigned long  nPacketsReceived = 0;
	unsigned long  nBytesReceived = 0;
	unsigned long  nPacketsSent = 0;
	unsigned long  nBytesSent = 0;

	if (nSeconds < 1)
	{
		nSeconds = 1;
	}
	else if (nSeconds >= TCP_INTERVAL_SECONDS)
	{
		nSeconds = TCP_INTERVAL_SECONDS - 1;
	}

	/*
	 * Find the right index for the per second statistics
	 */
	statistics = tcpPacketStatisticsPerSecond + (now % TCP_INTERVAL_SECONDS);

	/*
	 * Look back for TCP_INTERVAL_SECONDS seconds
	 */
	now -= TCP_INTERVAL_SECONDS;

	/*
	 * Start with the second that just elapsed
	 */
	statistics--;

	for (i = 0; i < nSeconds; i++, statistics--)
	{
		if (statistics < tcpPacketStatisticsPerSecond)
		{
			statistics = tcpPacketStatisticsPerSecond + (TCP_INTERVAL_SECONDS - 1);
		}

		if (now < statistics->second)
		{
			nPacketsReceived += statistics->nPacketsReceived;
			nBytesReceived += statistics->nBytesReceived;
			nPacketsSent += statistics->nPacketsSent;
			nBytesSent += statistics->nBytesSent;
		}
	}

	if (pPacketsReceived)
	{
		*pPacketsReceived = nPacketsReceived;
	}
	if (pBytesReceived)
	{
		*pBytesReceived = nBytesReceived;
	}
	if (pPacketsSent)
	{
		*pPacketsSent = nPacketsSent;
	}
	if (pBytesSent)
	{
		*pBytesSent = nBytesSent;
	}
}

/*
 * Write statistics to the log file.
 */
void tcpPacketWriteStatistics()
{
	unsigned long nPacketsReceived;
	unsigned long nBytesReceived;
	unsigned long nPacketsSent;
	unsigned long nBytesSent;

	tcpPacketStatisticsPerNSeconds(1, &nPacketsReceived, &nBytesReceived, &nPacketsSent, &nBytesSent);

	LOG_INFO(("D last second PR %lu BR %lu PS %lu BS %lu\n",
		nPacketsReceived, nBytesReceived, nPacketsSent, nBytesSent));

	tcpPacketStatisticsPerNSeconds(10, &nPacketsReceived, &nBytesReceived, &nPacketsSent, &nBytesSent);

	LOG_INFO(("D av last 10s PR %lu BR %lu PS %lu BS %lu\n",
		nPacketsReceived / 10, nBytesReceived / 10, nPacketsSent / 10, nBytesSent / 10));

	tcpPacketStatisticsPerNSeconds(60, &nPacketsReceived, &nBytesReceived, &nPacketsSent, &nBytesSent);

	LOG_INFO(("D av last 60s PR %lu BR %lu PS %lu BS %lu\n",
		nPacketsReceived / 60, nBytesReceived / 60, nPacketsSent / 60, nBytesSent / 60));
}
