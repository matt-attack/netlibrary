#include "Networking.h"

#include <stdio.h>
//always include winsock before windows!!!!!
#ifdef WINDOWS
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#pragma comment (lib, "Ws2_32.lib")
#endif

bool NetworkInit()
{
#ifdef WINDOWS
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
#ifdef WINDOWS
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
	port = 0;
	received = 0;
	socket = 0;
}

Socket::~Socket()
{
	Close();
}

bool Socket::Open( unsigned short port )
{
	//assert( !IsOpen() );
	log("socket opened %d", (int)port);

	// create socket
	errno = 0;
	socket = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

	if ( socket <= 0 )
	{
		log("failed to create socket %i", port);
		//OutputDebugString( "failed to create socket\n" );
		socket = 0;
		return false;
	}

	if (errno != 0)
	{
		log("socket create error %d", errno);
		errno = 0;
	}

	// bind to port

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons( (unsigned short) port );

	if ( bind( socket, (const sockaddr*) &address, sizeof(sockaddr_in) ) < 0 )
	{
		//jboolean isCopy;
		//const char * szLogThis = "failed to bind socket";//"hey";//(*env)->GetStringUTFChars(env, logThis, &isCopy);
		//__android_log_print(ANDROID_LOG_DEBUG, DEBUG_TAG, "NDK:LC: [%s]", szLogThis);

		log("failed to bind socket");//OutputDebugString( "failed to bind socket\n" );
		Close();
		return false;
	}

	if (errno != 0)
	{
		//const char * szLogThis = "socket bind error";//"hey";//(*env)->GetStringUTFChars(env, logThis, &isCopy);
		//__android_log_print(ANDROID_LOG_DEBUG, DEBUG_TAG, "NDK:LC: [%s] [%d]", szLogThis, errno);
		log("socket bind error %d",errno);
		errno = 0;
	}

	// set non-blocking i
#ifdef WINDOWS
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

	u_int yes = 1;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
	{
		log("reuse addr failed");
		// perror("Reusing ADDR failed");
		//exit(1);
	}

	if (errno != 0)
	{
		//const char * szLogThis = "socket non-block error";//"hey";//(*env)->GetStringUTFChars(env, logThis, &isCopy);
		//__android_log_print(ANDROID_LOG_DEBUG, DEBUG_TAG, "NDK:LC: [%s] [%d]", szLogThis, errno);
		log("socket non-block error %d", errno);
		errno = 0;
	}

	this->port = port;
	return true;
}

void Socket::Close()
{
	log("socket closed %i", (int)this->port);
	if ( socket != 0 )
	{
#ifdef WINDOWS
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
	if ( socket == 0 )
		return false;

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ntohl(destination.GetAddress());//destination.GetAddress();//
	address.sin_port = htons( (unsigned short) destination.GetPort() );

	int sent_bytes = sendto( socket, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

	return sent_bytes == size;
}

int Socket::Receive( Address & sender, void * data, int size )
{
	if ( socket == 0 )
		return false;

	typedef int socklen_t;

	sockaddr_in from;
	socklen_t fromLength = sizeof( from );

	int received_bytes = recvfrom( socket, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

	if ( received_bytes <= 0 )
	{
		if (errno != 11)
		{
			//char o[50];
			//sprintf(o, "had a socket issue %i", received_bytes);
			//const char * szLogThis = "socket create error";//"hey";//(*env)->GetStringUTFChars(env, logThis, &isCopy);
			//__android_log_print(ANDROID_LOG_DEBUG, DEBUG_TAG, "NDK:LC: [%s] [%d]", o, errno);
			log("socket recieve error %d", errno);
			errno = 0;
		}
		return 0;
	}

	unsigned int address = ntohl( from.sin_addr.s_addr );
	unsigned short port = ntohs( from.sin_port );

	sender = Address( address, port );

	this->received += received_bytes;

	return received_bytes;
}

void Socket::SetMulticastTTL(unsigned char multicastTTL)
{
	if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&multicastTTL, sizeof(multicastTTL)) < 0)
	{
		log("fail setmulticastTTL");
	}
}

void Socket::JoinGroup(const char* multicastGroup)
{
	ip_mreq multicastRequest;

	multicastRequest.imr_multiaddr.s_addr = inet_addr(multicastGroup);
	multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&multicastRequest, sizeof(multicastRequest)) < 0)
	{
		log("fail joingroup");
	}
}

void Socket::LeaveGroup(const char* multicastGroup)
{
	ip_mreq multicastRequest;

	multicastRequest.imr_multiaddr.s_addr = inet_addr(multicastGroup);
	multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&multicastRequest, sizeof(multicastRequest)) < 0)
	{
	}
}

