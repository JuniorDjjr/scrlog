#pragma once
#include <cstdint>

#pragma pack(push, 1)

union ScriptVar	// sizeof = 0x4
{
	unsigned int  dwParam;
	int           nParam;
	float         fParam;
	void*         pParam;
	char*         szParam;
};
static_assert(sizeof(ScriptVar) == 0x4, "Incorrect struct size: ScriptVar");

struct CRunningScript_SA	// sizeof = 0xE0
{
	typedef CRunningScript_SA CRunningScript;

	CRunningScript* next;
	CRunningScript* prev;
	char scriptName[8];
	char* base;
	char* ip;
	char* gosub_stack[8];
	short gosub_index;
	short field_3A;
	ScriptVar tls[34];
	char isActive;
	char compareFlag;
	char missionFlag;
	char isExternal;
	char field_C8;
	char field_C9;
	char field_CA;
	char field_CB;
	int wakeTime;
	short logicalOp;
	char notFlag;
	char wastedBustedCheckEnabled;
	char wastedOrBusted;
	char field_D5;
	short field_D6;
	char* sceneSkipIP;
	char isMission;
	short CLEO_scmFunction;
	char CLEO_isCustom;
};
static_assert(sizeof(CRunningScript_SA) == 0xE0, "Incorrect struct size: CRunningScript_SA");


struct CRunningScript_VC	// sizeof = 0x88
{
	typedef CRunningScript_VC CRunningScript;

	CRunningScript* next;
	CRunningScript* prev;
	char scriptName[8];
//	char* base;
	char* ip;
	char* gosub_stack[6];
	short gosub_index;
	//short field_2E;
	short CLEO_isCustom;
	ScriptVar tls[18];
	char isActive;
	char compareFlag;
	char missionFlag;
	char notSleeps;
	int wakeTime;
	short logicalOp;
	char notFlag;
	char wastedBustedCheckEnabled;
	char wastedOrBusted;
	char field_85;			// missionFlag?
	short field_86;
};
static_assert(sizeof(CRunningScript_VC) == 0x88, "Incorrect struct size: CRunningScript_VC");

struct CRunningScript_III	// sizeof = 0x??
{
	typedef CRunningScript_III CRunningScript;

	CRunningScript* next;
	CRunningScript* prev;
	char scriptName[8];
//	char* base;
	char* ip;
	char* gosub_stack[6];
	short gosub_index;
	short field_2E;
	ScriptVar tls[18];
//	char isActive;
	char compareFlag;
	char missionFlag;
	char notSleeps;
	char _pad;// pad on here?
	int wakeTime;
	short logicalOp;
	char notFlag;
	char wastedBustedCheckEnabled;
	char wastedOrBusted;
	char field_85;			// missionFlag?
	short field_86;
};
//static_assert(sizeof(CRunningScript_III) == 0x??, "Incorrect struct size: CRunningScript_III");


#pragma pack(pop)
