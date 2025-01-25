// Copyright (C) 1996-1997 Id Software, Inc. GPLv3 See LICENSE for details.

// cl_parse.c  -- parse a message received from the server

#include "quakedef.h"

#define SHOWNET(x) if(cl_shownet.value==2)Con_Printf("%3i:%s\n",msg_readcount-1,x);

int bitcounts[16];
char *svc_strings[] = {
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",		// [long] server version
	"svc_setview",		// [short] entity number
	"svc_sound",		// <see code>
	"svc_time",		// [float] server time
	"svc_print",		// [string] null terminated string
	"svc_stufftext",	// [string] stuffed into client's console buffer
	// the string should be \n terminated
	"svc_setangle",		// [vec3] set the view angle to this absolute value

	"svc_serverinfo",	// [long] version
	// [string] signon string
	// [string]..[0]model cache [string]...[0]sounds cache
	// [string]..[0]item cache
	"svc_lightstyle",	// [byte] [string]
	"svc_updatename",	// [byte] [string]
	"svc_updatefrags",	// [byte] [short]
	"svc_clientdata",	// <shortbits + data>
	"svc_stopsound",	// <see code>
	"svc_updatecolors",	// [byte] [byte]
	"svc_particle",		// [vec3] <variable>
	"svc_damage",		// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	"OBSOLETE svc_spawnbinary",
	"svc_spawnbaseline",

	"svc_temp_entity",	// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",		// [string] music [string] text
	"svc_cdtrack",		// [byte] track [byte] looptrack
	"svc_sellscreen",
	"svc_cutscene"
};


entity_t *CL_EntityNum(int num)
{ // This error checks and tracks the total number of entities
	if (num >= cl.num_entities) {
		if (num >= MAX_EDICTS)
			Host_Error("CL_EntityNum: %i is an invalid number",num);
		while (cl.num_entities <= num) {
			cl_entities[cl.num_entities].colormap = vid.colormap;
			cl.num_entities++;
		}
	}
	return &cl_entities[num];
}

void CL_ParseStartSoundPacket()
{
	int mask = MSG_ReadByte();
	int vol = mask&SND_VOLUME ? MSG_ReadByte() :DEFAULT_SOUND_PACKET_VOLUME;
	float atn = mask & SND_ATTENUATION ? MSG_ReadByte() / 64.0
			: DEFAULT_SOUND_PACKET_ATTENUATION;
	int ch = MSG_ReadShort();
	int sound_num = MSG_ReadByte();
	int ent = ch >> 3;
	ch &= 7;
	if (ent > MAX_EDICTS)
		Host_Error("CL_ParseStartSoundPacket: ent = %i", ent);
	vec3_t pos;
	for (int i = 0; i < 3; i++)
		pos[i] = MSG_ReadCoord();
	S_StartSound(ent,ch,cl.sound_precache[sound_num],pos,vol/255.0,atn);
}

void CL_KeepaliveMessage() // When the client is taking a long time to load
{ // stuff, send keepalive messages so the server doesn't disconnect.
	static float lastmsg;
	if (sv.active || cls.demoplayback)
		return; // no need if server is local
	sizebuf_t old = net_message; // read messages from server
	byte olddata[8192]; // should just be nops
	memcpy(olddata, net_message.data, net_message.cursize);
	int ret;
	do {
		ret = CL_GetMessage();
		switch (ret) {
		default:
			Host_Error("CL_KeepaliveMessage: CL_GetMessage failed");
		case 0:
			break;	// nothing waiting
		case 1:
			Host_Error("CL_KeepaliveMessage: received a message");
			break;
		case 2:
			if (MSG_ReadByte() != svc_nop)
				Host_Error("CL_KeepaliveMessage: datagram wasn't a nop");
			break;
		}
	} while (ret);
	net_message = old;
	memcpy(net_message.data, olddata, net_message.cursize);
	float time = Sys_FloatTime(); // check time
	if (time - lastmsg < 5)
		return;
	lastmsg = time;
	Con_Printf("--> client to server keepalive\n"); // write out a nop
	MSG_WriteByte(&cls.message, clc_nop);
	NET_SendMessage(cls.netcon, &cls.message);
	SZ_Clear(&cls.message);
}

