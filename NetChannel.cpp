
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#ifdef _DEBUG   
#ifndef DBG_NEW      
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )     
#define new DBG_NEW   
#endif
#endif

#include "NetChannel.h"

void NetChannel::Cleanup()
{
	for (int i = 0; i < 33; i++)
	{
		if (this->window[i].data && (this->window[i].fragment + 1) == this->window[i].numfragments)
			delete[] this->window[i].data;

		this->window[i].data = 0;
	}

	//clean up fragments
	for (auto& i: this->reliable_frags)
	{
		if (i.data)
			delete[] i.data;
		i.data = 0;
	}

	if (this->unreliable_fragment.data)
		delete[] this->unreliable_fragment.data;
	this->unreliable_fragment.data = 0;

	while (this->reliable_sending.empty() == false)
	{
		delete[] this->reliable_sending.front().data;
		this->reliable_sending.pop();
	}

	while (this->sending.empty() == false)
	{
		delete[] this->sending.front().data;
		this->sending.pop();
	}
}

int NetChannel::GetAckBits()
{
	int bits = 0;
	for (int i = 0; i < 32; i++)
	{
		if (acks[i])
		{
			bits |= 1<<i;
		}
	}
	return bits;
}

void NetChannel::ProcessPacket(char* buffer, int recvsize, std::vector<Packet>& container)
{
	NetMsg msg(2048,buffer);
	int rsequence = msg.ReadInt();//sequence
	int reliable = rsequence & (1<<31);
	bool shoulduse = this->ProcessHeader(buffer, recvsize);
	if (shoulduse == false)
	{
		//netlog("[Client] Got duplicate or really old packet, dropping!\n");
		return;
	}

	if (rsequence == -1)//OOB
	{
		netlog("[Client] Got OOB packet\n");
		Packet p;
		//make copy of data
		//char* copy = new char[recvsize - 4];
		//memcpy(copy, &buffer[4], recvsize - 4);
		p.size = recvsize - 4;
		p.data = new char[p.size];
		memcpy(p.data, &buffer[4], p.size);
		container.push_back(p);
	}
	else if (reliable == false)
	{
		netlogf("[%s] Decoding Packet %d bytes\n", this->server ? "Server" : "Client", recvsize);

		//netlog("Got Message\n");
		int ackbits = msg.ReadInt();//this is ackbits

		int ptr = 8;//points to size of sub-packet
		while (ptr < recvsize)//can have multiple packets
		{
			unsigned short packetsize = *(unsigned short*)&buffer[ptr];
			//packetsize = msg.ReadInt();

			//todo: fragmenting
			if (packetsize & 1<<15)
			{
				packetsize &= ~(1<<15);
				NetMsg msg2 = NetMsg(2048, &buffer[ptr+2]);

				byte sequence = msg2.ReadByte();
				byte frag = msg2.ReadByte();
				byte numfrags = msg2.ReadByte();

				netlogf("	Got unreliable split packet %d of %d\n", frag+1, numfrags);
				if (sequence == this->unreliable_fragment.sequence)
				{
					if (frag == this->unreliable_fragment.frags_recieved)//it is next packet in set
					{
						this->unreliable_fragment.frags_recieved++;
						//msg.ReadData(&this->unreliable_fragment.data[this->unreliable_fragment.curpos], packetsize);

						memcpy(&this->unreliable_fragment.data[this->unreliable_fragment.curpos],&buffer[ptr+5],packetsize);
						this->unreliable_fragment.curpos += packetsize;
					}
				}
				else//make sure its newer
				{
					if (this->unreliable_fragment.data)
						delete[] this->unreliable_fragment.data;

					this->unreliable_fragment.data = new char[numfrags*FRAGMENT_SIZE];
					this->unreliable_fragment.sequence = sequence;
					this->unreliable_fragment.frags_recieved = 1;
					this->unreliable_fragment.curpos = packetsize;
					//msg.ReadData(this->unreliable_fragment.data, packetsize);
					memcpy(this->unreliable_fragment.data, &buffer[ptr+5], packetsize);
				}

				ptr += 5+packetsize;

				if(frag+1 == numfrags)
				{
					//we should be done
					if (this->unreliable_fragment.frags_recieved == numfrags)
					{
						netlog("		Was final packet in split unreliable set.\n");
						//return this->unreliable_fragment.data;
						Packet p;
						p.reliable = false;
						p.size = this->unreliable_fragment.curpos;
						p.data = this->unreliable_fragment.data;
						this->unreliable_fragment.data = 0;
						container.push_back(p);
					}
				}
			}
			else
			{
				netlogf("	Got packet of %d bytes at %d\n", packetsize, ptr);
				Packet p;
				p.reliable = false;
				p.size = packetsize;
				//msg.cursize += packetsize;
				p.data = new char[p.size];
				memcpy(p.data,&buffer[ptr+2], p.size);
				container.push_back(p);

				ptr += 2+packetsize;
			}
		}
	}
	else
	{
		//check if the packet is complete if fragmented
		rsequence &= ~(1<<31);
		if (rsequence & 1<<29)
		{
			netlog("was ordered reliable packet\n");
		}
		if (rsequence & (1<<30))
		{
			rsequence &= ~(1<<30);
			//check if we have whole packet, if not continue

			if (rsequence & 1<<29)
			{
				rsequence &= ~(1<<29);
				unsigned char chan = *(unsigned char*)&buffer[12];
				unsigned short seq = *(unsigned short*)&buffer[13];
				int frag = *(unsigned short*)&buffer[15];
				int numfrags = *(unsigned short*)&buffer[17];

				netlog("was ordered split reliable packet\n");

				netlogf("[Client] Got reliable split packet %d of %d\n", frag+1, numfrags);

				//we need to copy data into a buffer
				int startseq = rsequence - frag;
				if (reliable_frags[startseq%20].sequence == startseq)
				{
					reliable_frags[startseq%20].frags_recieved += 1;
				}
				else
				{
					//delete the old one
					//if (reliable_frags[startseq%20].data)
					//delete[] reliable_frags[startseq%20].data;

					//first packet we got in the set
					reliable_frags[startseq%20].frags_recieved = 1;
					reliable_frags[startseq%20].data = new char[FRAGMENT_SIZE*numfrags];
					reliable_frags[startseq%20].sequence = startseq;
					reliable_frags[startseq%20].curpos = 0;
				}

				//ok, copy in the data
				memcpy(reliable_frags[startseq%20].data+frag*FRAGMENT_SIZE, &buffer[16+3], recvsize - 19);
				reliable_frags[startseq%20].curpos += recvsize - 19;

				if (reliable_frags[startseq%20].frags_recieved == numfrags)
				{
					netlog("[Client] We got the whole split reliable packet\n");
					Packet p;
					p.reliable = true;
					p.size = reliable_frags[startseq%20].curpos;//frags_recieved*(FRAGMENT_SIZE-1) + (recvsize - 16);//reliable_frags[startseq%20].frags_recieved*FRAGMENT_SIZE;//approximate
					p.data = reliable_frags[startseq%20].data;
					reliable_frags[startseq%20].data = 0;
					//container.push_back(p);

					if (this->incoming_ordered_sequence[chan] + 1 == seq)
					{
						this->incoming_ordered_sequence[chan]++;
						netlogf("Pushed ordered pack %d\n", incoming_ordered_sequence[chan]);
						container.push_back(p);

						//check map to see if next packet is availible
						while (this->ordered_buffer[chan].find(this->incoming_ordered_sequence[chan]+1) != this->ordered_buffer[chan].end())
						{
							container.push_back(this->ordered_buffer[chan][this->incoming_ordered_sequence[chan]+1]);
							this->ordered_buffer[chan].erase(this->ordered_buffer[chan].find(this->incoming_ordered_sequence[chan]+1));

							this->incoming_ordered_sequence[chan]++;
							netlogf("Pushed ordered pack %d\n", incoming_ordered_sequence[chan]);
						}
					}
					else
					{
						//hold it for a while until we get packets before it
						this->ordered_buffer[chan][seq] = p;
					}
				}
			}
			else
			{
				int frag = *(unsigned short*)&buffer[12];
				int numfrags = *(unsigned short*)&buffer[14];

				netlogf("[Client] Got reliable split packet %d of %d\n", frag+1, numfrags);

				//we need to copy data into a buffer
				int startseq = rsequence - frag;
				if (reliable_frags[startseq%20].sequence == startseq)
				{
					reliable_frags[startseq%20].frags_recieved += 1;
				}
				else
				{
					//delete the old one
					//if (reliable_frags[startseq%20].data)
					//delete[] reliable_frags[startseq%20].data;

					//first packet we got in the set
					reliable_frags[startseq%20].frags_recieved = 1;
					reliable_frags[startseq%20].data = new char[FRAGMENT_SIZE*numfrags];
					reliable_frags[startseq%20].sequence = startseq;
					reliable_frags[startseq%20].curpos = 0;
				}

				//ok, copy in the data
				memcpy(reliable_frags[startseq%20].data+frag*FRAGMENT_SIZE, &buffer[16], recvsize - 16);
				reliable_frags[startseq%20].curpos += recvsize - 16;

				if (reliable_frags[startseq%20].frags_recieved == numfrags)
				{
					netlog("[Client] We got the whole split reliable packet\n");
					Packet p;
					p.reliable = true;
					p.size = reliable_frags[startseq%20].curpos;//frags_recieved*(FRAGMENT_SIZE-1) + (recvsize - 16);//reliable_frags[startseq%20].frags_recieved*FRAGMENT_SIZE;//approximate
					p.data = reliable_frags[startseq%20].data;
					reliable_frags[startseq%20].data = 0;
					container.push_back(p);
				}
			}
		}
		else
		{
			if (rsequence & 1<<29)
			{
				netlog("[NetChan] Got Ordered Reliable Packet\n");
				//size at 15
				//chan at 12
				//seq at 13
				unsigned short seq = *(unsigned short*)&buffer[13];
				unsigned char chan = *(unsigned char*)&buffer[12];

				Packet p;
				p.reliable = true;
				p.size = recvsize - (14+3);

				//lets make copy
				p.data = new char[p.size];
				memcpy(p.data, &buffer[14+3], p.size);
				if (this->incoming_ordered_sequence[chan] + 1 == seq)
				{
					this->incoming_ordered_sequence[chan]++;
					netlogf("Pushed ordered pack %d\n", incoming_ordered_sequence[chan]);
					container.push_back(p);

					//check map to see if next packet is availible
					while (this->ordered_buffer[chan].find(this->incoming_ordered_sequence[chan]+1) != this->ordered_buffer[chan].end())
					{
						container.push_back(this->ordered_buffer[chan][this->incoming_ordered_sequence[chan]+1]);
						this->ordered_buffer[chan].erase(this->ordered_buffer[chan].find(this->incoming_ordered_sequence[chan]+1));

						this->incoming_ordered_sequence[chan]++;
						netlogf("Pushed ordered pack %d\n", incoming_ordered_sequence[chan]);
					}
				}
				else
				{
					//hold it for a while until we get packets before it
					this->ordered_buffer[chan][seq] = p;
				}
			}
			else
			{
				netlog("[Client] Got Reliable packet\n");

				//not split packet, or ordered
				Packet p;
				p.reliable = true;
				p.size = recvsize - 14;
				p.data = new char[p.size];
				memcpy(p.data,&buffer[14], p.size);
				container.push_back(p);
			}
		}
	}
};

