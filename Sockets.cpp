#include "Sockets.h"

#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#ifdef _DEBUG   
#ifndef DBG_NEW      
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )     
#define new DBG_NEW   
#endif
#endif

#include <stdio.h>
#include <stdlib.h>

#include "NetDefines.h"

//always include winsock before windows!!!!!
#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#pragma comment (lib, "Ws2_32.lib")
#else
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#endif

bool NetworkInit()
{
#ifdef _WIN32
	WSAData wsaData;
	int retval;
	if ((retval = WSAStartup(MAKEWORD(2,2), &wsaData)) != 0)
	{
		char sz[56];
		sprintf_s(sz, "WSAStartup failed with error %d\n",retval);
		OutputDebugStringA(sz);
		WSACleanup();
		return false;
	}
#endif
	return true;
};

bool NetworkCleanup()
{
#ifdef _WIN32
	WSACleanup();
#endif
	return true;
};

Address::Address(char* address, unsigned short port)
{
	this->address = ntohl(inet_addr(address));

	//need to add domain resolving

	/*struct sockaddr_in  *sockaddr_ipv4;
	sockaddr_in address2;
	address2.sin_family = AF_INET;
	address2.sin_addr.s_addr = htonl( this->address );
	address2.sin_port = htons( (unsigned short) 5007);
	sockaddr_ipv4 = (struct sockaddr_in *) ptr->ai_addr;
	OutputDebugString(inet_ntoa(address2));

	if (this->address != Address(127,0,0,1,5007).GetAddress())
	{
	OutputDebugString("not equal\n");
	}
	if ( this->address == INADDR_NONE ) {
	OutputDebugString("inet_addr returned INADDR_NONE\n");
	}

	if (this->address == INADDR_ANY) {
	OutputDebugString("inet_addr returned INADDR_ANY\n");
	}*/
	this->port = port;
}

static bool initialized = false;
Socket::Socket()
{
#ifdef _WIN32
	if (!initialized)
	{
		WSAData wsaData;
		int retval;
		if ((retval = WSAStartup(MAKEWORD(2,2), &wsaData)) != 0)
		{
			char sz[56];
			sprintf_s(sz, "WSAStartup failed with error %d\n",retval);
			OutputDebugStringA(sz);
			WSACleanup();
		}
		initialized = true;
	}
#endif
#ifdef NETSIMULATE
	this->rtt = 0;
	this->variance = 0;
	this->drop = 0;
#endif
	sent = received = 0;
	socket = 0;
}

Socket::~Socket()
{
	Close();
#ifdef NETSIMULATE
	while (this->lagged.size() > 0)
	{
		auto p = this->lagged.top();
		this->lagged.pop();
		delete[] p.data;
	}
#endif
}

bool Socket::Open( unsigned short port )
{
	//assert( !IsOpen() );

	// create socket

	socket = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

	if ( socket == 0 )
	{
		int po = port;
		printf("Failed To Create Socket on %i!\n", po);//OutputDebugString( "failed to create socket\n" );
		socket = 0;
		return false;
	}

	// bind to port

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons( (unsigned short) port );

	if ( bind( socket, (const sockaddr*) &address, sizeof(sockaddr_in) ) < 0 )
	{
#ifdef _WIN32
		int err = WSAGetLastError();
		printf("Failed to Bind Socket, %i\n", err);//OutputDebugString( "failed to bind socket\n" );
#endif
		Close();
		return false;
	}

	// set non-blocking i
#ifdef _WIN32
	DWORD nonBlocking = 1;
	if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
	{
		printf("Failed to Set Socket as Non-Blocking!\n");//OutputDebugString( "failed to set non-blocking socket\n" );
		Close();
		return false;
	}
#else
	int flags = fcntl(socket, F_GETFL);
	fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif

	return true;
}

void Socket::Close()
{
	if ( socket != 0 )
	{
#ifdef _WIN32
		closesocket( socket );
#else
		close(socket);
#endif
		socket = 0;
		received = 0;
	}
}

bool Socket::IsOpen() const
{
	return socket != 0;
}

