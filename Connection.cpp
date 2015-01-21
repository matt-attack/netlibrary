
#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "Connection.h"

#ifdef _DEBUG   
#ifndef DBG_NEW      
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )     
#define new DBG_NEW   
#endif
#endif

#define _CRTDBG_MAP_ALLOC

void NetConnection::net_thread(void* data)
{
	netlog("Network thread started!\n");
	NetConnection* connection = (NetConnection*)data;
	char buffer[2048];
	while(connection->running)
	{
		connection->peerincomingmutex.lock();

		//for each client, read in all packets
		int size;
		Address sender;
		Peer* client;
		while ((size = connection->connection.Receive(sender, buffer, sizeof(buffer))) > 0)
		{
			//ok, check for connect and disconnect
			if (connection->peers.find(sender) != connection->peers.end())
			{
				client = connection->peers[sender];

				if (client->state == PEER_CONNECTING)
				{
					//we got our connection response
					netlog("Got Connection request response!\n");

					//parse the packet, ensure it is correct
					NetMsg msg(2048, buffer);
					int seq = msg.ReadInt();//should be -1
					int magic = msg.ReadInt();

					if (magic == NET_MAGIC_ID && seq == -1)
					{
						unsigned char status = msg.ReadByte();
						if (status == 1)
						{
							netlog("all good, got connection success response\n");

							unsigned short port = msg.ReadShort();

							//then mark as connected
							client->state = PEER_CONNECTED;
						}
						else
						{
							netlog("Got Connection Denied response!\n");

							char reason[500];
							msg.ReadString(reason, 500);

							//check and read error message if is valid at all
							netlogf("Reason was: %s\n", reason);
							client->state = PEER_DISCONNECTED;//tell the waitloop we were denied
						}
					}
					else
					{
						netlog("it was a bad response!\n");//just ignore these
					}

					continue;//done with this packet
				}

				client->lastreceivetime = NetGetTime();

				int rsequence = *(int*)&buffer[0];
				if (rsequence == -1)
				{//is OOB packet
					//read in command packets
					if (buffer[4] == (unsigned char)NetCommandPackets::Disconnect)//packetid of 99 is disconnect
					{
						if (client->state != PEER_DISCONNECTED)
						{
							client->state = PEER_DISCONNECTED;
							TPacket p;
							p.data = 0;
							//memcpy(p.data, buffer+4, size-4);
							p.size = 0;
							p.sender = client;
							p.addr = sender;
							p.id = 2;
							connection->incoming.push(p);
						}
						//netlogf("Client from %d.%d.%d.%d:%d Disconnected\n", sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort());

						//disconnect
						//connection->OnDisconnect(client);

						//client->connection.Cleanup();

						//delete client;

						//connection->peers.erase(connection->peers.find(sender));//delete me
					}
					else if (buffer[4] == (unsigned char)NetCommandPackets::Ack)
					{
						//netlog("[NetCon] got keep alive/ack packet\n");

						int ack = *(int*)&buffer[5];
						int ackbits = *(int*)&buffer[9];
						client->ProcessAck(ack, ackbits);
					}
					else if (buffer[4] == (unsigned char)NetCommandPackets::Ping)
					{
						char buffer2[100];
						NetMsg msg(100, buffer2);
						msg.WriteByte((unsigned char)NetCommandPackets::Pong);

						//write acks
						unsigned int time = *(unsigned int*)&buffer[5];
						msg.WriteLong(time);

						client->SendOOB(buffer2, msg.cursize);
					}
					else if (buffer[4] == (unsigned char)NetCommandPackets::Pong)
					{
						unsigned int time = *(unsigned int*)&buffer[5];
						unsigned int rtt = NetGetTime() - time;
						client->rtt = (client->rtt*2 + rtt*2)/4;
						//printf("RTT is: %dms Avg: %dms\n", rtt, client->connection.rtt);
					}
					else if (buffer[4] == (unsigned char)NetCommandPackets::ConnectionRequest)//is another connection request packet, ignore it)
					{
						netlog("[NetCon] Got connection request packet while still connected\n");
					}
					else
					{
						netlog("[NetCon] Got Misc OOB Packet\n");
						//other oob packet
						TPacket p;
						p.data = new char[size-4];
						memcpy(p.data, &buffer[4], size-4);
						p.size = size-4;
						p.id = 0;
						p.sender = client;
						connection->incoming.push(p);
					}

					continue;
				}

				std::vector<Packet> returns;
				client->ProcessPacket(buffer, size, returns);

				//push all packets to the return queue
				for (unsigned int i = 0; i < returns.size(); i++)
				{
					TPacket p;
					auto pak = returns[i];
					p.data = pak.data;
					p.size = pak.size;
					p.id = 0;
					p.sender = client;
					connection->incoming.push(p);
				}
			}
			else
			{
				//new connection
				/* just use username at first, then add passwords eventually */
				ConnectionRequest* p = (ConnectionRequest*)(buffer+4);
				if (p->packid != (unsigned char)NetCommandPackets::ConnectionRequest)
				{
					netlog("[NetCon] Got invalid connection request!\n");
					continue;
				}

				char* msg = connection->CanConnect(sender, p);
				if (msg == 0)//no error message
				{
					//its all good
					netlogf( "Client Connected from %d.%d.%d.%d:%d, added to queue\n", sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort());
					/*Peer* peer = new Peer;
					peer->connection.remoteaddr = sender;
					peer->connection.Init();
					peer->connection.server = true;
					peer->connection.connection = connection->connection;

					//send reply
					char t[500];
					NetMsg msg(500,t);
					msg.WriteInt(-1);
					msg.WriteInt(51296165);//magic
					msg.WriteShort(5007);//my port number, todo, changeme
					peer->connection.SendOOB(&t[4], 5);

					//move to a message queue like system
					//connection->OnConnect(peer, p);

					connection->peers[sender] = peer;*/

					TPacket p;
					p.data = new char[size-4];
					memcpy(p.data, buffer+4, size-4);
					p.size = size-4;
					p.sender = 0;
					p.addr = sender;
					p.id = 1;
					connection->incoming.push(p);
				}
				else
				{
					if (strcmp(msg, "Bad Protocol ID") == 0)
						netlogf("Unknown unconnected packet from %i.%i.%i.%i:%i!\n",sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort());
					else
					{
						netlogf("Client at %i.%i.%i.%i:%i could not join because '%s'\n", sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort(), msg);

						//send reason to client
						char buf[150];
						NetMsg nmsg = NetMsg(150, buf);
						nmsg.WriteInt(-1);//OOB
						nmsg.WriteInt(NET_MAGIC_ID);//id
						nmsg.WriteByte(69);//tell it we failed
						nmsg.WriteString(msg);
						connection->connection.Send(sender, nmsg.data, nmsg.cursize);
					}
				}
			}
		}
		
		//send queued packets
		connection->SendPackets();

		//do keep alives and handle timeouts
		for (auto ii: connection->peers)
		{
			unsigned int lastsendtime = ii.second->lastsendtime;
			if ((lastsendtime + NET_PING_INTERVAL < NetGetTime() || ii.second->unsent_acks > 16) && ii.second->state == PEER_CONNECTED)
			{
				//send keep alive packet, with acks
				//if (ii.second->connection.unsent_acks > 16)
				//netlog("we got lots of unacked packets, sending keepalive/ack\n");
				//else
				//netlog("it has been a while since packet last sent, sending keepalive/ack\n");

				ii.second->unsent_acks = 0;

				char buffer[500];
				NetMsg msg(500, buffer);
				msg.WriteByte((unsigned char)NetCommandPackets::Ack);

				//write acks
				msg.WriteInt(ii.second->recieved_sequence);
				msg.WriteInt(ii.second->GetAckBits());

				ii.second->SendOOB(buffer, msg.cursize);

				ii.second->lastsendtime = NetGetTime();
			}

			if (ii.second->lastpingtime + NET_PING_INTERVAL < NetGetTime() && ii.second->state == PEER_CONNECTED)
			{
				ii.second->lastpingtime = NetGetTime();

				char buffer[100];
				NetMsg msg(100, buffer);
				msg.WriteByte((unsigned char)NetCommandPackets::Ping);

				//write acks
				msg.WriteLong(NetGetTime());

				ii.second->SendOOB(buffer, msg.cursize);
			}

			if (ii.second->lastreceivetime < (NetGetTime() - connection->timeout < 0 ? 0 : NetGetTime() - connection->timeout) && ii.second->state == PEER_CONNECTED)
			{
				//connection timed out
				Peer* client = ii.second;
				Address sender = ii.second->remoteaddr;

				netlogf("Client from %d.%d.%d.%d:%d Timed Out\n", sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort());

				client->state = PEER_DISCONNECTED;
				//disconnect
				//connection->OnDisconnect(client);

				//client->connection.Cleanup();

				//delete client;

				//connection->peers.erase(connection->peers.find(sender));//delete me

				TPacket p;
				p.data = 0;
				p.size = 0;
				p.sender = client;
				p.addr = sender;
				p.id = 2;
				connection->incoming.push(p);
			}
		}


		//update stats
		if (connection->lastupdate + 500 < NetGetTime())
		{
			connection->lastupdate = NetGetTime();

			int totalout = 0; 
			//need to sum up totals for all connections
			for (auto ii: connection->peers)
			{
				//ok, poll the connection
				totalout += ii.second->connection->sent;
				ii.second->connection->sent = 0;
				break;
			}
			connection->sent += totalout;

			int deltain = connection->connection.received - connection->lastreceived;
			int deltaout = totalout;//connection->connection.sent - connection->lastsent;
			//int deltatcp = connection->data_connection.received - connection->lasttcp;

			connection->datarate = 0.75f*connection->datarate + 0.25f*((float)deltain*2);
			connection->outdatarate = 0.75f*connection->outdatarate + 0.25f*((float)deltaout*2);
			//connection->tcprate = 0.75f*connection->tcprate + 0.25f*((float)deltatcp*2);

			//connection->lasttcp = connection->data_connection.received;
			connection->lastreceived = connection->connection.received;
			connection->lastsent = totalout;//connection->connection.sent;
		}

		connection->peerincomingmutex.unlock();

		NetSleep(1);//sleep then poll again
	}
	netlog("Network thread exited\n");

	return;
};

