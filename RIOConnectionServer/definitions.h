#pragma once
#include "stdafx.h"

enum SocketType {
	UDPSocket,
	TCPListener,
	TCPConnection,
	TCPClient,
	TCPServer
};

enum InstructionType {
	SEND,
	RECEIVE,
	CLOSESOCKET,
	FREEBUFFER
};

enum OperationType {
	OP_RECEIVE,
	OP_SEND
};

enum COMPLETION_KEY {
	CK_QUIT,
	CK_RIO,
	CK_ACCEPT,
	CK_GETINFO,
	CK_COUNTER,
	CK_BUFINFO
};
enum SrcDstType
{
	MATCHING_SERVER = 0,
	MATCHING_CLIENT,
	ROOM_SERVER,
	PACKET_GENERATOR,
	MONITORING_SERVER,
	CONFIG_SERVER,
	CONNECTION_SERVER
};

enum Command
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

enum Status : int
{
	SUCCESS,
	FAIL,
	NONE
};

//Some stuff for flat buffer that's not needed in this program
//table Body
//{
//cmd: Command;
//status: Status;
//data: string;
//}
//
//root_type Body;


struct Body
{
	Command command;
	Status status;
	char* data;
};


struct EXTENDED_OVERLAPPED : public OVERLAPPED {
	int serviceType;
	SOCKET relevantSocket;
	char* buffer = new char[(2*(sizeof(sockaddr_in))+32)];
};

struct CQ_Handler {
	CQ_Handler() {
		rio_CQ = RIO_INVALID_CQ;
	}

	RIO_CQ rio_CQ;
	CRITICAL_SECTION criticalSection;
};

struct RQ_Handler {
	RIO_RQ rio_RQ;
	SOCKET socket;
	CRITICAL_SECTION criticalSection;
};


struct EXTENDED_RIO_BUF : public RIO_BUF
{
	OperationType operationType;
	SrcDstType srcType;
	int socketContext;
	char* buffer;
	int messageLength;

};

struct Instruction {
	InstructionType type;
	int socketContext;		//Destination Code
	SrcDstType destinationType;
	EXTENDED_RIO_BUF* buffer;
};


