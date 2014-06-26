#ifndef _NET_DEFINES
#define _NET_DEFINES

#define MAGIC_ID 51296165
#define PROTOCOL_ID 9001//just an integer, so can be pmuch any value
#define PACKET_MTU 1400//not used
#define PING_INTERVAL 500//not used
#define NET_FRAGMENT_SIZE 1000//use a header size less than the MTU of the network (header is a maximum of 15 bytes)
#define NET_MAX_FRAGMENTS 1000//1 million byte packet limit, prevents someone using up lots of memory by sending super large split packets


void netlog(char* o);
void netlogf(const char* fmt, ...);

#endif