SocketTCP::~SocketTCP()
{
	Close();
}

SocketTCP::SocketTCP()
{
	received = 0;
	socket = 0;
}

SocketTCP::SocketTCP(SOCKET& sock)
{
	received = 0;
	socket = sock;
}

bool SocketTCP::SetNonBlocking(bool enable)
{
	if (enable)
	{
#ifdef WIN32
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
#ifdef WINDOWS
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
	//int timeout = ms;//time in ms
	struct timeval timeout;
	timeout.tv_sec = 2;
	timeout.tv_usec = 2000;//ms*1000;

	if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
		sizeof(timeout)) < 0)
	{
		log("setsockopt error: %d", errno);
		perror("setsockopt failed\n");
		return false;
	}

	if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
		sizeof(timeout)) < 0)
	{
		log("setsockopt error: %d", errno);
		perror("setsockopt failed\n");
		return false;
	}
	return true;
}

bool SocketTCP::Connect( const char* address)
{
	addrinfo req;
	addrinfo *res;

	memset(&req, 0, sizeof(req));
	req.ai_family = AF_UNSPEC;
	req.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(address, "80", &req, &res) != 0)
	{
		log("get addr info failed");
		return false;
	}

	if (res == 0)
	{
		log("sock info null");
		return false;
	}

	int s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s < 0)
	{
		log("socket create error %d", errno);
		return false;
	}
	this->socket = s;

	if (connect(socket, res->ai_addr, res->ai_addrlen) < 0)
	{
		log("socket connect error %d", errno);
		close(socket);
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
		log( "failed to create socket\n" );
		socket = 0;
		return false;
	}

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( server.GetAddress() );//server.GetAddress();//
	address.sin_port = htons( (unsigned short) server.GetPort() );

	if ( connect(socket, (const sockaddr*) &address, sizeof(sockaddr_in)) < 0)
	{
		log( "failed to connect socket\n" );
		Close();
		return false;
	}

	// set non-blocking i
	/*DWORD nonBlocking = 1;
	if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
	{
	printf( "failed to set non-blocking socket\n" );
	Close();
	return false;
	}*/

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
	SOCKET n = accept( socket, (sockaddr*) &address, &size);
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
#ifdef WINDOWS
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
		log( "failed to create socket\n" );
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
		log( "failed to bind socket\n" );
		//fprintf(stderr, "bind() failed, Error: %d\n", WSAGetLastError());
		Close();
		return false;
	}

	if (listen(socket, 10) < 0)
	{
		log("listen failed\n");
	}

	int size = sizeof(sockaddr_in);
	//accept(socket, (sockaddr*) &address, sizeof(sockaddr_in));

	/*while(true){
	log("waiting for a connection\n");
	//csock = (int*)malloc(sizeof(int));
	SOCKET n = accept( socket, (sockaddr*) &address, &size);
	if (n != 0)
	{
	close(this->socket);//close socket
	this->socket = n;
	log("Received connection from %s\n",inet_ntoa(address.sin_addr));

	break;//CreateThread(0,0,&SocketHandler, (void*)csock, 0,0);
	}
	else
	{
	//printf(stderr, "Error accepting %d\n",WSAGetLastError());
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
		log( "failed to create socket\n" );
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
		//OutputDebugStringA( "failed to connect socket\n" );
		Close();
		return false;
	}

	// set non-blocking i
	DWORD nonBlocking = 1;
	/*if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
	{
	OutputDebugStringA( "failed to set non-blocking socket\n" );
	Close();
	return false;
	}*/

	return true;
}

void SocketTCP::Close()
{
	if ( socket != 0 )
	{
		log("TCP Connection Closed...\n");
#ifdef WINDOWS
		if(closesocket( socket ) != 0)
#else
		if(close(socket) != 0)
#endif
		{
#ifdef WINDOWS
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

	return sent_bytes == size;
}

int SocketTCP::Receive( void * data, unsigned int size )
{
	if ( socket == 0 )
		return -1;

	int received_bytes = recv(socket, data, size, 0);//recvfrom( socket, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

	if (received_bytes < 0)
	{
		if (errno != EWOULDBLOCK)//ignore errors about it being blocked
		{
			char o[50];
			sprintf(o,"socket recv err: %d", errno);
			log(o);

			return -1;
		}
	}
	if ( received_bytes <= 0 )
		return 0;

	//unsigned int address = ntohl( from.sin_addr.s_addr );
	//unsigned int port = ntohs( from.sin_port );

	this->received += received_bytes;

	return received_bytes;
};