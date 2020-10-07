/*
 *	scrlog_III.cpp:
 *	     This file contains the hookers for GTA 3 (all versions);
 *
 *  Issues:
 *		*No CollectGlobalVariableIndex or CollectVariablePointer (game fault)
 *      *If using CLEO, this must be loaded after CLEO.asi (not really a issue, just a advice),
 *        hopefully, this may happen always since scrlog.asi is after CLEO.asi in alphabetic order
 *      *CLEO hooks all commands with label as parameter, so... yeah, we cant show label params if CLEO is installed
 *      *Many of the Vice's script engine stuff is inlined, and when inlined we wont have access to the values since
 *        no func is called.
 *
 *  LICENSE:
 *		 (c) 2020 - Junior_Djjr
 *		 (c) 2013 - LINK/2012 - <dma_2012@hotmail.com>
 *
 *		 Permission is hereby granted, free of charge, to any person obtaining a copy
 *		 of this plugin and source code, to copy, modify, merge, publish and/or distribute
 *		 as long as you leave the original credits (above copyright notice)
 *		 together with your derived work. Please not you are NOT permited to sell or get money from it
 *
 *		 THE SOFTWARE AND SOURCE CODE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *		 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *		 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *		 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *		 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *		 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *		 THE SOFTWARE.
 *
 */
#include "GameInfo.h"
#include "Injector.h"
#include "scrlog.h"

// Some macros to help in saving important registers being used
#define push_regs(a, b)	_asm push a _asm push b
#define pop_regs(a, b)  _asm pop  b _asm pop  a

static void PatchALL(bool bSteam);
void III_Patch(GameInfo& info)
{
	PatchALL(info.IsSteam());
}

static void* CRunningScript__ProcessOneCommand;
static void HOOK_RegisterCommand();
static void HOOK_AfterScripts();
static void HOOK_RegisterScript();
static void HOOK_CalledCollectParameters();
static void HOOK_RegisterCollectedDatatype();
static void HOOK_RegisterCollectedParam();
static void HOOK_CalledStoreParameters();
static void HOOK_CalledCollectVariablePointer();
static void HOOK_RegisterCollectVariablePointer();

static uint32_t phAfterScriptsOriginal;
static uint32_t phAfterScriptsRet;
extern uint32_t pCheatString;
extern uint32_t pScriptsProcessed;

// Structure to store addresses for each version, used in CommonPatch() function
struct CommonPatchInfo
{
	uint32_t pCheatString;
	uint32_t pScriptsProcessed;
	uint32_t phAfterScripts;
	uint32_t phRegisterScript;
	uint32_t phRegisterCommand;
	uint32_t phCollectParameters;
	uint32_t phCollectedDatatype;
	uint32_t phCollectedValue;
	uint32_t phStoreParameters;
	uint32_t phUpdateCompareFlag;
	uint32_t CollectiveArray;
	uint32_t ScriptsUpdated;
	uint32_t ScriptSpace;
	uint32_t MissionSpace;
	size_t   ScriptSpaceSize;
	size_t   MissionSpaceSize;
};

// Patches based on the sent CommonPatchInfo structure
static void CommonPatch(CommonPatchInfo& c)
{
	uint32_t ptr;
	
	c.ScriptSpaceSize = 131072;
	c.MissionSpaceSize = 32768;

	// Get pointers for scrlog
	{
		SCRLog::ScriptSpace      = ReadMemory<char*>(c.ScriptSpace, true);
		SCRLog::MissionSpace     = SCRLog::ScriptSpace + c.ScriptSpaceSize;
		SCRLog::CollectiveArray  = ReadMemory<ScriptVar*>(c.CollectiveArray, true);
		SCRLog::ScriptsUpdated   = ReadMemory<short*>(c.ScriptsUpdated, true);

		SCRLog::ScriptSpaceEnd =  SCRLog::ScriptSpace + c.ScriptSpaceSize;
		SCRLog::MissionSpaceEnd = SCRLog::MissionSpace+ c.MissionSpaceSize;
	}

	// RegisterScript
	if(SCRLog::bHookRegisterScript)
	{
		ptr = c.phRegisterScript;
		MakeCALL(ptr, &HOOK_RegisterScript);
		MakeNOP(ptr+5, 1);
	}

	if (SCRLog::bHookAfterScripts) {
		ptr = c.phAfterScripts;
		if (ptr) {
			phAfterScriptsRet = ptr + 5;
			phAfterScriptsOriginal = GetAbsoluteOffset(ReadMemory<uintptr_t>(ptr + 1, true), phAfterScriptsRet);
			MakeCALL(ptr, &HOOK_AfterScripts);
		}
	}
	pCheatString = c.pCheatString;
	pScriptsProcessed = c.pScriptsProcessed;

	if (!SCRLog::bHookOnlyRegisterScript)
	{
		// RegisterCommand
		if (SCRLog::bHookRegisterCommand)
		{
			ptr = c.phRegisterCommand;
			CRunningScript__ProcessOneCommand = (void*)GetAbsoluteOffset(ReadMemory<int>(ptr + 1, true), ptr + 5);
			MakeCALL(ptr, &HOOK_RegisterCommand);
		}

		// Called CollectParameters
		if (SCRLog::bHookCollectParam)
		{
			ptr = c.phCollectParameters;
			MakeCALL(ptr, &HOOK_CalledCollectParameters);
			MakeNOP(ptr + 5, 1);
		}

		// Register collected datatype
		if (SCRLog::bHookFindDatatype)
		{
			ptr = c.phCollectedDatatype;
			MakeCALL(ptr, &HOOK_RegisterCollectedDatatype);
		}

		// Register collected value
		if (SCRLog::bHookCollectParam)
		{
			ptr = c.phCollectedValue;
			MakeCALL(ptr, &HOOK_RegisterCollectedParam);
		}

		// Register call to store parameters
		if (SCRLog::bHookStoreParam)
		{
			ptr = c.phStoreParameters;
			MakeCALL(ptr, &HOOK_CalledStoreParameters);
		}

		// Replace UpdateCompareFlag
		if (SCRLog::bHookUpdateCompareFlag)
		{
			ptr = c.phUpdateCompareFlag;
			MakeJMP(ptr, SCRLog::New_CRunningScript__UpdateCompareFlag);
			MakeNOP(ptr + 5, 2);
		}
	}
}