void CL_ParseServerInfo()
{
	Con_DPrintf("Serverinfo packet received.\n");
	CL_ClearState(); // wipe the client_state_t struct
	int i = MSG_ReadLong(); // parse protocol version number
	if (i != PROTOCOL_VERSION) {
		Con_Printf("Server returned version %i, not %i", i,
			   PROTOCOL_VERSION);
		return;
	}
	cl.maxclients = MSG_ReadByte(); // parse maxclients
	if (cl.maxclients < 1 || cl.maxclients > MAX_SCOREBOARD) {
		Con_Printf("Bad maxclients (%u) from server\n", cl.maxclients);
		return;
	}
	cl.scores = Hunk_AllocName(cl.maxclients*sizeof(*cl.scores), "scores");
	cl.gametype = MSG_ReadByte(); // parse gametype
	char *str = MSG_ReadString(); // parse signon message
	strncpy(cl.levelname, str, sizeof(cl.levelname) - 1);
	// seperate the printfs so the server message can have a color
	Con_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_Printf("%c%s\n", 2, str);
	// first we go through and touch all of the precache data that still
	// happens to be in the cache, so precaching something else doesn't
	// needlessly purge it
	char model_precache[MAX_MODELS][MAX_QPATH];
	memset(cl.model_precache, 0, sizeof(cl.model_precache));
	int nummodels = 1;
	for (;; nummodels++) {
		str = MSG_ReadString();
		if (!str[0])
			break;
		if (nummodels == MAX_MODELS) {
			Con_Printf("Server sent too many model precaches\n");
			return;
		}
		strcpy(model_precache[nummodels], str);
		Mod_TouchModel(str);
	}
	char sound_precache[MAX_SOUNDS][MAX_QPATH];
	memset(cl.sound_precache, 0, sizeof(cl.sound_precache));
	int numsounds = 1;
	for (;; numsounds++) {
		str = MSG_ReadString();
		if (!str[0])
			break;
		if (numsounds == MAX_SOUNDS) {
			Con_Printf("Server sent too many sound precaches\n");
			return;
		}
		strcpy(sound_precache[numsounds], str);
		S_TouchSound(str);
	}
	// now we try to load everything else until a cache allocation fails
	for (i = 1; i < nummodels; i++) {
		cl.model_precache[i] = Mod_ForName(model_precache[i], false);
		if (cl.model_precache[i] == NULL) {
			Con_Printf("Model %s not found\n", model_precache[i]);
			return;
		}
		CL_KeepaliveMessage();
	}
	for (i = 1; i < numsounds; i++) {
		cl.sound_precache[i] = S_PrecacheSound(sound_precache[i]);
		CL_KeepaliveMessage();
	}
	cl_entities[0].model=cl.worldmodel=cl.model_precache[1]; // local state
	R_NewMap();
	Hunk_Check(); // make sure nothing is hurt
	noclip_anglehack = false; // noclip is turned off at start        
}