bool Socket::Send( const Address & destination, const void * data, int size )
{
	this->sent += size;

#ifndef NETSIMULATE
	if ( socket == 0 )
		return false;

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( destination.GetAddress() );
	address.sin_port = htons( (unsigned short) destination.GetPort() );

	int sent_bytes = sendto( socket, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

	return sent_bytes == size;
#else
	if (drop > 0 && rand()%100 < drop)
		return true;

	this->SendLaggedPackets();


	int delay = 0;
	if (variance > 0)
		delay = rtt + (rand()%(2*variance)-variance);
	else
		delay = rtt;


	if (delay == 0)
	{
		if ( socket == 0 )
			return false;

		sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl( destination.GetAddress() );
		address.sin_port = htons( (unsigned short) destination.GetPort() );

		int sent_bytes = sendto( socket, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

		return sent_bytes == size;
	}
	else
	{
		laggedpacket p;
		p.addr = destination;
		p.data = new char[size];
		p.size = size;
		p.sendtime = NetGetTime() + delay;
		memcpy(p.data, data, size);
		this->lagged.push(p);
	}

	return true;
#endif
}

void Socket::SendLaggedPackets()
{
#ifdef NETSIMULATE
	//die compilation
	if ( socket == 0 )
		return;

	while (this->lagged.empty() == false)
	{
		laggedpacket ii = this->lagged.top();//front();
		if (NetGetTime() >= ii.sendtime)
		{
			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl( ii.addr.GetAddress() );
			address.sin_port = htons( (unsigned short) ii.addr.GetPort() );

			int sent_bytes = sendto( socket, (const char*)ii.data, ii.size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

			delete[] ii.data;
			this->lagged.pop();
		}
		else
			return;
	}
#endif
};

int Socket::Receive( Address & sender, void * data, int size )
{
	if ( socket == 0 )
		return false;

	typedef int socklen_t;

	sockaddr_in from;
	socklen_t fromLength = sizeof( from );

	int received_bytes = recvfrom( socket, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

	if ( received_bytes <= 0 )
		return 0;

	unsigned int address = ntohl( from.sin_addr.s_addr );
	unsigned int port = ntohs( from.sin_port );

	sender = Address( address, port );

	this->received += received_bytes;

	return received_bytes;
}

void Socket::SetMulticastTTL(unsigned char multicastTTL)
{
	/*if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&multicastTTL, sizeof(multicastTTL)) < 0)
	{
	printf("fail setmulticastTTL\n");
	}*/
}

void Socket::JoinGroup(const char* multicastGroup)
{
	/*ip_mreq multicastRequest;

	multicastRequest.imr_multiaddr.s_addr = inet_addr(multicastGroup);
	multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&multicastRequest, sizeof(multicastRequest)) < 0)
	{
	printf("fail joingroup\n");
	}*/
}

void Socket::LeaveGroup(const char* multicastGroup)
{
	/*ip_mreq multicastRequest;

	multicastRequest.imr_multiaddr.s_addr = inet_addr(multicastGroup);
	multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&multicastRequest, sizeof(multicastRequest)) < 0)
	{
	}*/
}

SocketTCP::SocketTCP()
{
#ifdef _WIN32
	if (!initialized)
	{
		WSAData wsaData;
		int retval;
		if ((retval = WSAStartup(MAKEWORD(2,2), &wsaData)) != 0)
		{
			char sz[56];
			sprintf_s(sz, "WSAStartup failed with error %d\n",retval);
			OutputDebugStringA(sz);
			WSACleanup();
		}
		initialized = true;
	}
#endif
	received = 0;
	socket = 0;
}

#ifdef _WIN32
SocketTCP::SocketTCP(unsigned int sock)
{
	received = 0;
	socket = sock;
}
#else
SocketTCP::SocketTCP(int sock)
{
	received = 0;
	socket = sock;
}
#endif

SocketTCP::~SocketTCP()
{
	Close();
}

bool SocketTCP::SetNonBlocking( bool enable)
{
	if (enable)
	{
#ifdef _WIN32
		DWORD nonBlocking = 1;
		if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
		{
			printf( "failed to set non-blocking socket\n" );
			return false;
		}
#else
		int flags = fcntl(socket, F_GETFL);
		fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
	}
	else
	{
#ifdef _WIN32
		DWORD nonBlocking = 0;
		if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
		{
			printf( "failed to set blocking socket\n" );
			return false;
		}
#else
		int flags = fcntl(socket, F_GETFL);
		fcntl(socket, F_SETFL, flags ^ O_NONBLOCK);
#endif
	}
	return true;
}

bool SocketTCP::SetTimeout(int ms)
{
	if (socket == 0)
		return false;
	int timeout = ms;//time in ms

	if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
		sizeof(timeout)) < 0)
	{
		perror("setsockopt failed\n");
		return false;
	}

	if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
		sizeof(timeout)) < 0)
	{
		perror("setsockopt failed\n");
		return false;
	}
	return true;
}

