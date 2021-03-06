#ifndef _NET_CHANNEL_
#define _NET_CHANNEL_

#include <queue>
#include <thread>
#include <map>
#include <mutex>

#include "Sockets.h"
#include "NetDefines.h"
#include "NetMsg.h"

//allows proper handling of wrap around
inline int modulus(int x, int m) {
	int r = x%m;
	return r < 0 ? r + m : r;
}

//data structures
struct Packet
{
	bool reliable;
	char* data;
	int size;
};

struct RPacket//reliable packet structure
{
	char* data;
	int size;
	unsigned int sendtime;
	bool recieved;
	unsigned int sequence;
	unsigned short resends;
	int fragment;
	int numfragments;

	//stores ordered sequence and channel
	signed char channel;
	unsigned int channel_sequence;
};

//used to reassemble fragments
struct Fragment
{
	char* data;
	int frags_recieved;
	int num_frags;
	int sequence;
	int curpos;//used for split packets
};

enum NetConstants
{
	NumberAcks = 32,//must be the number of bits in an integer or less
	//the real number of acks per message is one more than this
	NumberWindows = NumberAcks + 1,//dont change this

	MaxUnsentAcks = NumberAcks / 2,//number of unsent acks to build up before forcing a ack packet

	//these must be < 32, as must fit in an int
	ReliableFlagBit = 31,
	FragmentFlagBit = 30,
	OrderedFlagBit = 29,

	MaxSequenceNumber = 33*82, //536870895,//must be a multiple of 33 and less than 2^OrderedFlagBit

	NumberReliableFragments = (NumberWindows+8)/2,//20,//must be >= (numwindows+1)/2

	SplitFlagBit = 15,//must be less than or equal to max bit number of a short (<= 15)
};


bool sequence_more_recent(unsigned int s1, unsigned int s2);

//this handles data sending, needs to be symmetrical, so one on server
//for each client, and one on client
class NetChannel
{
	friend class NetConnection;
	friend class Peer;

	//ping stuff
	unsigned int lastpingtime;
	unsigned int rtt;//in ms

	//need to calculate and store RTT
	//lets add a sequence number to all packets
	//and keep list of last idk, 64 with sequence numbers, send times and a space for recieve time
	//then calculate RTT from those
	std::mutex sendingmutex;//lock this when messing with queues
	std::queue<Packet> sending;
	std::queue<RPacket> reliable_sending;

	bool acks[NumberAcks];//stores if we got last 32 packets
	RPacket window[NumberWindows];//sliding window of reliable packets

	unsigned char split_sequence;
	int sequence, recieved_sequence, last_acked;
	int unsent_acks;

	Fragment unreliable_fragment;//only supports one at a time
	Fragment reliable_frags[NumberReliableFragments];//need to have half the number of these as the number of window frames, so (numwindows+1)/2

	//lets just give 16 channels for now
	//this stores reliable packets for reordering
	int incoming_ordered_sequence[16];
	int outgoing_ordered_sequence[16];//this stores last recieved packet in order on each stream
	//when get complete packet, if one larger than ordered_Sequence, increment it and push to recieve
	//then check ordered_buffer if any packets can now be pushed out, increment if needed
	//if more than one larger, push to ordered_buffer
	std::map<int, Packet> ordered_buffer[16];

	bool server;
	Socket* connection;
public:

	unsigned int lastreceivetime;
	unsigned int lastsendtime;
	int state;

	Address remoteaddr;

	NetChannel()
	{
		this->Init();
	}

	~NetChannel()
	{
		this->Cleanup();
	}

	void Init();

	void Cleanup();

	//todo, lets make this work
	void Send(char* data, int size)
	{
		//ok, we need to make copy of data
		char* tmp = new char[size];
		memcpy(tmp, data, size);

		Packet p;
		p.size = size;
		p.data = tmp;
		p.reliable = false;
		sendingmutex.lock();
		sending.push(p);
		sendingmutex.unlock();
	}

	void Send(char id, char* data, int size)
	{
		char* tmp = new char[size + 1];
		memcpy(tmp + 1, data, size);

		tmp[0] = id;

		Packet p;
		p.size = size + 1;
		p.data = tmp;
		p.reliable = false;
		sendingmutex.lock();
		sending.push(p);
		sendingmutex.unlock();
	}

	void SendOOB(char* data, int size)
	{
		//need to form the packet
		char* tmp = new char[size + 4];
		NetMsg msg(size + 4, tmp);
		msg.WriteInt(-1);//is OOB;
		msg.WriteData(data, size);

		this->connection->Send(this->remoteaddr, tmp, size + 4);
		delete[] tmp;
	}

	void SendReliable(char* data, int size)
	{
		//ok, we need to make copy of data
		char* tmp = new char[size];
		memcpy(tmp, data, size);

		RPacket p;
		p.size = size;
		p.data = tmp;
		p.recieved = false;
		p.sendtime = 0;
		p.sequence = 0;
		p.resends = 0;
		p.fragment = p.numfragments = 0;
		p.channel = -1;//tell system this is not ordered
		sendingmutex.lock();
		reliable_sending.push(p);
		sendingmutex.unlock();
	}

	void SendReliableOrdered(char* data, int size, unsigned char channel = 0)
	{
		char* tmp = new char[size];
		memcpy(tmp, data, size);

		RPacket p;
		p.size = size;
		p.data = tmp;
		p.recieved = false;
		p.sendtime = 0;
		p.resends = 0;
		p.fragment = p.numfragments = 0;
		p.channel = channel;
		p.sequence = 0;//will fill in later
		sendingmutex.lock();
		reliable_sending.push(p);
		sendingmutex.unlock();
	}

	//this calls sendreliables
	void SendPackets();


private:
	void SendReliables();

	//little wrapper to keep track of stuff, use me
	inline void Send(const Address& addr, const char* data, const int size)
	{
		this->lastsendtime = NetGetTime();//keeps track of how often to send keep alives
		this->connection->Send(addr, data, size);
	}

	inline void ProcessAck(int ack, int ackbits)
	{
		//error check
		if (sequence_more_recent(ack, this->sequence))// ack > this->sequence)
		{
			netlog("[NetChan] ERROR: Got ack larger than was ever sent!!\n");
			return;
		}

		//process primary ack number
		if (sequence_more_recent(ack, this->last_acked))//ack > this->last_acked)
		{
			this->last_acked = ack;
			for (int i2 = 0; i2 < NumberWindows; i2++)
			{
				if (window[i2].sequence == ack && window[i2].recieved == false)
				{
#ifdef NET_VERBOSE_DEBUG
					netlogf("[%s] Got ack for %d\n", this->server ? "Server" : "Client", ack);
#endif
					window[i2].recieved = true;
					break;
				}
			}
		}

		//then process the acks
		for (int i = 0; i < NumberAcks; i++)
		{
			int acked = ackbits & 1 << i;
			int id = ack - (NumberAcks - i);
			if (acked && id >= 0)
			{
				for (int i2 = 0; i2 < NumberWindows; i2++)
				{
					if (window[i2].sequence == id && window[i2].recieved == false)
					{
#ifdef NET_VERBOSE_DEBUG
						netlogf("[%s] Got ack for %d\n", this->server ? "Server" : "Client", id);
#endif
						window[i2].recieved = true;
						break;
					}
				}
			}
		}
	}

	int GetAckBits();

	//returns if should use packet, is false if is duplicate
	bool ProcessHeader(char* data, int size);//this processes the header of incoming packets
	void ProcessPacket(char* buffer, int recvsize, std::vector<Packet>& container);//returns char*s of each packet, make sure to delete it
};

#endif