// All versions have the same addresses
static void PatchALL(bool bSteam)
{
	CommonPatchInfo c;

	if (!bSteam) { // not tested on non-1.0
		c.phAfterScripts = 0x4394F7;
		c.pCheatString = 0x885B90;
		c.pScriptsProcessed = 0x95CC5E;
	}
	else {
		c.phAfterScripts = 0;
		c.pCheatString = 0;
		c.pScriptsProcessed = 0;
	}
	c.phRegisterScript = 0x439441;
	c.phRegisterCommand = 0x439485;
	c.phCollectParameters = 0x4382E9;
	c.phCollectedDatatype = 0x438300;
	c.phCollectedValue = 0x438447;
	c.phStoreParameters = 0x4385A7;
	c.phUpdateCompareFlag = 0x44FD90;
	
	c.ScriptsUpdated = 0x4393D3;
	c.CollectiveArray = 0x43835A;
	c.ScriptSpace = c.phCollectedDatatype + 1;
	c.MissionSpace = 0;

	CommonPatch(c);
}


void __declspec(naked) HOOK_RegisterScript()
{
	_asm
	{
		mov ebx, ecx			// Replaced code

		push ecx
		call SCRLog::RegisterScript

		// Run replaced code and go back to normal flow
		mov ecx, ebx
		cmp byte ptr [ecx+0x79], 0
		retn
	}
}

void __declspec(naked) HOOK_AfterScripts()
{
	_asm
	{
		call SCRLog::AfterScripts
		pop ebx
		retn
	}
}

void __declspec(naked) HOOK_RegisterCommand()
{
	_asm
	{
		call SCRLog::RegisterCommand

		// Back
		mov ecx, ebx
		call CRunningScript__ProcessOneCommand
		retn
	}
}

void __declspec(naked) HOOK_CalledCollectParameters()
{
	_asm
	{
		mov ebx, dword ptr [esp+0x28+0x8+0x4]	// count

		push ecx
		push ebx
		call SCRLog::RegisterCallToCollectParameters
		pop ecx

		// Run replaced code and go back to normal flow
		cmp ebx, 0
		retn
	}
}


void __declspec(naked) HOOK_RegisterCollectedDatatype()
{
	_asm
	{
		xor eax, eax
		mov edx, dword ptr [ebx]		// CRunningScript.ip
		add edx, dword ptr [SCRLog::ScriptSpace]
		mov al, byte ptr [edx]
		mov SCRLog::Datatype, eax

		// Run replaced code and go back to normal flow
		mov eax, SCRLog::ScriptSpace
		retn
	}
}

void __declspec(naked) HOOK_RegisterCollectedParam()
{
	_asm
	{
		push ebp
		call SCRLog::RegisterCollectionAtIndex

		// Jump back
		cmp bp, word ptr [esp+0x28+0x8+0x4]
		retn
	}
}

void __declspec(naked) HOOK_CalledStoreParameters()
{
	_asm
	{
		push ecx
		push edi
		mov  SCRLog::Datatype, 1
		call SCRLog::RegisterCallToStoreParameters
		pop ecx

		// Run replaced code and go back to normal flow
		xor esi, esi
		test di, di
		retn
	}
}

void IIICHudSetHelpMessage(wchar_t *message, bool quick) {
	((void(__cdecl *)(wchar_t *, bool))0x5051E0)(message, quick);
}