// Parse an entity update message from the server
// If an entities model or origin changes from frame to frame, it must be
// relinked. Other attributes can change without relinking.
void CL_ParseUpdate(int bits)
{
	if (cls.signon == SIGNONS - 1) {//first update is the final signon stage
		cls.signon = SIGNONS;
		CL_SignonReply();
	}
	int i;
	if (bits & U_MOREBITS) {
		i = MSG_ReadByte();
		bits |= (i << 8);
	}
	int num = bits & U_LONGENTITY ? MSG_ReadShort() : MSG_ReadByte();
	entity_t *ent = CL_EntityNum(num);
	for (i = 0; i < 16; i++)
		if (bits & (1 << i))
			bitcounts[i]++;
	qboolean forcelink = ent->msgtime != cl.mtime[1]; // true - no previous frame to lerp from
	ent->msgtime = cl.mtime[0];
	int modnum;
	if (bits & U_MODEL) {
		modnum = MSG_ReadByte();
		if (modnum >= MAX_MODELS)
			Host_Error("CL_ParseModel: bad modnum");
	} else
		modnum = ent->baseline.modelindex;
	model_t *model = cl.model_precache[modnum];
	if (model != ent->model) {
		ent->model = model;
		// automatic animation (torches, etc) can be either all together
		// or randomized
		if (model) {
			if (model->synctype == ST_RAND)
				ent->syncbase = (float)(rand()&0x7fff) / 0x7fff;
			else
				ent->syncbase = 0.0;
		} else
			forcelink = true; //hack to make null model players work
	}
	ent->frame = bits & U_FRAME ? MSG_ReadByte() : ent->baseline.frame;
	i = bits & U_COLORMAP ? MSG_ReadByte() : ent->baseline.colormap;
	if (!i)
		ent->colormap = vid.colormap;
	else {
		if (i > cl.maxclients)
			Sys_Error("i >= cl.maxclients");
		ent->colormap = cl.scores[i - 1].translations;
	}
	ent->skinnum = bits & U_SKIN ? MSG_ReadByte() : ent->baseline.skin;
	ent->effects = bits & U_EFFECTS ? MSG_ReadByte() :ent->baseline.effects;
	// shift the known values for interpolation
	VectorCopy(ent->msg_origins[0], ent->msg_origins[1]);
	VectorCopy(ent->msg_angles[0], ent->msg_angles[1]);
	ent->msg_origins[0][0] = bits & U_ORIGIN1 ? MSG_ReadCoord()
		: ent->baseline.origin[0];
	ent->msg_angles[0][0] = bits & U_ANGLE1 ? MSG_ReadAngle()
		: ent->baseline.angles[0];
	ent->msg_origins[0][1] = bits & U_ORIGIN2 ? MSG_ReadCoord()
		: ent->baseline.origin[1];
	ent->msg_angles[0][1] = bits & U_ANGLE2 ? MSG_ReadAngle()
		: ent->baseline.angles[1];
	ent->msg_origins[0][2] = bits & U_ORIGIN3 ? MSG_ReadCoord()
		: ent->baseline.origin[2];
	ent->msg_angles[0][2] = bits & U_ANGLE3 ? MSG_ReadAngle()
		: ent->baseline.angles[2];
	ent->forcelink =  bits & U_NOLERP;
	if (forcelink) { // didn't have an update last message
		VectorCopy(ent->msg_origins[0], ent->msg_origins[1]);
		VectorCopy(ent->msg_origins[0], ent->origin);
		VectorCopy(ent->msg_angles[0], ent->msg_angles[1]);
		VectorCopy(ent->msg_angles[0], ent->angles);
		ent->forcelink = true;
	}
}

void CL_ParseBaseline(entity_t *ent)
{
	ent->baseline.modelindex = MSG_ReadByte();
	ent->baseline.frame = MSG_ReadByte();
	ent->baseline.colormap = MSG_ReadByte();
	ent->baseline.skin = MSG_ReadByte();
	for (int i = 0; i < 3; i++) {
		ent->baseline.origin[i] = MSG_ReadCoord();
		ent->baseline.angles[i] = MSG_ReadAngle();
	}
}

void CL_ParseClientdata(int bits)
{ // Server information pertaining to this client only
	cl.viewheight = bits&SU_VIEWHEIGHT? MSG_ReadChar(): DEFAULT_VIEWHEIGHT;
	cl.idealpitch = bits & SU_IDEALPITCH ? MSG_ReadChar() : 0;
	VectorCopy(cl.mvelocity[0], cl.mvelocity[1]);
	for (int i = 0; i < 3; i++) {
		cl.punchangle[i] = bits & (SU_PUNCH1 << i) ? MSG_ReadChar() : 0;
		cl.mvelocity[0][i]=bits&(SU_VELOCITY1<<i)? MSG_ReadChar()*16: 0;
	}
	// [always sent]        if (bits & SU_ITEMS)
	int i = MSG_ReadLong();
	if (cl.items != i) { // set flash times
		Sbar_Changed();
		for (int j = 0; j < 32; j++)
			if ((i & (1 << j)) && !(cl.items & (1 << j)))
				cl.item_gettime[j] = cl.time;
		cl.items = i;
	}
	cl.onground = (bits & SU_ONGROUND) != 0;
	cl.inwater = (bits & SU_INWATER) != 0;
	cl.stats[STAT_WEAPONFRAME] = bits & SU_WEAPONFRAME ? MSG_ReadByte() : 0;
	i = bits & SU_ARMOR ? MSG_ReadByte() : 0;
	if (cl.stats[STAT_ARMOR] != i) {
		cl.stats[STAT_ARMOR] = i;
		Sbar_Changed();
	}
	i = bits & SU_WEAPON ? MSG_ReadByte() : 0;
	if (cl.stats[STAT_WEAPON] != i) {
		cl.stats[STAT_WEAPON] = i;
		Sbar_Changed();
	}
	i = MSG_ReadShort();
	if (cl.stats[STAT_HEALTH] != i) {
		cl.stats[STAT_HEALTH] = i;
		Sbar_Changed();
	}
	i = MSG_ReadByte();
	if (cl.stats[STAT_AMMO] != i) {
		cl.stats[STAT_AMMO] = i;
		Sbar_Changed();
	}
	for (i = 0; i < 4; i++) {
		int j = MSG_ReadByte();
		if (cl.stats[STAT_SHELLS + i] != j) {
			cl.stats[STAT_SHELLS + i] = j;
			Sbar_Changed();
		}
	}
	i = MSG_ReadByte();
	if (standard_quake) {
		if (cl.stats[STAT_ACTIVEWEAPON] != i) {
			cl.stats[STAT_ACTIVEWEAPON] = i;
			Sbar_Changed();
		}
	} else {
		if (cl.stats[STAT_ACTIVEWEAPON] != (1 << i)) {
			cl.stats[STAT_ACTIVEWEAPON] = (1 << i);
			Sbar_Changed();
		}
	}
}

