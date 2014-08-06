Here's the basics of how to use this...

First, include the Connection.h file:
```cpp
#include "Netlibrary/Connection.h"
```

Then you must open a connection on some port:
```cpp
NetConnection connection;
connection.Open(5432/*insert your port here*/);
```

Then you can connect to another server, or just run a message receive loop as shown further down to operate a server.
This is a blocking function. It tries to connect 4 times, with a timeout of 1 second each before failing:
```cpp
int status = connection.Connect(Address(127,0,0,1,5007), "password", 0/*status string pointer if you want*/);
if (status < 0)
{
	//connection failed
}
```

To receive messages:
```cpp
Peer* sender; int size;
char* buffer;
while (buffer = connection.Receive(sender, size))
{
	//do stuff with your message
	
	delete[] buffer;//delete the message buffer when you are finished with it, or else it leaks
}
```

Now, there are four different types of messages that you can send:
1. Unreliable, Unsequenced Messages - these may or may not arrive in any order
2. Reliable, Unordered Messages - these are guaranteed to arrive, but can be in any order
3. Reliable, Ordered Messages - these are guaranteed to arrive in the order that they were sent in
4. OOB Messages - these are unreliable messages used primarily for low level commands that are sent immediately

```cpp
//There are two different ways to send messages, either by specifying the Peer* or not
netconnection.SendOOB(char* data, int size);
netconnection.SendOOB(Peer* peer, char* data, int size);

netconnection.Send(char* data, int size);
netconnection.Send(Peer* peer, char* data, int size);

netconnection.SendReliable(char* data, int size);
netconnection.SendReliable(Peer* peer, char* data, int size);
```

You may access all of the connected peers through the peers std::map
```cpp
netconnection.peers
```
