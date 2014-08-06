#ifndef _CONNECTION_HEADER_
#define _CONNECTION_HEADER_

#include <map>
#include <thread>
#include <mutex>
#include <memory>

#include "NetMsg.h"
#include "Sockets.h"
#include "NetDefines.h"
#include "NetChannel.h"

//packets, modify these as you wish
#pragma pack(push)
#pragma pack(1)
struct ConnectionRequest//sent by client to request a join
{
	//all of this is required
	unsigned char packid;
	int id;//use a random number to ensure that people cant spam random packets and fake joining players
	char plyname[50];
	char password[50];
};
#pragma pack(pop)


enum PeerConnectionState
{
	PEER_CONNECTING,
	PEER_CONNECTED,
	PEER_DISCONNECTED, //only used when connection request is denied
};

/*class Syncronizer
{
public:
void Send(client);

//have variable rates, and store all data in here
//needs to have way to get snapshots externally for rolling back
//have each thing sent have a function that says if I should send to this client
}*/

class Peer
{
public:
	NetChannel connection;
	void* data;//used by external client to store data associated with this user, like the client class
};

//structure used by Receive()
struct TPacket
{
	char id;//for events, like join and leave
	char* data;
	unsigned int size;
	Peer* sender;//or something like this
	Address addr;

	void Release()
	{
		delete[] this->data;
	}
};

//hooks for a server class
class IServer
{
public:
	virtual void OnConnect(Peer* client, ConnectionRequest* p) = 0;
	virtual void OnDisconnect(Peer* client) = 0;

	//called from another thread, be careful
	virtual char* CanConnect(Address addr, ConnectionRequest* p) = 0;
};



class NetConnection
{
	std::mutex threadmutex;
	std::mutex queuemutex;
	std::mutex sendingmutex;

	std::thread thread;

	bool running;
	std::queue<TPacket> incoming;
	
	int timeout;
public:

	//stats
	int lastreceived, lastsent, lastupdate, lasttcp;
	float datarate;
	float outdatarate;
	float tcprate;

	Socket connection;//typically just shares one socket

	NetConnection()
	{
		this->observer = 0;
		this->running = false;
		this->timeout = 15000;//in milliseconds
	}

	~NetConnection()
	{
		this->Close();
	}

	//add peer argument to send functions and way to get peers without accessing peers object
	//todo, implement these so it sends them to first connected client
	void SendOOB(char* data, int size)
	{
		if (this->peers.size() > 0)
			this->peers.begin()->second->connection.SendOOB(data, size);
	};//this defaults to first connected peer

	void SendOOB(Peer* peer, char* data, int size)
	{
		peer->connection.SendOOB(data, size);
	}

	void Send(char* data, int size)
	{
		if (this->peers.size() > 0)
			this->peers.begin()->second->connection.Send(data, size);
	};//this defaults to first connected peer

	void Send(Peer* peer, char* data, int size)
	{
		peer->connection.Send(data, size);
	}

	void SendReliable(char* data, int size)
	{
		if (this->peers.size() > 0)
			this->peers.begin()->second->connection.SendReliable(data, size);
	};//this also defaults to first connected peer

	void SendReliable(Peer* peer, char* data, int size)
	{
		peer->connection.SendReliable(data, size);
	}

	//ok have to make this async, or at least block til network thread gets connection packet
	//this is because packets can come from different addresses, so need to have get successful 
	//connection packet from packet queue
	//blocking function
	//need to change this to just add new peer connection
	int Connect(Address server_address, char* name, char* password, char** status = 0)
	{ 
		Peer* peer = new Peer;
		peer->connection.Init();
		peer->connection.state = PEER_CONNECTING;
		peer->connection.remoteaddr = server_address;
		peer->connection.connection = &this->connection;

		//add new connection to peer list;
		this->peers[server_address] = peer;

		if (status)
			status[0] = "Opening Sockets...";

		//build handshake packet
		ConnectionRequest p;
		p.packid = (unsigned char)NetCommandPackets::ConnectionRequest;
		p.id = NET_PROTOCOL_ID;
		strncpy(p.plyname, name, 50);
		if (password)
			strncpy(p.password, password, 50);
		else
			memset(p.password, 0, 50);

		if (status)
			status[0] = "Performing Handshake...";

		Address sender;
		for (int i = 0; i < 4; i++)//attempt to connect 4 times
		{
			//try and request a connection by sending the handshake
			peer->connection.SendOOB((char*)&p, sizeof(ConnectionRequest));

			netlogf("trying to connect to %i.%i.%i.%i:%u\n", (int)peer->connection.remoteaddr.GetA(), (int)peer->connection.remoteaddr.GetB(), (int)peer->connection.remoteaddr.GetC(), (int)peer->connection.remoteaddr.GetD(), (unsigned int)peer->connection.remoteaddr.GetPort());

			NetSleep(1000);//wait for answer

			//ok, check state of the new connection
			if (peer->connection.state == PEER_CONNECTED)
			{
				//we are done
				netlogf("peer is now connected, got packet back from %i.%i.%i.%i:%u\n", (int)sender.GetA(), (int)sender.GetB(), (int)sender.GetC(), (int)sender.GetD(), (unsigned int)sender.GetPort());

				if (status)
					status[0] = "Connection Established";

				return 1;
			}
			else if (peer->connection.state == PEER_DISCONNECTED)
			{
				//need to get reason back
				if (status)
					status[0] = "Connection was denied.";
				NetSleep(1000);

				//remove the peer
				this->threadmutex.lock();

				//remove the peer
				this->peers.erase(this->peers.find(peer->connection.remoteaddr));

				this->threadmutex.unlock();

				return -1;
			}

			//this is problematic, must make this async
			/*int length = 0;//this->connection.Receive(sender, (void*)buffer, 1024);
			if (length > 0)//did I get the packet?
			{
			InitialPacket *pack = (InitialPacket*)(buffer+4);
			if (pack->id != 1945)
			{
			//was probably bad connect
			if (buffer[4] == 69)//disconnect w/reason
			{
			char reason[150];
			NetMsg msg(150,&buffer[5]);
			msg.ReadString(reason);

			status[0] = reason;//"Connection Failed For a Reason.";
			Sleep(1000);

			//remove the peer
			this->Disconnect(peer);

			status[0] = "";//gotta prevent bugs

			return -1;
			}
			}
			}*/
		}
		if (status)
			status[0] = "Connection Failed After 4 Retries.";
		NetSleep(1000);

		this->threadmutex.lock();

		//remove the peer
		this->peers.erase(this->peers.find(peer->connection.remoteaddr));

		this->threadmutex.unlock();

		delete peer;

		return -1;
	}

