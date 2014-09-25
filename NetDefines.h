#ifndef _NET_DEFINES
#define _NET_DEFINES

#define NET_MAGIC_ID 51296165//identifier for connection request packets
#define NET_PROTOCOL_ID 9001//Change me for your program. Just an integer, so can be pmuch any value
#define PACKET_MTU 1400//not used
#define NET_PING_INTERVAL 500//how often (in milliseconds) a packet is sent automatically if no packet is sent manually
#define NET_FRAGMENT_SIZE 1000//use a header size less than the MTU of the network (header is a maximum of 15 bytes)
#define NET_MAX_FRAGMENTS 1000//1 million byte packet limit, prevents someone using up lots of memory by sending super large split packets

//uncomment these if you want them applied
//#define NET_VERBOSE_DEBUG//prints stuff about packet encoding/decoding


enum class NetCommandPackets: unsigned short
{
	Ping = 95,
	Pong = 96,
	ConnectionRequest = 97,
	Ack = 98,
	Disconnect = 99
};

//debug print functions
void netlog(char* o);
void netlogf(const char* fmt, ...);

void NetSleep(unsigned int ms);
unsigned int NetGetTime();

#endif