bool NetChannel::ProcessHeader(char* data, int size)//this processes the header of incoming packets
{
	int rsequence = *(int*)&data[0];
	int ack, ackbits;
	if (rsequence == -1)
	{//is OOB packet
		//do nothing with OOB packets
		return true;
	}
	else if (rsequence & (1<<31))
	{
		//record that we got it
		rsequence &= ~(1<<31);
		rsequence &= ~(1<<30);//get rid of fragment flag
		rsequence &= ~(1<<29);//get rid of ordered flag

		//lets fill acks
		if (rsequence > this->recieved_sequence)
		{
			int diff = rsequence - this->recieved_sequence;
			this->recieved_sequence = rsequence;

			//ok, roll down the bits
			for (int c = 0; c < diff; c++)
			{
				for (int i = 1; i < 32; i++)
					this->acks[i-1] = this->acks[i];

				if (c == 0)
					acks[31] = true;
				else
					acks[31] = false;
			}
			this->unsent_acks++;
		}
		else if (rsequence < this->recieved_sequence)
		{
			//either old or resent
			int id = 32 - (this->recieved_sequence - rsequence);

			if (id >= 0)
			{
				if (this->acks[id])
					return false;//we already acked this, dont do anything with it

				//netlogf("[NetCon] Got reliable resend packet %d\n", id);
				netlogf("[%s] Got reliable resend packet %d\n", this->server ? "Server" : "Client", rsequence);
				this->unsent_acks++;
				this->acks[id] = true;
			}
			else
			{
				netlog("[NetCon] Got Superold Packet, dropping!\n");
				return false;//we acked this long time ago, drop
			}
		}
		else
			return false;//we already acked it

		ack = *(int*)&data[4];
		ackbits = *(int*)&data[8];
	}
	else
	{
		ack = rsequence;
		ackbits = *(int*)&data[4];
		//first 4, last received sequence
		//next 4 ack bits
		//next 4 size (can probably change to unsigned short)
	}

	this->ProcessAck(ack, ackbits);

	return true;
}