	//this disconnects all peers, or the specified one
	//will call the on disconnect hooks
	void Disconnect(Peer* peer = 0)
	{
		this->threadmutex.lock();
		if (peer)
		{
			//todo
			unsigned char id = (unsigned char)NetCommandPackets::Disconnect;
			//disconnect packet id
			peer->connection.SendOOB((char*)&id, 1);

			this->OnDisconnect(peer);

			//remove the peer
			this->peers.erase(this->peers.find(peer->connection.remoteaddr));

			delete peer;
		}
		else
		{
			while(incoming.size() > 0)
			{
				TPacket p = this->incoming.front();
				this->incoming.pop();
				delete[] p.data;
			}

			//send message that we are disconnecting
			auto copy = this->peers;
			for (auto p : copy)//auto p = this->peers.cbegin(); p != peers.cend(); )
			{
				if (p.second->connection.state == PEER_CONNECTED)
				{
					unsigned char id = (unsigned char)NetCommandPackets::Disconnect;
					//disconnect packet id
					p.second->connection.SendOOB((char*)&id, 1);
				}

				this->OnDisconnect(p.second);

				this->peers.erase(this->peers.find(p.first));

				delete p.second;
			}
		}
		this->threadmutex.unlock();
	}

	//opens sockets for communication
	void Open(unsigned short port);

	//closes sockets, and disconnects any connected peers
	void Close();

	IServer* observer;
	void SetObserver(IServer* server)
	{
		observer = server;
	}

	//sets the disconnect timeout in milliseconds
	void SetTimeout(int ms)
	{
		this->timeout = ms;
	}

	//forces sending of queued packets to clients
	void SendPackets();

	//sends packet to specified peer
	void Send(Peer* client, char* data, unsigned int size, bool OOB = false);

	//delete returned buffer when done
	/*std::unique_ptr<Packet> Receive()
	{
		return std::make_unique<Packet>();
	}*/

	//this returns a char array of the latest packet received, delete[] when finished with it
	char* Receive(Peer*& sender, int& size)
	{
		if (this->incoming.size() == 0)
			return 0;//no messages to parse

		while (true)
		{
			this->queuemutex.lock();//ok, dont put locks in locks, and then the same locks in locks in the opposite order somewhere else
			TPacket p = this->incoming.front();
			this->incoming.pop();
			this->queuemutex.unlock();

			//ok, introduce callbacks here as different "packets"
			if (p.id == 1)
			{
				//connect
				this->threadmutex.lock();

				Address sender = p.addr;

				//this->peers[
				netlogf( "Client Connected from %d.%d.%d.%d:%d\n", sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort());
				Peer* peer = new Peer;
				peer->connection.remoteaddr = sender;
				peer->connection.Init();
				peer->connection.server = true;
				peer->connection.connection = &this->connection;
				peer->connection.state = PEER_CONNECTED;

				//send reply
				char t[500];
				NetMsg msg(500,t);
				//msg.WriteInt(-1);
				msg.WriteInt(NET_MAGIC_ID);//magic
				msg.WriteByte(1);//success
				msg.WriteShort(5007);//my port number, todo, changeme
				peer->connection.SendOOB(t, msg.cursize);

				//connect
				this->OnConnect(peer, (ConnectionRequest*)p.data);

				this->peers[p.addr] = peer;
				this->threadmutex.unlock();

				delete[] p.data;
			}
			else if (p.id == 2)
			{
				this->threadmutex.lock();//Enter();

				netlogf("Client from %d.%d.%d.%d:%d Disconnected\n", p.addr.GetA(), p.addr.GetB(), p.addr.GetC(), p.addr.GetD(), p.addr.GetPort());

				//disconnect
				this->OnDisconnect(p.sender);

				this->peers.erase(this->peers.find(p.addr));

				delete p.sender;

				this->threadmutex.unlock();//Leave();
			}
			else
			{
				size = p.size;
				sender = p.sender;

				return p.data;
			}


			if (this->incoming.size() == 0)
			{
				size = 0;
				sender = 0;
				return 0;
			}
		}
	};

private:
	//hook calls
	void OnConnect(Peer* newclient, ConnectionRequest* p)
	{
		if (this->observer)
			this->observer->OnConnect(newclient, p);
	}

	void OnDisconnect(Peer* client)
	{
		if (this->observer)
			this->observer->OnDisconnect(client);
	}

	char* CanConnect(Address addr, ConnectionRequest* p)
	{
		if (p->id != NET_PROTOCOL_ID)
			return "Bad Protocol ID";

		if (this->observer)
			return this->observer->CanConnect(addr, p);

		return 0;
	}

	static void net_thread(void* data);

public:
	std::map<Address, Peer*> peers;
};
#endif