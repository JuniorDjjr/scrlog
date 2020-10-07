/*
 *	scrlog_SA.cpp:
 *	     This file contains the hookers for GTA San Andreas (all versions);
 *
 *  Issues:
 *       Collect global var index not hooked in DEViANCE 1.01EU
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


static void Patch10();
static void Patch11();
static void Patch30();

void SA_Patch(GameInfo& info)
{
	int major = info.GetMajorVersion(), minor = info.GetMinorVersion();

	if(major == 1 && minor == 0)
		Patch10();
	else if(major == 1 && minor == 1)
		Patch11();
	else if(major == 3)
		Patch30();
}

static void* pb;								// Used to store somewhere to return
static void* CommandsExecuted;

static void HOOK_RegisterCommand();
static void HOOK_AfterScripts();
static void HOOK_RegisterScript();

static char* pbCollectParam_for;
static char* pbCollectParam_ret;
static void HOOK_R_CalledCollectParameters();	// retail
static void HOOK_S_CalledCollectParameters();	// steam
static void HOOK_RegisterCollectedDatatype();
static void HOOK_RegisterCollectedParam();
static void HOOK_CalledStoreParameters();

static char* pbCollectString;
static void HOOK_CalledCollectString();
static void HOOK_RegisterCollectedString();		// Used in HOOK_CalledCollectString, not in Patch

static void HOOK_CalledGlobalVariableIndex();
static void HOOK_CalledCollectVariablePointer();
static void HOOK_RegisterGlobalVariableIndex();
static void HOOK_RegisterCollectVariablePointer();

static uint32_t phAfterScriptsOriginal;
static uint32_t phAfterScriptsRet;
extern uint32_t pCheatString;
extern uint32_t pScriptsProcessed;

// Structure to store addresses for each version, used in CommonPatch() function
struct CommonPatchInfo
{
	bool steam;
	uint32_t pCheatString;
	uint32_t pScriptsProcessed;
	uint32_t phAfterScripts;
	uint32_t phRegisterScript;
	uint32_t phRegisterCommand;
	uint32_t phCollectParameters;
	uint32_t phCollectVariablePointer;
	uint32_t phCollectGlobalVariableOffset;
	uint32_t phCollectedDatatype;
	uint32_t phCollectedValue;
	uint32_t phStoreParameters;
	uint32_t phCollectString;
	uint32_t phUpdateCompareFlag;
	uint32_t MissionLocals;
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

	//
	c.ScriptSpaceSize = 0x30D40;
	c.MissionSpaceSize = 0x10D88;

	// Get pointers for scrlog
	{
		SCRLog::ScriptSpace      = ReadMemory<char*>(c.ScriptSpace, true);
		SCRLog::MissionSpace     = ReadMemory<char*>(c.MissionSpace, true);
		SCRLog::MissionLocals    = ReadMemory<ScriptVar*>(c.MissionLocals, true);
		SCRLog::CollectiveArray  = ReadMemory<ScriptVar*>(c.CollectiveArray, true);
		SCRLog::ScriptsUpdated   = ReadMemory<short*>(c.ScriptsUpdated, true);

		SCRLog::ScriptSpaceEnd =  SCRLog::ScriptSpace + c.ScriptSpaceSize;
		SCRLog::MissionSpaceEnd = SCRLog::MissionSpace+ c.MissionSpaceSize;

		CommandsExecuted = ReadMemory<void*>(c.phRegisterCommand+3, true);
	}

	// RegisterScript
	if(SCRLog::bHookRegisterScript)
	{
		ptr = c.phRegisterScript;
		MakeCALL(ptr, &HOOK_RegisterScript);
		MakeNOP(ptr+5, c.steam? 2 : 1);
	}

	if (SCRLog::bHookAfterScripts) {
		ptr = c.phAfterScripts;
		if (ptr) {
			//phAfterScriptsRet = ptr + 5;
			//phAfterScriptsOriginal = GetAbsoluteOffset(ReadMemory<uintptr_t>(ptr + 1, true), phAfterScriptsRet);
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
			MakeCALL(ptr, &HOOK_RegisterCommand);
			MakeNOP(ptr + 5, 2);
		}

		// Called CollectParameters
		if (SCRLog::bHookCollectParam)
		{
			ptr = c.phCollectParameters;
			MakeCALL(ptr, c.steam ? &HOOK_S_CalledCollectParameters : &HOOK_R_CalledCollectParameters);
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
			pbCollectParam_for = (char*)(void*)GetAbsoluteOffset(ReadMemory<int>(ptr + 2, true), ptr + 6);
			pbCollectParam_ret = (char*)(ptr + 6);
			MakeJMP(ptr, &HOOK_RegisterCollectedParam);
			MakeNOP(ptr + 5, 1);
		}

		// Register call to store parameters
		if (SCRLog::bHookStoreParam)
		{
			ptr = c.phStoreParameters;
			MakeCALL(ptr, &HOOK_CalledStoreParameters);
		}

		// Register call to collect string parameter
		if (SCRLog::bHookCollectString)
		{
			ptr = c.phCollectString;
			MakeJMP(ptr, &HOOK_CalledCollectString);
			MakeNOP(ptr + 5, 1);
			pbCollectString = (char*)(ptr + 6);
		}

		// Replace UpdateCompareFlag
		if (SCRLog::bHookUpdateCompareFlag)
		{
			ptr = c.phUpdateCompareFlag;
			MakeJMP(ptr, SCRLog::New_CRunningScript__UpdateCompareFlag);
			MakeNOP(ptr + 5, c.steam ? 2 : 1);
		}

		//
		if (SCRLog::bHookCollectVarPointer)
		{
			if (c.phCollectGlobalVariableOffset)
				MakeCALL(c.phCollectGlobalVariableOffset, &HOOK_CalledGlobalVariableIndex);

			ptr = c.phCollectVariablePointer;
			MakeCALL(ptr, &HOOK_CalledCollectVariablePointer);
			MakeNOP(ptr + 5, 1);
		}
	}
}

// Setup CommonPatchInfo for 1.0 executable and send it to CommonPatch
static void Patch10()
{
	CommonPatchInfo c;
	c.steam = false;
	c.pCheatString = 0x969110;
	c.pScriptsProcessed = 0xA447F8;
	c.phAfterScripts = 0x469FFB; // old version: 0x53BFE2
	c.phRegisterScript = 0x469F04;
	c.phRegisterCommand = 0x469FB0;
	c.phCollectParameters = 0x464089;
	c.phCollectedDatatype = 0x4640A0;
	c.phCollectedValue = 0x464214;
	c.phStoreParameters = 0x464370;
	c.phCollectString = 0x463D50;
	c.phUpdateCompareFlag = 0x4859D0;
	c.phCollectGlobalVariableOffset = 0x0;
	c.phCollectVariablePointer = 0x464790;


	if(ReadMemory<uint8_t>(0x464700, true) == 0xE9)	// Is HOODLUM?
		c.phCollectGlobalVariableOffset = 0x156F341;
	else
		c.phCollectGlobalVariableOffset = 0x464701;

	c.MissionLocals = 0x46410B;
	c.ScriptSpace = 0x4640E0;
	c.MissionSpace = 0x4899D7;
	c.ScriptsUpdated = 0x46A1F3;
	c.CollectiveArray = c.phCollectParameters + 1;
	CommonPatch(c);
}

// Yes sir
static void Patch11()
{
	CommonPatchInfo c;
	c.steam = false;
	c.pCheatString = 0x0;
	c.phAfterScripts = 0x0;
	c.pScriptsProcessed = 0x0;
	c.phRegisterScript = 0x469F84;
	c.phRegisterCommand = 0x46A030;
	c.phCollectParameters = 0x464109;
	c.phCollectedDatatype = 0x464120;
	c.phCollectedValue = 0x464294;
	c.phStoreParameters = 0x4643F0;
	c.phCollectString = 0x463DD0;
	c.phUpdateCompareFlag = 0x485A50;
	c.phCollectGlobalVariableOffset = 0x0;
	c.phCollectVariablePointer = 0x464810;

	if(ReadMemory<uint8_t>(0x464780, true) == 0xFF)	// Is DEViANCE?
		c.phCollectGlobalVariableOffset = 0x0;			// I have no idea where I should go on dev.
	else
		c.phCollectGlobalVariableOffset = 0x464781;

	c.MissionLocals = 0x46418B;
	c.ScriptSpace = 0x464160;
	c.MissionSpace = 0x489A57;
	c.ScriptsUpdated = 0x46A273;
	c.CollectiveArray = c.phCollectParameters + 1;
	CommonPatch(c);
}

// Steam version
static void Patch30()
{
	CommonPatchInfo c;
	c.steam = true;
	c.pCheatString = 0x0;
	c.phAfterScripts = 0x0;
	c.pScriptsProcessed = 0x0;
	c.phRegisterScript = 0x46F674;
	c.phRegisterCommand = 0x46F720;
	c.phCollectParameters = 0x469799;
	c.phCollectedDatatype = 0x4697A6;
	c.phCollectedValue = 0x46991B;
	c.phStoreParameters = 0x469A70;
	c.phCollectString = 0x469420;
	c.phUpdateCompareFlag = 0x48BF40;
	c.phCollectGlobalVariableOffset = 0x469DE1;
	c.phCollectVariablePointer = 0x469E80;

	c.MissionLocals = 0x469817;
	c.ScriptSpace = 0x4697ED;
	c.MissionSpace = 0x490715;
	c.ScriptsUpdated = 0x46F993;
	c.CollectiveArray = c.phCollectParameters + 1;
	CommonPatch(c);
}



void __declspec(naked) HOOK_RegisterScript()
{
	_asm
	{
		push esi
		call SCRLog::RegisterScript

		// Run replaced code and go back to normal flow
		mov eax, dword ptr [esi+0xD8]						// Retail
		test eax, eax										// Steam
		retn
	}
}

void __declspec(naked) HOOK_AfterScripts()
{
	_asm
	{
		call SCRLog::AfterScripts
		pop esi
		xor al, al
		pop ebx
		retn
	}
}

void __declspec(naked) HOOK_RegisterCommand()
{
	_asm
	{
		// Replaced code
		mov eax, CommandsExecuted
		inc dword ptr [eax]

		// Register command and go back to normal flow
		call SCRLog::RegisterCommand
		retn
	}
}

void __declspec(naked) HOOK_R_CalledCollectParameters()
{
	_asm
	{
		push_regs(eax, ecx)

		push eax
		call SCRLog::RegisterCallToCollectParameters
		mov SCRLog::Datatype, 1

		pop_regs(eax, ecx)

		// Run replaced code and go back to normal flow
		mov ebx, SCRLog::CollectiveArray
		test ax, ax
		retn
	}
}

void __declspec(naked) HOOK_S_CalledCollectParameters()
{
	_asm
	{
		push_regs(eax, ecx)

		push ebp
		call SCRLog::RegisterCallToCollectParameters
		mov SCRLog::Datatype, 1

		pop_regs(eax, ecx)

		// Run replaced code and go back to normal flow
		mov ebx, SCRLog::CollectiveArray
		test bp, bp
		retn
	}
}

void __declspec(naked) HOOK_RegisterCollectedDatatype()
{
	_asm
	{
		xor edx, edx
		mov eax, dword ptr [ecx+0x14]			// | Replaced code
		mov dl, byte ptr [eax]					// |

		cmp dl, 0x7		// may be array?
		jge _l1			// >>
		mov SCRLog::Datatype, edx
		retn

_l1:
		cmp dl, 0x8		// not int\float array?
		jg _unk			// >>

		// it is a array, check if float or int
		mov dh, byte ptr [eax+6]		// array flags
		and dh, 0x7F
		test dh, dh
		jz _int
		cmp dh, 1
		jz _float

_unk:
		mov SCRLog::Datatype, 1
		retn

_float:
		mov SCRLog::Datatype, 6
		retn

_int:
		mov SCRLog::Datatype, 1
		retn

	}
}

void __declspec(naked) HOOK_RegisterCollectedParam()
{
	_asm
	{
		push ecx		// Save this pointer

		mov eax, ebx				// ebx is current value in iteration over CollectiveArray
		sub eax, SCRLog::CollectiveArray
		shr eax, 2					// (eax >>= 2) = (eax /= 4)
		push eax
		call SCRLog::RegisterCollectionAtIndex

		pop ecx

		// Jump back
		test bp, bp
		jz _Return
		jmp pbCollectParam_for
_Return:
		jmp pbCollectParam_ret
		
	}
}

void __declspec(naked) HOOK_CalledStoreParameters()
{
	_asm
	{
		push ecx
		push dword ptr [esp+12]
		mov  SCRLog::Datatype, 1
		call SCRLog::RegisterCallToStoreParameters
		pop  ecx

		// Run replaced code and go back to normal flow
		mov ax, word ptr [esp+8]
		retn
	}
}

void __declspec(naked) HOOK_CalledCollectString()
{
	_asm
	{
		push ecx
		call SCRLog::RegisterCallToCollectString
		pop ecx

		pop pb								// Store the return ip into pb
		push HOOK_RegisterCollectedString	// and change the return ip to HOOK_RegisterCollectedString
											// NOTE: This is a HACK HACK HACK. Is not thread-safe!!

		// Run replaced code and go back to normal flow
		mov eax, dword ptr [ecx+0x14]
		sub esp, 8
		jmp pbCollectString
	}
}

void __declspec(naked) HOOK_RegisterCollectedString()
{
	_asm
	{
		// Gets the params to CollectString, currently they are a trash on the stack.
		mov edx, dword ptr [esp-4]		// maxlen
		mov ecx, dword ptr [esp-8]		// out

		push eax			// Save the return value.
							// I don't think this func return any value, but let's save it anyway
		
		push edx
		push ecx
		mov SCRLog::Datatype, 14
		call SCRLog::RegisterCollectedString

		// Ok, return to the right place
		pop eax
		jmp pb
	}
}


void __declspec(naked) HOOK_CalledGlobalVariableIndex()
{
	_asm
	{
		pop edx

		// Replace return ip
		pop pb
		push HOOK_RegisterGlobalVariableIndex

		push edx

		mov eax, dword ptr [ecx+0x14]		// | Replaced code
		mov dl, byte ptr [eax]				// |

		retn
	}
}

void __declspec(naked) HOOK_CalledCollectVariablePointer()
{
	_asm
	{
		pop edx

		// Replace return ip
		pop pb
		push HOOK_RegisterCollectVariablePointer

		sub esp, 8							// | Replaced code
		mov eax, dword ptr [ecx+0x14]		// |

		push edx
		retn
	}
}

void __declspec(naked) HOOK_RegisterCollectVariablePointer()
{
	_asm
	{
		mov SCRLog::Datatype, 1	// clear it
		push eax
		push eax
		call SCRLog::RegisterVariable
		pop eax
		jmp pb
	}
}

void __declspec(naked) HOOK_RegisterGlobalVariableIndex()
{
	_asm
	{
		push ecx
		push eax

		mov SCRLog::Datatype, 1	// clear it
		push eax
		call SCRLog::RegisterGlobalVariable

		pop eax
		pop ecx
		jmp pb
	}
}

void SACHudSetHelpMessage(char const* text, bool quickMessage, bool permanent, bool addToBrief) {
	((void(__cdecl *)(char const*, bool, bool, bool))0x588BE0)(text, quickMessage, permanent, addToBrief);
}