void NetChannel::SendPackets()//actually sends to the server
{
	//PROFILE("SendPackets")
	this->SendReliables();
	if (this->sending.empty() == true)
		return;

	this->lastsendtime = GetTickCount();

	int ptr = 0;
	int frag = 0;
	int numfrags = 0;
	while(this->sending.empty() == false)
	{
		netlogf("[%s] Building Packet\n", this->server ? "Server" : "Client");
		char d[2056];
		NetMsg msg(2056,d);

		int wseq = this->recieved_sequence;
		wseq &= ~(1<<31);//set as not reliable
		msg.WriteInt(wseq);
		msg.WriteInt(this->GetAckBits());//then write bits with last sequences

		//ok, pack in the data, make sure we dont pack too much
		while(this->sending.empty() == false)
		{
			Packet pack = this->sending.front();
			if (ptr != 0)
			{
				//put in fragment
				int left2send = pack.size - ptr;
				int size = FRAGMENT_SIZE;
				if (left2send < FRAGMENT_SIZE)
					size = left2send;
				msg.WriteShort(size | (1<<15));//msb high to signal split data
				msg.WriteByte(this->split_sequence);
				msg.WriteByte(frag);
				msg.WriteByte(numfrags);
				msg.WriteData(&pack.data[ptr], size);


				netlogf("	Inserting Fragment size %d at %d\n", size, ptr);

				ptr += FRAGMENT_SIZE;

				frag++;

				if (frag == numfrags)
				{
					this->split_sequence++;
					ptr = 0;
					delete[] pack.data;
					this->sending.pop();
				}
				else
					break;//only one fragment per packet
			}
			else if ((pack.size + msg.cursize - 8) <= FRAGMENT_SIZE)
			{
				netlogf("	Inserting Packet size %d at %d\n", pack.size, msg.cursize);
				//write size of packet
				//netlog("[NetCon] Sent unreliable message.\n");
				msg.WriteShort(pack.size & ~(1<<15));//msb low to signal unfragmented data
				msg.WriteData(pack.data, pack.size);

				delete[] pack.data;
				this->sending.pop();
			}
			else
			{
				//this is broken atm, would provide better packing
				if ((pack.size) > FRAGMENT_SIZE && msg.cursize == 8)//dont put half a packet if we can avoid it
				{
					//netlogf("[%s] Unreliable message too large (%d bytes), need to split.\n", this->server ? "Server" : "Client", pack.size);
					//lets just fill the rest of this packet, and pop out a new one
					int sizeleft = FRAGMENT_SIZE - (msg.cursize-8);//dont count header bits
					numfrags = pack.size/FRAGMENT_SIZE+1;//should be generally right
					if ((sizeleft + (numfrags-1)*FRAGMENT_SIZE) < pack.size)
						numfrags++;

					//use size bits to store id, change size bits to ushort, no reason for longer
					msg.WriteShort(sizeleft | (1<<15));//msb high to signal split data
					msg.WriteByte(this->split_sequence);
					msg.WriteByte(frag);
					msg.WriteByte(numfrags);
					msg.WriteData(pack.data, sizeleft);

					netlogf("	Inserting First Fragment size %d (%d bytes for all frags)\n", sizeleft, pack.size);


					ptr = sizeleft;
					frag = 1;
				}
				else
					netlogf("[%s] Unreliable message full, have to send another one.\n", this->server ? "Server" : "Client");
				break;
			}
		}
		netlogf("[%s] Send Unreliable Payload of %d bytes.\n", this->server ? "Server" : "Client", msg.cursize);
		this->unsent_acks = 0;
		this->connection->Send(this->remoteaddr, msg.data, msg.cursize);
	}
}//actually does the networking

