/*
 *	scrlog.h:
 *	     All shared definitions between scrlog cpp units are defined here.
 *
 *  LICENSE:
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
#pragma once
class GameInfo;
union ScriptVar;

// Patches
extern void III_Patch(GameInfo& info);
extern void VC_Patch(GameInfo& info);
extern void SA_Patch(GameInfo& info);

namespace SCRLog
{
	// Open or Close SCRLog
	extern bool Open();
	extern void Close();

	// Kinda like a virtual function table (they are stdcall functions)
	extern void** ppRunningScript;
	extern void* AfterScripts;
	extern void* RegisterScript;
	extern void* RegisterCommand;
	extern void* RegisterCommandID;
	extern void* RegisterCallToCollectParameters;
	extern void* RegisterCallToStoreParameters;
	extern void* RegisterCallToCollectString;
	extern void* RegisterCollectedString;
	extern void* RegisterCollectionAtIndex;
	extern void* RegisterCompareFlagUpdate;
	extern void* RegisterGlobalVariable;
	extern void* RegisterLocalVariable;
	extern void* RegisterVariable;

	extern void* New_CRunningScript__UpdateCompareFlag;

	// Sets working datatype
	extern int Datatype;

	//
	extern ScriptVar* MissionLocals;
	extern ScriptVar* CollectiveArray;
	extern short*     ScriptsUpdated;
	extern char*      ScriptSpace;
	extern char*      ScriptSpaceEnd;
	extern char*      MissionSpace;
	extern char*      MissionSpaceEnd;

	//
	extern bool bHookAfterScripts;
	extern bool bHookOnlyRegisterScript;
	extern bool bHookStrncpy;
	extern bool bHookCollectParam;
	extern bool bHookCollectString;
	extern bool bHookStoreParam;
	extern bool bHookFindDatatype;
	extern bool bHookUpdateCompareFlag;
	extern bool bHookRegisterScript;
	extern bool bHookRegisterCommand;
	extern bool bHookCollectVarPointer;

}

