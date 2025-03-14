// Copyright (C) 1996-2001 Id Software, Inc.
// Copyright (C) 2010-2014 QuakeSpasm developers
// GPLv3 See LICENSE for details.

#include "quakedef.h"
#include "net_sys.h"
#include "net_defs.h"
#include "net_loop.h"

static qboolean localconnectpending = false;
static qsocket_t *loop_client = NULL;
static qsocket_t *loop_server = NULL;

int Loop_Init()
{
	if (cls.state == ca_dedicated)
		return -1;
	return 0;
}

void Loop_Shutdown()
{
}

void Loop_Listen(qboolean state)
{
	(void)state; // removing the argument would break things, probably
} // mentioning it here with void just to quiet the compiler

void Loop_SearchForHosts(qboolean xmit)
{
	(void)xmit; // same as above
	if (!sv.active)
		return;
	hostCacheCount = 1;
	if (Q_strcmp(hostname.string, "UNNAMED") == 0)
		Q_strcpy(hostcache[0].name, "local");
	else
		Q_strcpy(hostcache[0].name, hostname.string);
	Q_strcpy(hostcache[0].map, sv.name);
	hostcache[0].users = net_activeconnections;
	hostcache[0].maxusers = svs.maxclients;
	hostcache[0].driver = net_driverlevel;
	Q_strcpy(hostcache[0].cname, "local");
}

qsocket_t *Loop_Connect(const char *host)
{
	if (Q_strcmp(host, "local") != 0)
		return NULL;
	localconnectpending = true;
	if (!loop_client) {
		if ((loop_client = NET_NewQSocket()) == NULL) {
			Con_Printf("Loop_Connect: no qsocket available\n");
			return NULL;
		}
		Q_strcpy(loop_client->address, "localhost");
	}
	loop_client->receiveMessageLength = 0;
	loop_client->sendMessageLength = 0;
	loop_client->canSend = true;
	if (!loop_server) {
		if ((loop_server = NET_NewQSocket()) == NULL) {
			Con_Printf("Loop_Connect: no qsocket available\n");
			return NULL;
		}
		Q_strcpy(loop_server->address, "LOCAL");
	}
	loop_server->receiveMessageLength = 0;
	loop_server->sendMessageLength = 0;
	loop_server->canSend = true;
	loop_client->driverdata = (void *)loop_server;
	loop_server->driverdata = (void *)loop_client;
	return loop_client;
}

qsocket_t *Loop_CheckNewConnections()
{
	if (!localconnectpending)
		return NULL;
	localconnectpending = false;
	loop_server->sendMessageLength = 0;
	loop_server->receiveMessageLength = 0;
	loop_server->canSend = true;
	loop_client->sendMessageLength = 0;
	loop_client->receiveMessageLength = 0;
	loop_client->canSend = true;
	return loop_server;
}

static int IntAlign(int value)
{
	return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
}

int Loop_GetMessage(qsocket_t *sock)
{
	if (sock->receiveMessageLength == 0)
		return 0;
	int ret = sock->receiveMessage[0];
	int length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
	// alignment byte skipped here
	SZ_Clear(&net_message);
	SZ_Write(&net_message, &sock->receiveMessage[4], length);
	length = IntAlign(length + 4);
	sock->receiveMessageLength -= length;
	if (sock->receiveMessageLength)
		memmove(sock->receiveMessage, &sock->receiveMessage[length],
			sock->receiveMessageLength);
	if (sock->driverdata && ret == 1)
		((qsocket_t *) sock->driverdata)->canSend = true;
	return ret;
}

int Loop_SendMessage(qsocket_t *sock, sizebuf_t *data)
{
	if (!sock->driverdata)
		return -1;
	int *bufferLength = &((qsocket_t *) sock->driverdata)->receiveMessageLength;
	if ((*bufferLength + data->cursize + 4) > NET_MAXMESSAGE)
		Sys_Error("Loop_SendMessage: overflow");
	byte *buffer =
	    ((qsocket_t *) sock->driverdata)->receiveMessage + *bufferLength;
	*buffer++ = 1; // message type
	*buffer++ = data->cursize & 0xff; // length
	*buffer++ = data->cursize >> 8;
	buffer++; // align
	Q_memcpy(buffer, data->data, data->cursize); // message
	*bufferLength = IntAlign(*bufferLength + data->cursize + 4);
	sock->canSend = false;
	return 1;
}

int Loop_SendUnreliableMessage(qsocket_t *sock, sizebuf_t *data)
{
	if (!sock->driverdata)
		return -1;
	int *bufferLength =
		&((qsocket_t *) sock->driverdata)->receiveMessageLength;
	if ((*bufferLength + data->cursize + sizeof(byte) + sizeof(short)) >
	    NET_MAXMESSAGE)
		return 0;
	byte *buffer =
	    ((qsocket_t *) sock->driverdata)->receiveMessage + *bufferLength;
	*buffer++ = 2; // message type
	*buffer++ = data->cursize & 0xff; // length
	*buffer++ = data->cursize >> 8;
	buffer++; // align
	Q_memcpy(buffer, data->data, data->cursize); // message
	*bufferLength = IntAlign(*bufferLength + data->cursize + 4);
	return 1;
}

qboolean Loop_CanSendMessage(qsocket_t *sock)
{
	if (!sock->driverdata)
		return false;
	return sock->canSend;
}

qboolean Loop_CanSendUnreliableMessage(qsocket_t *sock)
{
	(void)sock;
	return true;
}

void Loop_Close(qsocket_t *sock)
{
	if (sock->driverdata)
		((qsocket_t *) sock->driverdata)->driverdata = NULL;
	sock->receiveMessageLength = 0;
	sock->sendMessageLength = 0;
	sock->canSend = true;
	if (sock == loop_client)
		loop_client = NULL;
	else
		loop_server = NULL;
}
