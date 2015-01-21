#ifndef _NETMSG_HEADER
#define _NETMSG_HEADER

#include <string.h>
#include <stdio.h>

#ifdef _DEBUG
#include <assert.h>
#define NET_ASSERT(hi) assert(hi)
#else
#define NET_ASSERT(hi)
#endif

struct NetMsg
{
	int readpos;
	int maxsize;
	int cursize;
	char* data;

	NetMsg()
	{
		this->readpos = 0;
		this->maxsize = 0;
		this->cursize = 0;
	};

	NetMsg(int size, char* dat)
	{
		this->readpos = 0;
		this->cursize = 0;
		this->maxsize = size;
		this->data = dat;
	};

	void Reset()
	{
		readpos = 0;
	};

	void WriteByte(unsigned char i)
	{
		this->data[cursize] = i;
		this->cursize += 1;
	};

	unsigned char ReadByte()
	{
		unsigned char b = this->data[readpos];
		this->readpos += 1;

		NET_ASSERT(readpos <= this->maxsize);

		return b;
	};

	void WriteShort(unsigned short i)
	{
		unsigned short *sp = (unsigned short *)&this->data[this->cursize];
		*sp = i;
		this->cursize += 2;
	};

	unsigned short ReadShort()
	{
		unsigned short value;
		unsigned short *sp = (unsigned short *)&this->data[this->readpos];
		value = *sp;
		this->readpos += 2;

		NET_ASSERT(readpos <= this->maxsize);

		return value;
	};

	void WriteInt(int i)
	{
		int *sp = (int*)&this->data[this->cursize];
		*sp = i;
		this->cursize += 4;
	};

	int ReadInt()
	{
		int value;
		int *sp = (int*)&this->data[this->readpos];
		value = *sp;
		this->readpos += 4;

		NET_ASSERT(readpos <= this->maxsize);

		return value;
	};

	void WriteLong(unsigned int i)
	{
		unsigned int *sp = (unsigned int*)&this->data[this->cursize];
		*sp = i;
		this->cursize += 4;
	};

	unsigned int ReadLong()
	{
		unsigned int value;
		unsigned int *sp = (unsigned int*)&this->data[this->readpos];
		value = *sp;
		this->readpos += 4;

		NET_ASSERT(readpos <= this->maxsize);

		return value;
	};

	void WriteDouble(double f)
	{
		double *sp = (double *)&this->data[this->cursize];
		*sp = f;
		this->cursize += 8;
	};

	double ReadDouble()
	{
		double value;
		double *sp = (double *)&this->data[this->readpos];
		value = *sp;
		this->readpos += 8;

		NET_ASSERT(readpos <= this->maxsize);

		return value;
	};

	void WriteFloat(float f)
	{
		float *sp = (float *)&this->data[this->cursize];
		*sp = f;
		this->cursize += 4;
	};

	float ReadFloat()
	{
		float value;
		float *sp = (float *)&this->data[this->readpos];
		value = *sp;
		this->readpos += 4;

		NET_ASSERT(readpos <= this->maxsize);

		return value;
	};


	void WriteData(char* dat, int size)
	{
		//if (size < 0 || size > 600000)
			//log("we have issue");
		memcpy(&this->data[this->cursize], dat, size);
		this->cursize += size;
	};

	void WriteString(const char* str)
	{
		strcpy(&this->data[cursize],str);
		int l = strlen(str);
		//unsigned int p = 0;
		//while (str[p] != 0)
		//{
		//	this->data[cursize + p] = str[p];
		//	p += 1;
		//}
		//this->data[cursize + p] = 0;
		this->cursize += l + 1;
	}

	void WriteString(const char* str, unsigned int len)
	{
		strncpy(&this->data[cursize],str, len);
		int l = strlen(str);
		//unsigned int p = 0;
		//while (str[p] != 0)
		//{
		//	this->data[cursize + p] = str[p];
		//	p += 1;
		//}
		//this->data[cursize + p] = 0;
		this->cursize += l + 1;
	}

	void ReadString(char* str, unsigned int count)
	{
		strncpy(str, &this->data[readpos], count);
		int l = strlen(str);

		this->readpos += l + 1;

		NET_ASSERT(readpos <= this->maxsize);

		//unsigned int p = 0;
		//while (this->data[readpos + p] != 0)
		//{
		//	str[p] = this->data[readpos + p];
		//	p += 1;
		//}
		//str[p] = 0;
		//this->readpos += p;
	}

	/*char* ReadData(int size)
	{
		char* d = new char[size];
		memcpy(d, &this->data[this->readpos], size);
		this->readpos += size;
		return d;
	};*/

	void ReadData(char* dest, int size)
	{
		//if (size < 0 || size > 600000)
			//log("we have issue");
		memcpy(dest, &this->data[this->readpos], size);
		this->readpos += size;

		NET_ASSERT(readpos <= this->maxsize);
	}
};

#endif