void CL_NewTranslation(int slot)
{
	if (slot > cl.maxclients)
		Sys_Error("CL_NewTranslation: slot > cl.maxclients");
	byte *dest = cl.scores[slot].translations;
	byte *source = vid.colormap;
	memcpy(dest, vid.colormap, sizeof(cl.scores[slot].translations));
	int top = cl.scores[slot].colors & 0xf0;
	int bottom = (cl.scores[slot].colors & 15) << 4;
	for (int i = 0; i < VID_GRADES; i++, dest += 256, source += 256) {
		if (top < 128)	// the artists made some backwards ranges. sigh.
			memcpy(dest + TOP_RANGE, source + top, 16);
		else
			for (int j = 0; j < 16; j++)
				dest[TOP_RANGE + j] = source[top + 15 - j];
		if (bottom < 128)
			memcpy(dest + BOTTOM_RANGE, source + bottom, 16);
		else
			for (int j = 0; j < 16; j++)
				dest[BOTTOM_RANGE + j]= source[bottom + 15 - j];
	}
}

void CL_ParseStatic()
{
	int i = cl.num_statics;
	if (i >= MAX_STATIC_ENTITIES)
		Host_Error("Too many static entities");
	entity_t *ent = &cl_static_entities[i];
	cl.num_statics++;
	CL_ParseBaseline(ent);
	// copy it to the current state
	ent->model = cl.model_precache[ent->baseline.modelindex];
	ent->frame = ent->baseline.frame;
	ent->colormap = vid.colormap;
	ent->skinnum = ent->baseline.skin;
	ent->effects = ent->baseline.effects;
	VectorCopy(ent->baseline.origin, ent->origin);
	VectorCopy(ent->baseline.angles, ent->angles);
	R_AddEfrags(ent);
}