bool SocketTCP::Connect( const char* address, const char* protocol)
{
	addrinfo req;
	addrinfo *res;

	memset(&req, 0, sizeof(req));
	req.ai_family = AF_UNSPEC;
	req.ai_socktype = SOCK_STREAM;

	const char* proto = protocol;
#ifndef _WIN32
	if (strcmp(protocol, "http") == 0)
	{
		proto = "80";//for linux and wutnot
	}
#endif
	if (getaddrinfo(address, proto, &req, &res) < 0)
	{
		perror("getaddrinfo");
		return false;
	}

	if (res == 0)
	{
		return false;
	}

	int s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s < 0)
	{
		perror("socket");
		return false;
	}
	this->socket = s;

	if (connect(socket, res->ai_addr, res->ai_addrlen) < 0)
	{
		perror("connect");
#ifndef _WIN32
		close(socket);
#else
		closesocket(socket);
#endif
		return false;
	}

	return true;
}

bool SocketTCP::Connect( Address server )
{
	// create socket
	socket = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( socket <= 0 )
	{
		//log( "failed to create socket\n" );
		socket = 0;
		return false;
	}

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( server.GetAddress() );
	address.sin_port = htons( (unsigned short) server.GetPort() );

	if ( connect(socket, (const sockaddr*) &address, sizeof(sockaddr_in)) < 0)
	{
		//log( "failed to connect socket\n" );
		Close();
		return false;
	}

	// set non-blocking i
#ifdef _WIN32
	DWORD nonBlocking = 1;
	if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
	{
		printf( "failed to set non-blocking socket\n" );
		Close();
		return false;
	}
#else
	int flags = fcntl(socket, F_GETFL);
	fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif

	return true;
}

SocketTCP* SocketTCP::Accept(Address& sender)//returns sock if successful, and sets the addr to the user connecting
{
	SocketTCP* ret = NULL;

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons( 0 );//(unsigned short) port );

	int size = sizeof(sockaddr_in);
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(socket, &rfds);

	struct timeval tv;

	/* Wait up to five seconds. */
	//there is something wierd here on linux I saw it before
	tv.tv_sec = 20;
	tv.tv_usec = 0;
#ifdef _WIN32
	if (select(1, &rfds, 0, 0, &tv))
#else
	if (true)
#endif
	{
		int n = accept( socket, (sockaddr*) &address, &size);
		if (n != 0)
		{
			//closesocket(this->socket);
			ret = new SocketTCP(n);

			unsigned int addr = ntohl( address.sin_addr.s_addr );
			unsigned int port = ntohs( address.sin_port );

			sender = Address( addr, port );

			//this->socket = n;
			return ret;
		}
		else
		{
#ifdef _WIN32
			fprintf(stderr, "Error accepting %d\n",WSAGetLastError());
#endif
		}
	}
	else
	{
#ifdef _WIN32
		fprintf(stderr, "Error accepting %d\n",WSAGetLastError());
#endif
	}
	return ret;
}

