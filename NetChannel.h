#ifndef _NET_CHANNEL_
#define _NET_CHANNEL_

#include <queue>
#include <thread>
#include <map>

#include "Sockets.h"
#include "NetDefines.h"
#include "NetMsg.h"

//allows proper handling of wrap around
inline int modulus(int x, int m) {
	int r = x%m;
	return r<0 ? r+m : r;
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
	char channel;
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

//this handles data sending, needs to be symmetrical, so one on server
//for each client, and one on client
class NetChannel
{
	friend class NetConnection;

	//need to calculate and store RTT
	//lets add a sequence number to all packets
	//and keep list of last idk, 64 with sequence numbers, send times and a space for recieve time
	//then calculate RTT from those
	std::queue<Packet> sending;
	std::queue<RPacket> reliable_sending;

	bool acks[32];//stores if we got last 32 packets
	RPacket window[33];//sliding window of reliable packets

	unsigned char split_sequence;
	int sequence, recieved_sequence, last_acked;
	int unsent_acks;

	Fragment unreliable_fragment;//only supports one at a time
	Fragment reliable_frags[20];//need to have half the number of these as the number of window frames, so (numwindows+1)/2


	//lets just give 16 channels for now
	//this stores reliable packets for reordering
	int incoming_ordered_sequence[16];
	int outgoing_ordered_sequence[16];//this stores last recieved packet in order on each stream
	//when get complete packet, if one larger than ordered_Sequence, increment it and push to recieve
		//then check ordered_buffer if any packets can now be pushed out, increment if needed
	//if more than one larger, push to ordered_buffer
	std::map<int,Packet> ordered_buffer[16];

public:

	int lastreceivetime;
	int lastsendtime;
	int state;

	bool server;
	Socket* connection;

	Address remoteaddr;

	NetChannel()
	{
		this->Init();
	}

	~NetChannel()
	{
		this->Cleanup();
	}

	void Init()
	{
		this->unsent_acks = 0;
		this->lastsendtime = 0;
		this->server = false;
		this->sequence = this->last_acked = this->recieved_sequence = this->split_sequence = 0;
		for (int i = 0; i < 33; i++)
		{
			this->window[i].data = 0;
			this->window[i].sendtime = 0;
			this->window[i].size = 0;
			this->window[i].recieved = false;

			if (i < 32)
				this->acks[i] = false;
		}

		for (int i = 0; i < 16; i++)
		{
			this->incoming_ordered_sequence[i] = -1;
			this->outgoing_ordered_sequence[i] = 0;
		}

		this->unreliable_fragment.data = 0;
		this->unreliable_fragment.sequence = -1;

		for (auto& i: this->reliable_frags)
		{
			i.data = 0;
			i.sequence = 0;
			i.frags_recieved = 0;
		}
	}

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
		sending.push(p);
	}
	//this drops duplicate and out of order packets
	/*void SendOrdered(char* data, int size, unsigned char channel = 0)
	{
	//ok, we need to make copy of data
	char* tmp = new char[size];
	memcpy(tmp, data, size);

	Packet p;
	p.size = size;
	p.data = tmp;
	p.ordered = true;
	sending.push(p);
	}*/
	void SendOOB(char* data, int size)
	{
		//need to form the packet
		char* tmp = new char[size+4];
		NetMsg msg(size+4,tmp);
		msg.WriteInt(-1);//is OOB;
		msg.WriteData(data, size);

		this->connection->Send(this->remoteaddr, tmp, size+4);
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
		reliable_sending.push(p);
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
		p.sequence = 0;
		p.resends = 0;
		p.fragment = p.numfragments = 0;
		p.channel = channel;
		p.sequence = 0;//will fill in later
		reliable_sending.push(p);
	}

	void SendReliables();

	//if you call this, you dont need to call send reliables
	void SendPackets();

private:

	//little wrapper to keep track of stuff, use me
	inline void Send(const Address& addr, const char* data, const int size)
	{
		this->lastsendtime = NetGetTime();//keeps track of how often to send keep alives
		this->connection->Send(addr, data, size);
	}

	inline void ProcessAck(int ack, int ackbits)
	{
		//error check
		if (ack > this->sequence)
		{
			netlog("[NetChan] ERROR: Got ack larger than was ever sent!!\n");
			return;
		}

		//process primary ack number
		if (ack > this->last_acked)
		{
			this->last_acked = ack;
			for (int i2 = 0; i2 < 33; i2++)
			{
				if (window[i2].sequence == ack && window[i2].recieved == false)
				{
					//netlogf("[%s] Got ack for %d\n", this->server ? "Server" : "Client", ack);
					window[i2].recieved = true;
					break;
				}
			}
		}

		//then process the acks
		for (int i = 0; i < 32; i++)
		{
			int acked = ackbits & 1<<i;
			int id = ack - (32-i);
			if (acked && id > 0)
			{
				for (int i2 = 0; i2 < 33; i2++)
				{
					if (window[i2].sequence == id && window[i2].recieved == false)
					{
						//netlogf("[%s] Got ack for %d\n", this->server ? "Server" : "Client", id);
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