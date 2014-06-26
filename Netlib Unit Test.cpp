// Netlib Unit Test.cpp : Defines the entry point for the console application.
//

#ifdef _DEBUG   
#ifndef DBG_NEW      
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )     
#define new DBG_NEW   
#endif
#endif

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include "C:\Users\Matthew\Desktop\MyGame\NewAndroid\NewAndroid\Code\Net\Connection.h"

#include <vector>
#include <tchar.h>
#include <thread>
#include <assert.h>

int _tmain(int argc, _TCHAR* argv[])
{
	_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	srand(GetTickCount());

	Peer* sender; int size;

	NetConnection connection;
	connection.Open(5008);

	NetConnection server;
	server.Open(5007);

	connection.connection.SetRTT(0);
	connection.connection.SetDrop(0);
	connection.connection.SetVariance(0);

	auto t = std::thread([](NetConnection* server)
	{
		while (true)
		{
			Peer* sender; int size;
			server->Receive(sender, size);

			if (server->peers.size() > 5)
				return;
		}
	}, &server);

	int status = connection.Connect(0, Address(127,0,0,1,5007), "testing", 0);
	if (status < 0)
	{
		printf("Connection Test Failed!!!\n");
	}

	NetConnection cons[5];
	for (int i = 0; i < 5; i ++)
	{
		cons[i].Open(5010+i);
		int stat = cons[i].Connect(0, Address(127,0,0,1,5007), "yo", 0);
		if (stat < 0)
			printf("Connection test failed!\n");
	}

	if (t.joinable())
		t.join();

	printf("Connection Test Successful\n");

	for (int i = 0; i < 5; i++)
	{
		//cons[i].Disconnect();
		cons[i].Close();
	}

	while (server.peers.size() > 1)
	{
		Peer* s; int size;
		server.Receive(s, size);
		Sleep(1);
	}

	printf("Connections closed successfully\n\n");

	printf("Doing Security Test\n");
	 
	for (int i = 0; i < 50; i++)
	{
		char* buffer = new char[i*6];
		for (int in = 0; in < i*6; in++)
		{
			buffer[in] = rand();
		}
		connection.connection.Send(connection.peers.begin()->second->connection.remoteaddr, buffer, i*6);
		delete[] buffer;
	}

	Sleep(1000);//wait to receive messages

	//get all messages out
	char* out;
	while (out = server.Receive(sender, size))
	{
		delete[] out;
	}
	printf("Well, we didnt crash, so security test probably didn't fail...\n\n");
	

	connection.connection.SetRTT(0.05);
	connection.connection.SetDrop(0.1);
	connection.connection.SetVariance(0.02);

	//start testing
	printf("Testing reliable messages...\n");
	int num = 50;
	while(num-- > 0)
	{
		int data[200];
		for (int i = 0; i < 200; i++)
		{
			data[i] = rand();
		}
		connection.SendReliable((char*)data, sizeof(data));

		//ok, now check
		Peer* sender; int size;
		while (true)
		{
			char* buffer = server.Receive(sender, size);
			if (buffer)
			{
				if (size == sizeof(data))
				{
					for (int i = 0; i < 200; i++)
					{
						assert(((int*)buffer)[i] == data[i]);
					}
					//printf("one good\n");
				}
				else
				{
					printf("Bad message size!!!\n");
				}
				delete[] buffer;
				break;
			}
		}
	}
	printf("Reliable message sending passed!\n\n");

	printf("Testing reliable ordered messages...\n");
	num = 50;
	int data[20][200];
	for (int i = 0; i < 20; i++)
	{
		for (int i2 = 0; i2 < 200; i2++)
		{
			data[i][i2] = rand();
		}
		data[i][0] = i;
		connection.peers.begin()->second->connection.SendReliableOrdered((char*)data[i], sizeof(data[i]), 5);
	}


	//ok, now check
	num = 0;
	while (num < 20)
	{
		char* buffer = server.Receive(sender, size);
		if (buffer)
		{
			if (size == sizeof(data[0]))
			{
				for (int i = 0; i < 200; i++)
				{
					assert(((int*)buffer)[i] == data[num][i]);
				}
				//printf("one good\n");
			}
			else
			{
				printf("Bad message size!!!\n");
			}
			num++;
			delete[] buffer;
		}
	}
	printf("Ordered Reliable Message Sending Passed!\n\n");

	printf("Testing Split Reliable Ordered Messages...\n");
	num = 50;
	int data2[20][500];
	for (int i = 0; i < 20; i++)
	{
		for (int i2 = 0; i2 < 500; i2++)
		{
			data2[i][i2] = rand();
		}
		data2[i][0] = i;
		connection.peers.begin()->second->connection.SendReliableOrdered((char*)data2[i], sizeof(data2[i]), 5);
	}


	//ok, now check
	//Peer* sender; int size;
	num = 0;
	while (num < 20)
	{
		char* buffer = server.Receive(sender, size);
		if (buffer)
		{
			if (size == sizeof(data2[0]))
			{
				for (int i = 0; i < 500; i++)
				{
					assert(((int*)buffer)[i] == data2[num][i]);
				}
			}
			else
			{
				printf("Bad message size!!!\n");
			}
			num++;
			delete[] buffer;
		}
	}
	printf("Ordered Split Reliable Message Sending Passed!\n\n");

	printf("Testing Reliable Message Splitting...\n");
	//test splitting
	num = 50;
	while(num-- > 0)
	{
		int data[800];
		for (int i = 0; i < 800; i++)
		{
			data[i] = rand();
		}
		connection.SendReliable((char*)data, sizeof(data));

		//ok, now check
		Peer* sender; int size;
		while (true)
		{
			char* buffer = server.Receive(sender, size);
			if (buffer)
			{
				if (size == sizeof(data))
				{
					for (int i = 0; i < 800; i++)
					{
						assert(((int*)buffer)[i] == data[i]);
					}
				}
				else
				{
					printf("Bad message size!!!\n");
				}
				delete[] buffer;
				break;
			}
		}
	}
	printf("Reliable Message Splitting Sending Passed!\n\n");

	connection.connection.SetRTT(0);
	connection.connection.SetDrop(0);
	connection.connection.SetVariance(0);

	//test unreliable
	printf("Testing unreliable message sending...\n");
	//test splitting
	num = 50;
	while(num-- > 0)
	{
		int data[200];
		for (int i = 0; i < 200; i++)
		{
			data[i] = rand();
		}
		connection.Send((char*)data, sizeof(data));

		//ok, now check
		Peer* sender; int size;
		while (true)
		{
			char* buffer = server.Receive(sender, size);
			if (buffer)
			{
				if (size == sizeof(data))
				{
					for (int i = 0; i < 200; i++)
					{
						assert(((int*)buffer)[i] == data[i]);
					}
				}
				else
				{
					printf("Bad message size!!!\n");
				}
				delete[] buffer;
				break;
			}
		}
	}
	printf("Unreliable message sending passed!\n\n");

	//test unreliable splitting
	printf("Testing unreliable message splitting...\n");
	num = 50;
	while(num-- > 0)
	{
		int data[800];
		for (int i = 0; i < 800; i++)
		{
			data[i] = rand();
		}
		connection.Send((char*)data, sizeof(data));

		//ok, now check
		Peer* sender; int size;
		while (true)
		{
			char* buffer = server.Receive(sender, size);
			if (buffer)
			{
				if (size == sizeof(data))
				{
					for (int i = 0; i < 800; i++)
					{
						assert(((int*)buffer)[i] == data[i]);
					}
				}
				else
				{
					printf("Bad message size!!!\n");
				}
				delete[] buffer;
				break;
			}
		}
	}
	printf("Unreliable message splitting sending passed!\n\n");

	//test packet coalesing
	printf("Testing unreliable message coalescing...\n");
	num = 50;
	while(num-- > 0)
	{
		int data[121];
		for (int i = 0; i < 121; i++)
		{
			data[i] = rand();
		}
		connection.Send((char*)data, sizeof(data));
		connection.Send((char*)data, sizeof(data));
		connection.Send((char*)data, sizeof(data));

		connection.SendPackets();//force send

		//ok, now check
		Peer* sender; int size;
		for (int i = 0; i < 3; i++)
		{
			while (true)
			{
				char* buffer = server.Receive(sender, size);
				if (buffer)
				{
					if (size == sizeof(data))
					{
						for (int i = 0; i < 121; i++)
						{
							assert(((int*)buffer)[i] == data[i]);
						}
					}
					else
					{
						printf("Bad message size!!!\n");
					}
					delete[] buffer;
					break;
				}
			}
		}
	}
	printf("Unreliable message coalescing passed!\n\n");

	printf("Testing Disconnection...\n");
	connection.Disconnect();
	while (true)
	{
		Peer* sender; int size;
		server.Receive(sender, size);

		if (server.peers.size() == 0)
			break;
	}
	printf("Disconnection successful!\n");

	//while (true)
	Sleep(10000);

	return 0;
}