void CL_ParseStaticSound()
{
	vec3_t org;
	for (int i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord();
	int sound_num = MSG_ReadByte();
	int vol = MSG_ReadByte();
	int atten = MSG_ReadByte();
	S_StaticSound(cl.sound_precache[sound_num], org, vol, atten);
}


void CL_ParseServerMessage()
{
	if (cl_shownet.value == 1) // if recording demos, copy the message out
		Con_Printf("%i ", net_message.cursize);
	else if (cl_shownet.value == 2)
		Con_Printf("------------------\n");
	cl.onground = false; // unless the server says otherwise     
	MSG_BeginReading(); // parse the message
	while (1) {
		if (msg_badread)
			Host_Error("CL_ParseServerMessage: Bad server message");
		int cmd = MSG_ReadByte();
		if (cmd == -1) {
			SHOWNET("END OF MESSAGE");
			return;	// end of message
		}
		// if the high bit of the command byte is set it's a fast update
		if (cmd & 128) {
			SHOWNET("fast update");
			CL_ParseUpdate(cmd & 127);
			continue;
		}
		SHOWNET(svc_strings[cmd]);
		int i;
		switch (cmd) { // other commands
		default:
			Host_Error("CL_ParseServerMessage: Illegible server message\n");
			break;
		case svc_nop:
			break;
		case svc_time:
			cl.mtime[1] = cl.mtime[0];
			cl.mtime[0] = MSG_ReadFloat();
			break;
		case svc_clientdata:
			i = MSG_ReadShort();
			CL_ParseClientdata(i);
			break;
		case svc_version:
			i = MSG_ReadLong();
			if (i != PROTOCOL_VERSION)
				Host_Error ("CL_ParseServerMessage: Server is protocol %i instead of %i\n", i, PROTOCOL_VERSION);
			break;
		case svc_disconnect:
			Host_EndGame("Server disconnected\n");
			break;
		case svc_print:
			Con_Printf("%s", MSG_ReadString());
			break;
		case svc_centerprint:
			SCR_CenterPrint(MSG_ReadString());
			break;
		case svc_stufftext:
			Cbuf_AddText(MSG_ReadString());
			break;
		case svc_damage:
			V_ParseDamage();
			break;
		case svc_serverinfo:
			CL_ParseServerInfo();
			vid.recalc_refdef = true; // leave intermission full screen
			break;
		case svc_setangle:
			for (i = 0; i < 3; i++)
				cl.viewangles[i] = MSG_ReadAngle();
			break;
		case svc_setview:
			cl.viewentity = MSG_ReadShort();
			break;
		case svc_lightstyle:
			i = MSG_ReadByte();
			if (i >= MAX_LIGHTSTYLES)
				Sys_Error("svc_lightstyle > MAX_LIGHTSTYLES");
			Q_strcpy(cl_lightstyle[i].map, MSG_ReadString());
			cl_lightstyle[i].length =
			    Q_strlen(cl_lightstyle[i].map);
			break;
		case svc_sound:
			CL_ParseStartSoundPacket();
			break;
		case svc_stopsound:
			i = MSG_ReadShort();
			S_StopSound(i >> 3, i & 7);
			break;
		case svc_updatename:
			Sbar_Changed();
			i = MSG_ReadByte();
			if (i >= cl.maxclients)
				Host_Error
				    ("CL_ParseServerMessage: svc_updatename > MAX_SCOREBOARD");
			strcpy(cl.scores[i].name, MSG_ReadString());
			break;
		case svc_updatefrags:
			Sbar_Changed();
			i = MSG_ReadByte();
			if (i >= cl.maxclients)
				Host_Error
				    ("CL_ParseServerMessage: svc_updatefrags > MAX_SCOREBOARD");
			cl.scores[i].frags = MSG_ReadShort();
			break;
		case svc_updatecolors:
			Sbar_Changed();
			i = MSG_ReadByte();
			if (i >= cl.maxclients)
				Host_Error ("CL_ParseServerMessage: svc_updatecolors > MAX_SCOREBOARD");
			cl.scores[i].colors = MSG_ReadByte();
			CL_NewTranslation(i);
			break;
		case svc_particle:
			R_ParseParticleEffect();
			break;
		case svc_spawnbaseline:
			i = MSG_ReadShort();
			// must use CL_EntityNum() to force cl.num_entities up
			CL_ParseBaseline(CL_EntityNum(i));
			break;
		case svc_spawnstatic:
			CL_ParseStatic();
			break;
		case svc_temp_entity:
			CL_ParseTEnt();
			break;
		case svc_setpause:
			cl.paused = MSG_ReadByte();
			break;
		case svc_signonnum:
			i = MSG_ReadByte();
			if (i <= cls.signon)
				Host_Error("Received signon %i when at %i", i,
					   cls.signon);
			cls.signon = i;
			CL_SignonReply();
			break;
		case svc_killedmonster:
			cl.stats[STAT_MONSTERS]++;
			break;
		case svc_foundsecret:
			cl.stats[STAT_SECRETS]++;
			break;
		case svc_updatestat:
			i = MSG_ReadByte();
			if (i < 0 || i >= MAX_CL_STATS)
				Sys_Error("svc_updatestat: %i is invalid", i);
			cl.stats[i] = MSG_ReadLong();;
			break;
		case svc_spawnstaticsound:
			CL_ParseStaticSound();
			break;
		case svc_cdtrack:
			cl.cdtrack = MSG_ReadByte();
			cl.looptrack = MSG_ReadByte();
			if ((cls.demoplayback || cls.demorecording) && (cls.forcetrack != -1))
				BGM_PlayCDtrack((byte)cls.forcetrack, true);
			else
				BGM_PlayCDtrack((byte)cl.cdtrack, true);
			break;
		case svc_intermission:
			cl.intermission = 1;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true; // go to full screen
			break;
		case svc_finale:
			cl.intermission = 2;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true; // go to full screen
			SCR_CenterPrint(MSG_ReadString());
			break;
		case svc_cutscene:
			cl.intermission = 3;
			cl.completed_time = cl.time;
			vid.recalc_refdef = true; // go to full screen
			SCR_CenterPrint(MSG_ReadString());
			break;
		case svc_sellscreen:
			Cmd_ExecuteString("help", src_command);
			break;
		}
	}
}
