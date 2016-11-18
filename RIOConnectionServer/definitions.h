#pragma once
#include "stdafx.h"

enum SOCKET_TYPE 
{
	UDPSocket,
	TCPListener,
	TCPConnection,
	TCPClient,
	TCPServer
};

enum INSTRUCTION_TYPE
{
	SEND,
	RECEIVE,
	CLOSESOCKET,
	FREEBUFFER
};

enum OPERATION_TYPE 
{
	OP_RECEIVE,
	OP_SEND
};

enum COMPLETION_KEY 
{
	CK_QUIT,
	CK_RIO,
	CK_ACCEPT,
	CK_GETINFO,
	CK_COUNTER,
	CK_BUFINFO
};
enum SRC_DEST_TYPE
{
	MATCHING_SERVER = 0,
	MATCHING_CLIENT,
	ROOM_SERVER,
	PACKET_GENERATOR,
	MONITORING_SERVER,
	CONFIG_SERVER,
	CONNECTION_SERVER
};

enum COMMAND
{
	//Common
	HEALTH_CHECK = 0,

	//MS~MS
	MATCH_REQUEST = 10,
	MATCH_RESPONSE,
	LATENCY,

	//MS~Config
	MSLIST_REQUEST = 20,
	MSLIST_RESPONSE,

	//Room~MS
	ROOM_CREATE_REQUEST = 30,
	ROOM_CREATE_RESPONSE,

	//Room~Client
	ROOM_JOIN_REQUEST = 40,
	ROOM_JOIN_RESPONSE,
	GAME_START,
	GAME_END
};

enum STATUS : int
{
	SUCCESS,
	FAIL,
	NONE
};


struct Body
{
	COMMAND command;
	STATUS status;
	char* data;
};


struct ExtendedOverlapped : public OVERLAPPED 
{
	int serviceType;
	SOCKET relevantSocket;
	char* buffer = new char[(2*(sizeof(sockaddr_in))+32)];
};

struct CQ_Handler 
{
	CQ_Handler() 
	{
		rio_CQ = RIO_INVALID_CQ;
	}

	RIO_CQ rio_CQ;
	CRITICAL_SECTION criticalSection;
};

struct RQ_Handler 
{
	RIO_RQ rio_RQ;
	SOCKET socket;
	CRITICAL_SECTION criticalSection;
};


struct ExtendedRioBuf : public RIO_BUF
{
	OPERATION_TYPE operationType;
	SRC_DEST_TYPE srcType;
	int socketContext;
	char* buffer;
	int messageLength;

};

struct Instruction 
{
	INSTRUCTION_TYPE type;
	int socketContext;		//Destination Code
	SRC_DEST_TYPE destinationType;
	ExtendedRioBuf* buffer;
};

struct RIOMainConfig 
{
	int bufferSize;
	int dequeueCount;
	int numThreads;
	int spinCount;
};

struct ServiceData 
{
	SOCKET_TYPE serviceType;
	int serviceCode;
	int servicePort;
	int serviceMaxClients;
	int serviceMaxAccepts;
	int serviceRQMaxReceives;
	int serviceRQMaxSends;
	bool isAddressRequired;

	ServiceData() 
	{

	}

	ServiceData(SOCKET_TYPE servType, int code, int port) 
	{
		serviceType = servType;
		serviceCode = code;
		servicePort = port;
	}
};