void NetConnection::SendPackets()//actually sends to the server
{
	//this->sendingmutex.lock();

#ifdef NETSIMULATE
	this->connection.SendLaggedPackets();
#endif

	for (auto client : this->peers)
	{
		client.second->SendPackets();
	}

	//this->sendingmutex.unlock();
}

void NetConnection::Open(unsigned short port, unsigned int maxpeers)
{
	NetworkInit();//do this in case we havent already

	//reset stats
	lastreceived = lastupdate = lastsent = lasttcp = 0;
	sent = recieved = 0;

	//setup maxpeers
	this->maxpeers = maxpeers;

	this->running = true;//signal for thread

	//open ports ahoy
	this->connection.Open(port);

	//start the thread
	this->thread = std::thread(NetConnection::net_thread, this);
}

void NetConnection::Close()
{
	//signal the thread to exit
	this->running = false;

	//kill the networking thread
	if (this->thread.joinable())
		this->thread.join();

	this->Disconnect();

	this->connection.Close();

	//clean up remaining incoming paks
	while(incoming.size() > 0)
	{
		TPacket p = this->incoming.front();
		this->incoming.pop();
		delete[] p.data;
	}
}

void NetConnection::Send(Peer* client, char* data, unsigned int size, bool OOB)
{
	if (OOB)
	{
		netlog("[Server] Sent OOB Packet\n");
		client->SendOOB(data, size);
		return;
	}

	client->Send(data, size);
}