void NetChannel::SendReliables()//actually sends to the server
{
	//ok, lets check if we need to resend anything
	bool recieved = false;
	for (int i = 32; i >= 0; i--)
	{ 
		if (window[i].recieved == false && window[i].data)
		{
			//if we got a packet after this one and still havent
			//recieved, we should resend
			if ((recieved == true && window[i].resends == 0) || (window[i].sendtime + 1000 < GetTickCount()))
			{
				this->lastsendtime = GetTickCount();

				//netlogf("[%s] Resending sequence %d\n", this->server ? "Server" : "Client", window[i].sequence);

				bool fragmented = false;
				if (window[i].numfragments > 1)
					fragmented = true;

				//resend
				char d[2056];
				NetMsg msg(2056,d);
				int seq = window[i].sequence;//this->sequence;
				//todo, handle sequence number overflow//seq &= ~(1<<31);//this is not OOB
				seq |= 1<<31;//this is for if reliable;

				if (fragmented)
					seq |= 1<<30;//this signifies split packet

				if (window[i].channel != -1)
					seq |= 1<<29;

				//dont send this if not reliable
				msg.WriteInt(seq);//if MSB bit high, then fragmented 

				msg.WriteInt(recieved_sequence);
				msg.WriteInt(this->GetAckBits());//then write bits with last recieved sequences

				if (window[i].channel != -1)
				{
					msg.WriteByte(window[i].channel);
					msg.WriteShort(window[i].channel_sequence);
				}

				if (fragmented)
				{
					msg.WriteShort(window[i].fragment);
					msg.WriteShort(window[i].numfragments);

					msg.WriteData(window[i].data+window[i].fragment*FRAGMENT_SIZE, (window[i].size - window[i].fragment*FRAGMENT_SIZE) < FRAGMENT_SIZE ? window[i].size - window[i].fragment*FRAGMENT_SIZE : FRAGMENT_SIZE);
				}
				else
				{
					msg.WriteShort(window[i].size);
					msg.WriteData(window[i].data, window[i].size);
				}

				window[i].sendtime = GetTickCount()-500;//resend faster
				window[i].resends += 1;

				this->unsent_acks = 0;
				this->connection->Send(this->remoteaddr, msg.data, msg.cursize);
			}
		}
		else if (window[i].recieved == true && window[i].data)
		{
			recieved = true;
		}
	}

	if (this->reliable_sending.empty() == true)
		return;

	if (this->sequence == 0)
		this->sequence = 1;


	bool fragmented = false;
	int numfrags = 1;
	if (this->reliable_sending.front().size > FRAGMENT_SIZE)
	{
		numfrags = this->reliable_sending.front().size/FRAGMENT_SIZE + 1;
		fragmented = true;
	}
	int fragment = 0;
	int mw = modulus(this->sequence-1,33);
	//check if we are in the middle of sending packet
	if (window[mw].data && window[mw].fragment + 1 != window[mw].numfragments)
	{
		netlog("we were in the middle of sending split packet, try and send more\n");
		fragment = window[mw].fragment+1;
	}

	for (; fragment < numfrags; fragment++)
	{
		//check if we can slide the window
		int zw = modulus(this->sequence - 32, 33);//(this->sequence - 33) % 33;
		mw = modulus(this->sequence, 33);

		if (window[zw].recieved == true || window[zw].data == 0)
		{
			if (window[zw].data && window[zw].fragment+1 == window[zw].numfragments)//delete old data
			{
				delete[] window[zw].data;

				window[zw].data = 0;
			}
		}
		else if (window[zw].data)//ok, we cant let this break on us
		{
			//netlog("[NetCon] Couldn't send packet because sending old one\n");
			return;//we havent gotten ack on 31st packet, so cant slide
		}

		this->lastsendtime = GetTickCount();

		char d[2056];
		NetMsg msg(2056,d);
		int seq = this->sequence;
		//todo, handle sequence number overflow//seq &= ~(1<<31);//this is not OOB
		seq |= 1<<31;//this is for if reliable;
		if (fragmented)
			seq |= 1<<30;//this signifies split packet
		if (this->reliable_sending.front().channel != -1)
			seq |= 1<<29;

		//dont send this if not reliable
		msg.WriteInt(seq);//if MSB bit high, then fragmented 

		msg.WriteInt(recieved_sequence);
		msg.WriteInt(this->GetAckBits());//then write bits with last recieved sequences

		if (this->reliable_sending.front().channel != -1)
		{
			msg.WriteByte(this->reliable_sending.front().channel);
			msg.WriteShort(this->outgoing_ordered_sequence[this->reliable_sending.front().channel]);
		}

		if (fragmented)
		{
			msg.WriteShort(fragment);
			msg.WriteShort(numfrags);
		}

		//fill out latest window slot
		window[mw].recieved = false;
		window[mw].data = this->reliable_sending.front().data;
		window[mw].size = this->reliable_sending.front().size;
		window[mw].sendtime = GetTickCount();
		window[mw].sequence = this->sequence;
		window[mw].resends = 0;
		window[mw].fragment = fragment;
		window[mw].numfragments = numfrags;
		window[mw].channel = this->reliable_sending.front().channel;
		if (window[mw].channel != -1)
			window[mw].channel_sequence = this->outgoing_ordered_sequence[window[mw].channel];

		if (window[mw].fragment == numfrags-1 || numfrags == 1)
		{
			this->outgoing_ordered_sequence[window[mw].channel]++;
		}

		//ok, pack in the data, make sure we dont pack too much
		while(this->reliable_sending.empty() == false)
		{
			RPacket pack = this->reliable_sending.front();
			//write size of packet
			if (fragmented)
			{
				msg.WriteData(pack.data+fragment*FRAGMENT_SIZE, (pack.size - fragment*FRAGMENT_SIZE) < FRAGMENT_SIZE ? pack.size - fragment*FRAGMENT_SIZE : FRAGMENT_SIZE);
			}
			else
			{
				msg.WriteShort(pack.size);
				msg.WriteData(pack.data, pack.size);
			}

			break;//only send 1 for now
		}

		this->unsent_acks = 0;
		this->connection->Send(this->remoteaddr, msg.data, msg.cursize);

		this->sequence++;//increment sequence number
	}
	if (window[modulus(this->sequence - 1, 33)].fragment+1 == numfrags)
		this->reliable_sending.pop();//pop if finished with packet
}//actually does the networking