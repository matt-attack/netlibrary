#ifndef NETWORKING_HEADER
#define NETWORKING_HEADER

#ifdef _WIN32
#ifndef INCLUDEDSOCK
#define INCLUDEDSOCK
#endif
#define WINDOWS
#else
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#ifdef ANDROID
#include <android/log.h>
#include <jni.h>
#endif
#include <string.h>
#endif

#include <queue>

//uncomment and change these for simulation of bad network conditions
#define NETSIMULATE
#include <mutex>

//must call this before any networking calls will work
bool NetworkInit();
bool NetworkCleanup();

class Address
{
public:

	Address()
	{
		address = 0;
		port = 0;
	}

	Address( unsigned char a, unsigned char b, unsigned char c, unsigned char d, unsigned short port )
	{
		this->address = ( a << 24 ) | ( b << 16 ) | ( c << 8 ) | d;
		this->port = port;
	}

	Address(char* address, unsigned short port);

	Address( unsigned int address, unsigned short port )
	{
		this->address = address;
		this->port = port;
	}

	unsigned int GetAddress() const
	{
		return address;
	}

	unsigned char GetA() const
	{
		return ( unsigned char ) ( address >> 24 );
	}

	unsigned char GetB() const
	{
		return ( unsigned char ) ( address >> 16 );
	}

	unsigned char GetC() const
	{
		return ( unsigned char ) ( address >> 8 );
	}

	unsigned char GetD() const
	{
		return ( unsigned char ) ( address );
	}

	unsigned short GetPort() const
	{
		return port;
	}

	bool operator == ( const Address & other ) const
	{
		return address == other.address && port == other.port;
	}

	bool operator != ( const Address & other ) const
	{
		return address != other.address || port != other.port;//! ( *this == other );
	}

	bool operator < ( const Address & other ) const
	{
		return (this->address < other.address) || (this->address == other.address && this->port < other.port);
		//(id1.x < id2.x) || (id1.x == id2.x && id1.z < id2.z)
	}
	unsigned short port;
private:

	unsigned int address;
};

#ifdef NETSIMULATE
struct laggedpacket
{
	Address addr;
	unsigned int sendtime;
	unsigned int size;
	char* data;

	bool operator<(const laggedpacket& other) const {
		return this->sendtime > other.sendtime;//larger send times come later, and in priority queue, largest item goes first, so smaller send times are priority
	}
};
#endif

// sockets
class Socket
{
#ifdef NETSIMULATE
	std::mutex laggedmutex;
	std::priority_queue<laggedpacket> lagged;
#endif
	unsigned short port;
public:
	unsigned int received;
	unsigned int sent;

	Socket();
	~Socket();

	bool Open( unsigned short port );
	void Close();

	bool IsOpen() const;

	bool Send( const Address & destination, const void * data, int size );
	int Receive( Address & sender, void * data, int size );

	void SetMulticastTTL(unsigned char multicastTTL);

	void JoinGroup(const char* multicastGroup);
	void LeaveGroup(const char* multicastGroup);

	//should call this regularly to get simulation working
	void SendLaggedPackets();

	void SetVariance(float seconds)
	{
#ifdef NETSIMULATE
		variance = (int)(seconds*1000.0f);
#endif
	}

	void SetRTT(float seconds)
	{
#ifdef NETSIMULATE
		rtt = (int)(seconds*1000.0f);
#endif
	}

	void SetDrop(float frac)
	{
#ifdef NETSIMULATE
		drop = (int)(frac*100.0f);
#endif
	}
private:

#ifdef NETSIMULATE
	int drop;
	int rtt;
	int variance;
#endif

#ifndef _WIN32
	int socket;
#else
	unsigned int socket;
#endif
};

class SocketTCP
{
public:
	int received;

	SocketTCP();
#ifdef _WIN32
	SocketTCP(unsigned int sock);
#else
	SocketTCP(int soc);
#endif

	~SocketTCP();

	bool SetTimeout(int ms);

	bool Connect(const char* address, const char* protocol = "http");

	bool Connect(Address server);

	bool SetNonBlocking(bool enable);

	SocketTCP* Accept(Address& sender);//returns sock if successful, and sets the addr to the user connecting

	bool Listen(unsigned short port);

	bool Open( Address & server, unsigned short port);
	void Close();

	bool IsOpen() const;

	bool Send(const void * data, int size);
	int Receive(void * data, unsigned int size);

private:

	int socket;
};

#endif