bool SocketTCP::Listen(unsigned short port)
{
	//create socket
	socket = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( socket <= 0 )
	{
		//log( "failed to create socket\n" );
		socket = 0;
		return false;
	}

	// bind to port
	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons( (unsigned short) port );

	if ( bind( socket, (const sockaddr*) &address, sizeof(sockaddr_in) ) < 0 )
	{
#ifdef _WIN32
		printf( "failed to bind socket\n" );
		fprintf(stderr, "bind() failed, Error: %d\n", WSAGetLastError());
#endif
		Close();
		return false;
	}

	if (listen(socket, 10) < 0)
	{
		//log("listen failed\n");
	}

	//int size = sizeof(sockaddr_in);
	//accept(socket, (sockaddr*) &address, sizeof(sockaddr_in));

	/*while(true){
	printf("waiting for a connection\n");
	//csock = (int*)malloc(sizeof(int));
	SOCKET n = accept( socket, (sockaddr*) &address, &size);
	if (n != 0)
	{
	closesocket(this->socket);
	this->socket = n;
	printf("Received connection from %s\n",inet_ntoa(address.sin_addr));

	break;//CreateThread(0,0,&SocketHandler, (void*)csock, 0,0);
	}
	else
	{
	fprintf(stderr, "Error accepting %d\n",WSAGetLastError());
	}
	}*/

	// set non-blocking i
	//DWORD nonBlocking = 1;
	//if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
	//{
	//printf( "failed to set non-blocking socket\n" );
	//Close();
	//return false;
	//}
	return true;
}

bool SocketTCP::Open( Address & server, unsigned short port )
{
	//assert( !IsOpen() );

	// create socket

	socket = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

	if ( socket <= 0 )
	{
		printf( "failed to create socket\n" );
		socket = 0;
		return false;
	}

	// bind to port
	//sockaddr_in address;
	//address.sin_family = AF_INET;
	//address.sin_addr.s_addr = INADDR_ANY;
	//address.sin_port = htons( (unsigned short) port );

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( server.GetAddress() );
	address.sin_port = htons( (unsigned short) server.GetPort() );

	//if ( bind( socket, (const sockaddr*) &address, sizeof(sockaddr_in) ) < 0 )
	//{
	//OutputDebugString( "failed to bind socket\n" );
	//Close();
	//return false;
	//}

	if ( connect(socket, (const sockaddr*) &address, sizeof(sockaddr_in)) < 0)
	{
#ifdef _WIN32
		OutputDebugStringA( "failed to connect socket\n" );
#endif
		Close();
		return false;
	}

	// set non-blocking i
#ifdef _WIN32
	DWORD nonBlocking = 1;
	if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
	{
		OutputDebugStringA( "failed to set non-blocking socket\n" );

		Close();
		return false;
	}
#else
	int flags = fcntl(socket, F_GETFL);
	fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif

	return true;
}

void SocketTCP::Close()
{
	if ( socket != 0 )
	{
		//log("TCP Connection Closed...\n");
#ifdef _WIN32
		if(closesocket( socket ) != 0)
#else
		if(close(socket) != 0)
#endif
		{
#ifdef _WIN32
			fprintf(stderr, "closesocket() failed, Error: %d\n", WSAGetLastError());
#endif
		}
		socket = 0;
		received = 0;
	}
}

bool SocketTCP::IsOpen() const
{
	return socket != 0;
}

bool SocketTCP::Send( const void * data, int size) //const Address & destination, const void * data, int size )
{
	if ( socket == 0 )
		return false;

	//sockaddr_in address;
	//address.sin_family = AF_INET;
	//address.sin_addr.s_addr = htonl( destination.GetAddress() );
	//address.sin_port = htons( (unsigned short) destination.GetPort() );
	//WSASetLastError(0);

	int sent_bytes = send(socket, (const char*)data, size, 0);//sendto( socket, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

	/*int err = WSAGetLastError();
	if (err && err != 10035)//not a block error or 0
	{
	char o[100];
	sprintf(o, "TCP Send Socket Error %d\n",WSAGetLastError());
	OutputDebugString(o);
	//goto cleanup;
	}*/
	return sent_bytes == size;
}

int SocketTCP::Receive( void * data, unsigned int size )
{
	if ( socket == 0 )
		return false;

	int received_bytes = recv(socket, (char*)data, size, 0);//recvfrom( socket, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

	if ( received_bytes <= 0 )
		return 0;

	/*int err = WSAGetLastError();
	if (err && err != 10035)//not a block error or 0
	{
	char o[100];
	sprintf(o, "TCP Recv Socket Error %d\n",WSAGetLastError());
	OutputDebugString(o);
	//goto cleanup;
	}*/

	this->received += received_bytes;

	return received_bytes;
};