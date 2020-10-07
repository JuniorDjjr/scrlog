/*
 *	scrlog.cpp:
 *	     This file contains the higher level implementation of scrlog
 *
 *  Issues (entire SCRLOG project):
 *       Not thread safe
 *       Not lightweight
 *
 *  LICENSE:
 *		 (c) 2020 - lazyuselessman, Junior_Djjr
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
#define IS_ADDRESSES_CPP
#include <cstdio>					// for C Streams and sprintf
#include <cstring>					// for memset
#include <cctype>					// for isspace
#include <new>						// for std::bad_alloc
#include <string>					// for std::string, our buffer
#include "scrlog.h"
#include "GameInfo.h"
#include "Injector.h"
#include "CRunningScript.h"
#include "Events.h"
#include "Shellapi.h"
 
static GameInfo info;

void init(GameInfo& info)
{
	if(!SCRLog::Open())
		return;

	switch(info.GetGame())
	{
		case info.III:	III_Patch(info); break;
		case info.VC:	 VC_Patch(info); break;
		case info.SA:	 SA_Patch(info); break;
	}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		info.PluginName = "scrlog";
		info.DelayedDetect(init);
	}
	else if(fdwReason == DLL_PROCESS_DETACH)
	{
		SCRLog::Close();
	}
	return TRUE;
}

uint32_t pCheatString = 0;
uint32_t pScriptsProcessed = 0;
std::string message;
std::wstring messageWide;
uint32_t lastCommand = 0;

// Util functions
namespace SCRLog
{
	bool TestCheat(const char* cheat)
	{
		char *c = (char *)pCheatString;
		char buf[30];
		strcpy(buf, cheat);
		char *s = _strrev(buf);
		if (_strnicmp(s, c, strlen(s))) return false;
		c[0] = 0;
		return true;
	}

	// Get this DLL directory
	static size_t GetDLLDirectory(char* out_buf, size_t max)
	{
		// Reference:
		// http://stackoverflow.com/questions/6924195/get-dll-path-at-runtime

		char *path = out_buf, *p;
		HMODULE module;
		DWORD r;

		// Find module handle by a address (let's use this function pointer for the address)
		if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								(LPCSTR) &GetDLLDirectory, &module))
		{
			// Get the module path and remove the dll filename
			r = GetModuleFileNameA(module, path, max);
			if(r != 0 && r != max && (p = strrchr(path, '\\'))!=0)
			{
				p[1] = 0;
				return (p - path) + 1;
			}
		}

		return 0;
	}

	// Get path for file in dll directory ($GetDLLDirectory()$filename)
	static char* GetPathFor(const char* filename, char* out, size_t max)
	{
		size_t i = GetDLLDirectory(out, max);
		if(i == 0)
		{
			*out = 0;
			return out;
		}
		strcpy(out+i, filename);
		return out;
	}

	// Opens the file based on the DLL path
	static FILE* fopen(const char* filename, const char* mode)
	{
		//  We need this function to work properly with scripts\ subfolder with Silent's ASI Loader
		// and mss\ folder with miles loader.

		// Reference:
		// http://stackoverflow.com/questions/6924195/get-dll-path-at-runtime

		char path[2048];

		// If using Silent's ASI Loader and it was not delayed detect there is no need for this function
		// If using III or Vice City it is necessary because our ASI Loader (miles) run on splash screen
		//if(info.GetGame() == info.SA && !info.IsDelayed())
		//	return std::fopen(filename, mode);

		GetPathFor(filename, path, sizeof(path));
		return std::fopen(path, mode);
	}

	// WinAPI GetPrivateProfileX, Boolean implementation
	static bool GetPrivateProfileBoolA(const char* section, const char* key, bool defaultv, const char* filename)
	{
		char buf[8];
		if(!GetPrivateProfileStringA(section, key, 0, buf, 7, filename))
			return defaultv;
		else if(buf[1] == 0)
			return buf[0] != '0';
		else if(!_stricmp(buf, "TRUE"))
			return true;
		else
			return false;
	}
}



namespace SCRLog
{
	typedef std::string string_buffer;

	static const char szLogName[] = "scrlog.log";
	static const char szIniName[] = "scrlog.ini";
	static char ini[2048];


	enum {
		EXP_NONE,
		EXP_ASSIGN, EXP_ADD, EXP_SUB, EXP_MUL, EXP_DIV,
		EXP_EQ, EXP_LEQ, EXP_GEQ, EXP_LE, EXP_GE,
		EXP_TIMED_ADD, EXP_TIMED_SUB, EXP_CAST,
		EXP_MAX
	};
	static const char* ExpressionSeparator[EXP_MAX] =
	{
		" ", " =", " +=", " -=", " *=", " /=", " ==", " <=", " >=", " <", " >",
		" +=@ ", " -=@", " =#"
	};

	enum eFlushTime
	{
		FLUSH_NEVER,
		FLUSH_AUTOMATIC,
		FLUSH_ON_SCRIPT,
		FLUSH_ON_COMMAND,
		FLUSH_ON_COLLECT,
		FLUSH_ON_WRITE
	};
	static const char* aFlushTime[] =
	{
		"FLUSH_NEVER", "FLUSH_AUTOMATIC", "FLUSH_ON_SCRIPT", "FLUSH_ON_COMMAND",
		"FLUSH_ON_COLLECT", "FLUSH_ON_WRITE", 0
	};


	struct SScriptCommand
	{
		bool bValid;
		char cCommandName[64];
		char cArguments[32];
		int nExp;				// EXPression type

		SScriptCommand()
		{
			nExp = EXP_NONE;
			bValid = false;
			cCommandName[0] = 0;
			memset(cArguments, 0, sizeof(cArguments));
		}
	};


	static void ReadINI(void (*callback)(char* key, char* value));

	// fake vftable, set up by the hookers (scmlog_<GAME>.cpp)
	void** ppRunningScript;
	void* New_CRunningScript__UpdateCompareFlag;
	void* CRunningScript__UpdateCompareFlag;
	void* AfterScripts;
	void* RegisterScript;
	void* RegisterCommand;
	void* RegisterCommandOut;
	void* RegisterCallToCollectParameters;
	void* RegisterCallToStoreParameters;
	void* RegisterCallToCollectString;
	void* RegisterCollectedString;
	void* RegisterCollectionAtIndex;
	void* RegisterCompareFlagUpdate;
	void* RegisterLocalVariable;
	void* RegisterGlobalVariable;
	void* RegisterVariable;
	// Pointers to game executable data, also set up by the hookers
	ScriptVar* MissionLocals;
	ScriptVar* CollectiveArray;
	short*     ScriptsUpdated;
	char*      ScriptSpace;
	char*      ScriptSpaceEnd;
	char*      MissionSpace;
	char*      MissionSpaceEnd;

	// SCRLog
	FILE*    log;
	char*    pParam;					// Pointer to SScriptCommand::cArguments[_CURRENT_ARGUMENT_]
	bool     bParam;					// true if can use pParam to count params
	bool     bOpened = false;			// true if SCRLOG is open
	char*    StringBuffer;				// Buffer to use with sprintf, etc
	char*    lastLineBuffer;            // The last command line to use with crash window
	string_buffer LogBuffer;			// File buffer, let's use our own
	int32_t  Command;					// Current command being executed
	int32_t  Exp;						// Current expression being executed
	bool     UsedExpressionThisCommand;	// true if the current command already took a expresion string (==, *=, etc)
	bool     ThisCommandResult;			// The result (UpdateCompareFlag) of the current command
	int32_t  Datatype;					// Current datatype, set by the game hookers (scmlog_<GAME>.cpp)
	uint32_t nCommandsOnLog;			// Number of commands currently on the log file
	uint32_t nScriptsOnLog;			    // Number of scripts currently on the log file
	int32_t  nHighestCommand = 0x0B20;	// Highest command used.
	bool     bEnabled = true;           // Easy way to toggle. Can be toggled like a cheat in-game.
	SScriptCommand* Commands;			// Pointer to commands data... Commands[nHighestCommand+1]


	// Configuration - set up on the INI, see the ini file for descriptions
	uint32_t nMaxScriptsOnLog   = 600;
	uint32_t nMaxOpcodesOnLog   = 6000;
	uint32_t  nStreamBufferSize = 2048;
	int  FlushTime              = FLUSH_NEVER;
	bool bShowCrashWindow       = true;
	bool bClearLogEachFrame     = true;
	bool bUseBreakpointOpcode   = false;
	bool bClassicLog			= false;
	bool bUseParamInfo          = true;
	bool bUseSimpleFloat        = true;
	bool bUseExpressions		= true;
	bool bHookStrncpy           = false;	// Dont work as expected
	bool bHookAfterScripts      = true;
	bool bHookOnlyRegisterScript= true;
	bool bHookCollectString     = true;
	bool bHookCollectVarPointer = true;

	bool bHookRegisterScript = true; 
	bool bHookRegisterCommand = true;
	bool bHookCollectParam = true; // crash 4512bc // same // 1 2 451265 // 1 3 80808080
	bool bHookFindDatatype = true; // crash 7e0fc0 without updatecomapre flag // 4511a5 // 2 3 450fcc
	bool bHookStoreParam = true; // crash 450fcc without updatecomapre flag // 80808080 // 1 2 3 450fcc // 450fce
	bool bHookUpdateCompareFlag = true;

	bool bProcessingScriptsNow = false;
	char scname[8];

	// Setups commands in range a-b to expression exp
	inline void SetupCommandExpression(int a, int b, int exp)
	{
		for(int i = a; i <= b; ++i)
			Commands[i].nExp = exp;
	}

	// Setups all expression commands to their desired values
	static void SetupCommandsExpression()
	{
		if(nHighestCommand >= 0x0093)
		{
			SetupCommandExpression(0x0004, 0x0007, EXP_ASSIGN);
			SetupCommandExpression(0x0008, 0x000B, EXP_ADD);
			SetupCommandExpression(0x000C, 0x000F, EXP_SUB);
			SetupCommandExpression(0x0010, 0x0013, EXP_MUL);
			SetupCommandExpression(0x0014, 0x0017, EXP_DIV);
			SetupCommandExpression(0x0018, 0x0027, EXP_GE);
			SetupCommandExpression(0x0028, 0x0037, EXP_GEQ);
			SetupCommandExpression(0x0038, 0x003C, EXP_EQ);
			SetupCommandExpression(0x0042, 0x0046, EXP_EQ);
			SetupCommandExpression(0x0058, 0x005F, EXP_ADD);
			SetupCommandExpression(0x0060, 0x0067, EXP_SUB);
			SetupCommandExpression(0x0068, 0x006F, EXP_MUL);
			SetupCommandExpression(0x0070, 0x0077, EXP_DIV);
			SetupCommandExpression(0x0078, 0x007D, EXP_TIMED_ADD);
			SetupCommandExpression(0x007E, 0x0083, EXP_TIMED_SUB);
			SetupCommandExpression(0x0084, 0x008B, EXP_ASSIGN);
			SetupCommandExpression(0x008C, 0x0093, EXP_CAST);
		}
		if(nHighestCommand >= 0x04B7 && info.GetGame() != info.III)
		{
			Commands[0x04A3].nExp = EXP_EQ;
			Commands[0x04A4].nExp = EXP_EQ;
			Commands[0x04AE].nExp = EXP_ASSIGN;
			Commands[0x04AF].nExp = EXP_ASSIGN;
			SetupCommandExpression(0x04B0, 0x04B3, EXP_GE);
			SetupCommandExpression(0x04B4, 0x04B7, EXP_GEQ);
		}
		if(info.GetGame()== info.SA)
		{
			if(nHighestCommand >= 0x05AE)
			{
				Commands[0x05A9].nExp = EXP_ASSIGN;
				Commands[0x05AA].nExp = EXP_ASSIGN;
				Commands[0x05AD].nExp = EXP_EQ;
				Commands[0x05AE].nExp = EXP_EQ;
			}
			if(nHighestCommand >= 0x06D2)
			{
				Commands[0x06D1].nExp = EXP_ASSIGN;
				Commands[0x06D2].nExp = EXP_ASSIGN;
			}
			if(nHighestCommand >= 0x07D7)
			{
				Commands[0x07D6].nExp = EXP_EQ;
				Commands[0x07D7].nExp = EXP_EQ;
			}
		}
	}

	// Exp is setup with the command value, and the separator string is returned
	inline const char* GetExpressionSeparator()
	{
		if(Command <= nHighestCommand && !UsedExpressionThisCommand)
		{
			UsedExpressionThisCommand = true;
			return ExpressionSeparator[ Exp = Commands[Command].nExp ];
		}
		return ExpressionSeparator[ Exp = EXP_NONE ];
	}


	// Flush the stream to the disk
	inline void Flush(bool bForce = false)
	{
		if(!bForce && FlushTime == FLUSH_NEVER)
			return;

		if(LogBuffer.length() > 0)
		{
			fwrite(LogBuffer.data(), sizeof(char), LogBuffer.length(), log);
			LogBuffer.clear();
		}
		fflush(log);
	}


	// Logs string with size bytes
	inline void Log(const char* string, size_t size)
	{
		if(FlushTime != FLUSH_ON_WRITE)
		{
			LogBuffer.append(string, string+size);
			if(FlushTime != FLUSH_NEVER && LogBuffer.length() >= nStreamBufferSize)
				Flush();
		}
		else
		{
			fwrite(string, sizeof(char), size, log);
			Flush();
		}
	}

	// Log null terminated string
	inline void Log(const char* string)
	{
		Log(string, strlen(string));
	}


	// Clear the log file
	inline void Reset()
	{
		nCommandsOnLog = 0;
		nScriptsOnLog = 0;
		if(FlushTime != FLUSH_NEVER)
		{
			Flush();
			fclose(log);
			log = fopen(szLogName, "wb");
			//if(log == 0)
				  // error, what to do?
				  // throw and error and terminate application
		}
		else
		{
			LogBuffer.clear();
		}
	}

	// Setups the pParam pointer
	inline char* GetCommandArgs()
	{
		int cmd = Command;
		if(bParam && cmd <= nHighestCommand)
			return (pParam = Commands[cmd].cArguments);
		return 0;
	}

	// Get current command name
	inline char* GetCommandName()
	{
		static char cUnkCommand[16];
		int cmd = Command, exp;
		SScriptCommand* s;

		if(cmd <= nHighestCommand)
		{
			s = &Commands[cmd];
			exp = s->nExp;
			if(exp != EXP_NONE)
				return "";
			else if(s->bValid)
				return s->cCommandName;
		}

		sprintf(cUnkCommand, "COMMAND_%.4X", cmd);
		return cUnkCommand;
	}

	// Get argument type for the current parameter
	inline int GetArgumentType()
	{
		int cmd = Command;
		SScriptCommand* s;

		if(bParam && cmd <= nHighestCommand)
		{
			s = &Commands[cmd];
			if(s->bValid)
			{
				char x = *pParam++;
				if(x != 0 && x != 'a')
					return x;
			}
		}

		return Datatype;
	}

	// Helper function for RegisterParam()
	inline char* float2str(float f)
	{
		static char fbuf[64];

		if(bUseSimpleFloat)
		{
			sprintf(fbuf, "%g", f);
			for(char* p = fbuf; ; ++p)
			{
				if(*p == '.')
				{ break; }
				else if(*p == 0)
				{ p[0] = '.', p[1] = '0'; p[2] = 0; break; }
			}
		}
		else
		{
			sprintf(fbuf, "%f", f);
		}

		return fbuf;
	}

	// Register param to Buffer and returns num chars taken
	int RegisterParam(char* Buffer, ScriptVar* data, int n)
	{
		switch(GetArgumentType())
		{
			case 1:	case 2: case 3: case 4: case 5: // [III|VC|SA] Int32, Global, Local, Int8, Int16
			case 'i': case 'm': case 'l':
				return sprintf(Buffer, "    param %d = %d\r\n", n, data->nParam);

			case 'h':
				return sprintf(Buffer, "    param %d = 0x%X\r\n", n, data->nParam);

			case 6:	case 'f':	// Float
				return sprintf(Buffer, "    param %d = %s\r\n", n, float2str(data->fParam));

			default:
				return sprintf(Buffer, "    param %d = [UNKNOWN]\r\n", n);
		}
	}

	// Same as above, but for modern logging
	int RegisterParamValue(char* Buffer, ScriptVar* data)
	{
		switch(GetArgumentType())
		{
			case 1:	case 2: case 3: case 4: case 5: // [III|VC|SA] Int32, Global, Local, Int8, Int16
			case 'i': case 'm': case 'l':
				return sprintf(Buffer, " %d", data->nParam);

			case 'h':
				return sprintf(Buffer, " 0x%X", data->nParam);


			case 6:	case 'f':	// Float
				return sprintf(Buffer, " %s", float2str(data->fParam));

			default:
				return sprintf(Buffer, " [UNKNOWN]");
		}
	}


	// Base for LogFactory, this have functions that are specific for each game!
	// Base for III and Vice City:
	template<class TScript>
	class LogFactoryBase
	{
		public:
			typedef TScript CRunningScript;
			static CRunningScript* pRunningScript;

			// Get vars array and num vars for script
			static void GetScriptVars(CRunningScript* script, ScriptVar*& aVars, int& nVars)
			{
				aVars = script->tls;
				nVars = 18;
			}

			// Gets the instruction pointer for script
			static char* GetScriptIP(CRunningScript* script, unsigned int& humanIP)
			{
				char* ip = &ScriptSpace[(size_t)script->ip];
				humanIP = (unsigned int) script->ip;

				// if ip ain't in main block, then I guess the script has local offsets...
				if(ip < ScriptSpace || ip >= ScriptSpaceEnd)
				{
					// if ip ain't in mission block, it is a CLEO script (XXX: Anyone wanting to reverse III\VC CLEO.asi?)
					if(ip >= MissionSpace || ip < MissionSpaceEnd)
						humanIP = (unsigned int)(ip - MissionSpace);
				}

				return ip;
			}

			// Gets the variable type and value (offset if global, index if local)
			// Returns the var type (2 if global, 3 if local, -1 if pointer)
			static int __stdcall GetVariableOffset(CRunningScript* script, ScriptVar* var, int& out)
			{
				// Check entire script struct bounds, 'cause someone may want to replace something there
				// in favour of a variable
				if(var >= (ScriptVar*)(script) && var < (ScriptVar*)(script + sizeof(*script)) )
					return (out = var - script->tls),    3; // << return 3

				// Not local, try globals
				char* pv = (char*)var;
				if(pv >= ScriptSpace && pv < ScriptSpaceEnd)
					return (out = pv - ScriptSpace),  2; // << return 2

				// I have no idea what is this...
				out = (int)(var);
				return -1;
			}

			//
	};

	// Base for San Andreas:
	template<>
	class LogFactoryBase<CRunningScript_SA>
	{
		public:
			typedef CRunningScript_SA CRunningScript;
			static CRunningScript* pRunningScript;

			// Get vars array and num vars for script
			static void __stdcall GetScriptVars(CRunningScript* script, ScriptVar*& aVars, int& nVars)
			{
				if(script->missionFlag)
				{
					aVars = MissionLocals;
					nVars = 1024;
				}
				else
				{
					aVars = script->tls;
					nVars = 34;
				}
			}

			// Gets the instruction pointer for script
			static char* __stdcall GetScriptIP(CRunningScript* script, unsigned int& humanIP)
			{
				char *ip = script->ip, *base = script->base? script->base : ScriptSpace;
				humanIP = ip - base;
				return ip;
			}

			// Gets the variable type and value (offset if global, index if local)
			// Returns the var type (2 if global, 3 if local, -1 if pointer)
			static int __stdcall GetVariableOffset(CRunningScript* script, ScriptVar* var, int& out)
			{
				// Check locals first, since they're in a place higher in memory
				// The above statement is only true in non-mission scripts, but who cares for 1 script in a group of 100.
				if(!script->missionFlag)
				{
					// Check entire script struct bounds, 'cause someone may want to replace something there
					// in favour of a variable
					if(var >= (ScriptVar*)(script) && var < (ScriptVar*)(script + sizeof(*script)) )
						return (out = var - script->tls),    3; // << return 3
				}
				else
				{
					if(var >= MissionLocals && var < &MissionLocals[1024])
						return (out =var - MissionLocals),   3; // << return 3
				}

				// Not local, try globals
				char* pv = (char*) var;
				if(pv >= ScriptSpace && pv < ScriptSpaceEnd)
					return (out = pv - ScriptSpace),  2; // << return 2

				// I have no idea what is this... thread memory? super vars?
				out = (int)(var);
				return -1;
			}
	};



	// The factory itself, the factory will produce our functions for each game without any effort.
	// Write once, get 3 outputs, thanks generic programming.
	template<class TScript>
	class LogFactory : public LogFactoryBase<TScript>
	{
		public:
			// We need to replace the UpdateCompareFlag method =|
			static void __stdcall CRunningScript__UpdateCompareFlag(CRunningScript* script, char v)
			{
				char b = script->notFlag? v == 0 : v != 0;
				ThisCommandResult = b != 0;
				int logic = script->logicalOp;

				if(logic == 0)
				{
					script->compareFlag = b;
				}
				else
				{
					bool e;
					if(logic >= 1 && logic <= 8)	    // &&
						script->compareFlag &= b, e = logic == 1;
					else if(logic >= 21 && logic <= 28) // ||
						script->compareFlag |= b, e = logic == 21;

					if(e) script->logicalOp = 0;
					else  --script->logicalOp;
				}

				if(script == pRunningScript)
				{
					typedef void (__stdcall *fn_t)(void);
					fn_t fn = (fn_t) RegisterCompareFlagUpdate;
					fn();
				}
			}

			// Script finalized
			static void __stdcall AfterScripts(CRunningScript* script)
			{
				if (bEnabled) Log("\r\nFinished processing.");
				if (pCheatString) {
					// I don't want to make another hook just to add TestCheat, so, just run it for first script.
					// If pScriptsProcessed isn't available, fuck off this optimization for now.
					if (pScriptsProcessed == 0x0 || *(uint32_t*)pScriptsProcessed == 1) {
						if (TestCheat("SCRL")) {
							bEnabled = !bEnabled;
							WritePrivateProfileStringA("CONFIG", "ENABLED", (bEnabled) ? "TRUE" : "FALSE", ini);

							if (bEnabled) message = "SCRLog is ENABLED!"; else message = "SCRLog is DISABLED!";

							Log("\r\n\r\n");
							Log(&message[0]);
							Log("\r\n");

							wchar_t* wMessage = nullptr;
							if (info.GetGame() != info.SA) {
								int wchars_num = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, NULL, 0);
								wMessage = new wchar_t[wchars_num];
								MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wMessage, wchars_num);
							}

							switch (info.GetGame())
							{
							case info.III: IIICHudSetHelpMessage(wMessage, 1); break;
							case info.VC:  VCCHudSetHelpMessage(wMessage, 1, 0); break;
							case info.SA:  SACHudSetHelpMessage(message.c_str(), 1, 0, 0); break;
							}

							if (wMessage) delete[] wMessage;
						}
					}
				}
				bProcessingScriptsNow = false;
				scname[0] = 0;
				lastLineBuffer[0] = 0;
			}

			// Start logging script
			static void __stdcall RegisterScript(CRunningScript* script)
			{
				if (!bEnabled) return;
				int i, nVars;
				ScriptVar* aVars;
				char* buffer = StringBuffer;

				// Set running script pointer
				pRunningScript = script;

				++nScriptsOnLog;
				bProcessingScriptsNow = true;

				// Reset the log file if I must clear the file on each frame
				if((bClearLogEachFrame && *ScriptsUpdated == 1) || nScriptsOnLog > nMaxScriptsOnLog)
					Reset();

				// Get the pointer to the script variables (and num vars)
				GetScriptVars(script, aVars, nVars);

				// Get script name...
				strncpy(scname, pRunningScript->scriptName, 8);
				scname[7] = 0;		// Safe for buggy scripts that use names with >=8 chars

				// Create log string...
				{
					// Log script name
					buffer += sprintf(buffer,
						"\r\n\r\n********************************************\r\n"
						" script %s\r\n"
						" Local variables dump:",
						scname);

					// Log local variables
					for(i = 0; i < nVars; ++i)
					{
						if((i % 16) == 0) strcpy(buffer, "\r\n"), buffer += 2;
						buffer += sprintf(buffer, " %d", aVars[i]);
					}

					// ha!
					if (bHookOnlyRegisterScript) {
						buffer += sprintf(buffer, "\r\n********************************************\r\n");
						buffer += sprintf(buffer, "HOOK_ONLY_REGISTER_SCRIPT=TRUE set in the ini.\r\nThis log isn't useful for asking help!\r\nDisable it or also send other crash log.");
					}
					buffer += sprintf(buffer, "\r\n********************************************\r\n");
				}

				if(FlushTime == FLUSH_ON_SCRIPT)
					Flush();

				// Send it to the stream
				Log(StringBuffer, buffer - StringBuffer);
			}
	};

	template<class TScript>
	class LogClassic : public LogFactory<TScript>
	{
		public:
			static void __stdcall RegisterCommand()
			{
				if (!bEnabled) return;
				char* ip;
				unsigned int humanIP;
				unsigned short command;
				char* buffer = StringBuffer;
				CRunningScript* script = pRunningScript;

				++nCommandsOnLog;

				// If too many commands, clear the log
				if(nCommandsOnLog > nMaxOpcodesOnLog)
					Reset();

				// Get ip and command
				ip = GetScriptIP(pRunningScript, humanIP);
				command = *(uint16_t*)ip;
				lastCommand = command;

				//
				Command = command & 0x7FFF;
				UsedExpressionThisCommand = false;
				GetCommandArgs();

				Log(buffer, sprintf(buffer, "\r\n%.8u&%d: %.4X\r\n", humanIP, script->compareFlag, command));

				if(FlushTime == FLUSH_ON_COMMAND)
					Flush();

				if (lastCommand == 0xED0 /* RETURN_SCRIPT_EVENT from CLEO+ */) bProcessingScriptsNow = false;
			}

			static void __stdcall RegisterCompareFlagUpdate()
			{
				if (!bEnabled) return;
				Log(StringBuffer, sprintf(StringBuffer, "  update compare flag: %s\r\n",
					ThisCommandResult? "true" : "false"));
			}

			static void __stdcall RegisterCallToCollectParameters(unsigned short n)
			{
				if (!bEnabled) return;
				Log(StringBuffer, sprintf(StringBuffer, "  collect params: %d\r\n", n));
			}

			static void __stdcall RegisterCallToCollectString()
			{
				if (!bEnabled) return;
				Log("  collect string: ");
			}

			static void __stdcall RegisterCallToStoreParameters(int n)
			{
				if (!bEnabled) return;
				if (!bProcessingScriptsNow)
					return;

				char* buffer = StringBuffer;
				buffer += sprintf(buffer, "  store params: %d\r\n", n);
				for(int i = 0; i < n; ++i)
					buffer += RegisterParam(buffer, &CollectiveArray[i], i+1);

				Log(StringBuffer, buffer - StringBuffer);

				if(FlushTime == FLUSH_ON_COLLECT)
					Flush();
			}

			static void __stdcall RegisterCollectionAtIndex(int n)
			{
				if (!bEnabled) return;
				Log(StringBuffer, RegisterParam(StringBuffer, &CollectiveArray[n-1], n));

				if(FlushTime == FLUSH_ON_COLLECT)
					Flush();
			}

			// This is just used for temp fix on RegisterCollectedString
			// Maybe strnlen_s instead?
			static size_t safe_strlen(const char *str, size_t max_len)
			{
				const char * end = (const char *)memchr(str, '\0', max_len);
				if (end == NULL)
					return 0;
				else
					return end - str;
			}

			static void __stdcall RegisterCollectedString(const char* str, size_t max)
			{
				// Let's make it safe, we can guarante str is null terminated, so let's transfer things to
				// our buffer just in case.

				if (!bEnabled) return;

				if(!pRunningScript)		// III\VC hack for strncpy
					return;

				if(!bProcessingScriptsNow)
					return;

				char buf[512];
				const char* p = 0;
				
				// Register that we are going to another param, and ignore output
				ScriptVar dummy;
				Datatype = -1;
				RegisterParamValue(buf, &dummy);

				// TODO: This is just a temp fix for stack bug on new CLEO version.
				if (max >= 256)
				{
					max = safe_strlen(str, 256);
				}

				// making sure it is null terminated without any peformance effort...
				if (max > 0)
				{
					if (str[max - 1] == 0)
					{
						p = str;
					}
					else if (max < sizeof(buf))
					{
						strncpy(buf, str, max);
						buf[max] = 0;
						p = buf;
					}
				}
				
				Log(StringBuffer, sprintf(StringBuffer, "%s\r\n", p? p : "[DAMAGED_STRING]"));

				if(FlushTime == FLUSH_ON_COLLECT)
					Flush();
			}


			static void __stdcall RegisterPointer(void* ptr)
			{
				if (!bEnabled) return;
				Log(StringBuffer, sprintf(StringBuffer, "  collect pointer %p\r\n", ptr));
			}

			static void __stdcall RegisterGlobalVariable(int offset)
			{
				if (!bEnabled) return;
				char buffer[64];
				RegisterParamValue(buffer, (ScriptVar*)(&ScriptSpace[offset]));
				Log(StringBuffer, sprintf(StringBuffer, "  collect global var %d: %s\r\n", offset / 4, buffer));
			}

			static void __stdcall RegisterLocalVariable(int index)
			{
				if (!bEnabled) return;
				char buffer[64]; int n; ScriptVar* var;
				GetScriptVars(pRunningScript, var, n); 
				Log(StringBuffer, sprintf(StringBuffer, "  collect local var %d: %s\r\n",  index, buffer));
			}

			static void __stdcall RegisterVariable(ScriptVar* ptr)
			{
				if (!bEnabled) return;
				int value;
				switch( GetVariableOffset(pRunningScript, ptr, value) )
				{
					case -1: RegisterPointer(ptr);            break;
					case 2:  RegisterGlobalVariable(value);   break;
					case 3:  RegisterLocalVariable(value);    break;
				}
			}
	};

	template<class TScript>
	class LogModern : public LogFactory<TScript>
	{
		public:

			static void __stdcall RegisterCommand()
			{
				if (!bEnabled) return;
				static const char notArray1[2][6] = { " ", " NOT " };
				static const char notArray2[2][6] = { "", " NOT" };

				char* ip, *cmdName;
				bool notFlag;
				unsigned int humanIP;
				unsigned short command;
				char* buffer = StringBuffer;
				CRunningScript* script = pRunningScript;

				++nCommandsOnLog;

				// If too many commands, clear the log
				if(nCommandsOnLog > nMaxOpcodesOnLog)
					Reset();

				// Get ip and command
				ip = GetScriptIP(pRunningScript, humanIP);
				command = *(uint16_t*)ip;
				lastCommand = command;

				//
				Command = command & 0x7FFF;
				UsedExpressionThisCommand = false;
				notFlag = (command & 0x8000)!=0;
				GetCommandArgs();

				cmdName = GetCommandName();
				Log(buffer, sprintf(buffer, "\r\n%.8u&%d: [%.4X]%s%s", humanIP, script->compareFlag, command, *cmdName ? notArray1[notFlag] : notArray2[notFlag], cmdName));

				strcpy(lastLineBuffer, buffer);

				if(FlushTime == FLUSH_ON_COMMAND)
					Flush();
			}

			static void __stdcall RegisterCompareFlagUpdate()
			{
				if (!bEnabled) return;
				// Normally UpdateCompareFlag is the last thing called in a command handler
				// If not, our log will be fucked up
				Log(StringBuffer, sprintf(StringBuffer, "    // %s",
					ThisCommandResult? "TRUE" : "FALSE"));
			}

			static void __stdcall RegisterCallToCollectParameters(unsigned short n)
			{
			}

			static void __stdcall RegisterCallToCollectString()
			{
			}

			static void __stdcall RegisterCallToStoreParameters(int n)
			{
				if (!bEnabled) return;
				if (!bProcessingScriptsNow)
					return;

				char* buffer = StringBuffer;

				buffer[0] = ' ', buffer[1] = '-', buffer[2] = '>', buffer[3] = 0;
				buffer += 3;

				for(int i = 0; i < n; ++i)
					buffer += RegisterParamValue(buffer, &CollectiveArray[i]);

				Log(StringBuffer, buffer - StringBuffer);

				if(FlushTime == FLUSH_ON_COLLECT)
					Flush();
			}

			static void __stdcall RegisterCollectionAtIndex(int n)
			{
				if (!bEnabled) return;
				if (!bProcessingScriptsNow)
					return;

				Log(StringBuffer, RegisterParamValue(StringBuffer, &CollectiveArray[n-1]));

				if(FlushTime == FLUSH_ON_COLLECT)
					Flush();
			}

			// This is just used for temp fix on RegisterCollectedString
			static size_t safe_strlen(const char *str, size_t max_len)
			{
				const char * end = (const char *)memchr(str, '\0', max_len);
				if (end == NULL)
					return 0;
				else
					return end - str;
			}

			static void __stdcall RegisterCollectedString(const char* str, size_t max)
			{
				// Let's make it safe, we can guarante str is null terminated, so let's transfer things to
				// our buffer just in case.

				if (!bEnabled) return;

				if(!pRunningScript) // III\VC hack for strncpy
					return;

				if (!bProcessingScriptsNow)
					return;

				char buf[512];
				const char* p = 0;

				// Register that we are going to another param, and ignore output
				ScriptVar dummy;
				Datatype = -1;
				RegisterParamValue(buf, &dummy);

				// TODO: This is just a temp fix for stack bug on new CLEO version.
				if (max >= 256)
				{
					max = safe_strlen(str, 256);
				}

				// making sure it is null terminated without any peformance effort...
				if (max > 0)
				{
					if (str[max - 1] == 0)
					{
						p = str;
					}
					else if (max < sizeof(buf))
					{
						strncpy(buf, str, max);
						buf[max] = 0;
						p = buf;
					}
				}
				
				Log(StringBuffer, sprintf(StringBuffer, " \"%s\"", p? p : "[DAMAGED_STRING]"));

				if(FlushTime == FLUSH_ON_COLLECT)
					Flush();
			}

			static void __stdcall RegisterPointer(void* ptr)
			{
				if (!bEnabled) return;
				// We are not going to output the value of pointer, this would be dangerous
				// Register param and ignore the output
				ScriptVar x;
				RegisterParamValue(StringBuffer, &x);
				// Log
				Log(StringBuffer, sprintf(StringBuffer, " p%p(?)%s", ptr, GetExpressionSeparator()));
			}

			// HACK HACK HACK: Here (RegisterGlobalVariable) and there (RegisterLocalVariable)
			// we modified the returned buffer from RegisterParamValue, replacing the initial space
			// with a paratensis... If you modify RegisterParamValue we will need to check the funcs

			static void __stdcall RegisterGlobalVariable(int offset)
			{
				if (!bEnabled) return;
				// it is safe to output the var value 'cause the offset detection was based on checking
				// the ScriptSpace bounds
				char buffer[64];
				RegisterParamValue(buffer, (ScriptVar*)(&ScriptSpace[offset]));
				buffer[0] = '(';
				Log(StringBuffer, sprintf(StringBuffer, " g%d%s)%s", offset / 4, buffer, GetExpressionSeparator()));
			}

			static void __stdcall RegisterLocalVariable(int index)
			{
				if (!bEnabled) return;
				// it is safe to output the var value 'cause the index detection was based on checking
				// the script->tls\MissionLocals bounds
				char buffer[64]; int n; ScriptVar* var;
				GetScriptVars(pRunningScript, var, n); 
				RegisterParamValue(buffer, &var[index]);
				buffer[0] = '(';
				Log(StringBuffer, sprintf(StringBuffer, " l%d%s)%s", index, buffer, GetExpressionSeparator()));
			}

			static void __stdcall RegisterVariable(ScriptVar* ptr)
			{
				if (!bEnabled) return;
				int value;
				switch( GetVariableOffset(pRunningScript, ptr, value) )
				{
					case -1: RegisterPointer(ptr);            break;
					case 2:  RegisterGlobalVariable(value);   break;
					case 3:  RegisterLocalVariable(value);    break;
				}
			}

	};



	// Okay, I really didn't want to have this function, I wanted to directly hook to
	// factory::CRunningScript__UpdateCompareFlag, but we need to save ecx, sometimes
	// the compiler (with gta_sa.exe) may optimize calls, and know that ecx was not modified in
	// that specific function...
	static void __declspec(naked) HOOK_CRunningScript__UpdateCompareFlag()
	{
		_asm
		{
			push ecx
			push dword ptr [esp+8]
			push ecx
			call CRunningScript__UpdateCompareFlag
			pop ecx
			retn 4
		}
	}

	// Set function pointers for TScript type and start logging
	CRunningScript_III* LogFactoryBase<CRunningScript_III>::pRunningScript;
	CRunningScript_VC* LogFactoryBase<CRunningScript_VC>::pRunningScript;
	CRunningScript_SA* LogFactoryBase<CRunningScript_SA>::pRunningScript;
	template<class TLog> inline void OpenFactory()
	{
		typedef TLog factory;

		// Setup our "vtable"
		bProcessingScriptsNow = false;
		factory::pRunningScript = 0;
		ppRunningScript						=(void**)&factory::pRunningScript;
		AfterScripts                        = (void*) factory::AfterScripts;
		RegisterScript						= (void*) factory::RegisterScript;
		RegisterCommand						= (void*) factory::RegisterCommand;
		RegisterCallToCollectParameters		= (void*) factory::RegisterCallToCollectParameters;
		RegisterCallToStoreParameters		= (void*) factory::RegisterCallToStoreParameters;
		RegisterCallToCollectString			= (void*) factory::RegisterCallToCollectString;
		RegisterCollectedString				= (void*) factory::RegisterCollectedString;
		RegisterCollectionAtIndex			= (void*) factory::RegisterCollectionAtIndex;
		RegisterCompareFlagUpdate			= (void*) factory::RegisterCompareFlagUpdate;
		RegisterVariable					= (void*) factory::RegisterVariable;
		RegisterLocalVariable				= (void*) factory::RegisterLocalVariable;
		RegisterGlobalVariable				= (void*) factory::RegisterGlobalVariable;

		// More function pointers, not really part of our "vtable"
		New_CRunningScript__UpdateCompareFlag = (void*) HOOK_CRunningScript__UpdateCompareFlag;
		CRunningScript__UpdateCompareFlag     = (void*) factory::CRunningScript__UpdateCompareFlag;
		
		// Setup vars
		nCommandsOnLog = 0;
		nScriptsOnLog = 0;
		Datatype = 1;	// Default Datatype to Int32
		bParam = bUseParamInfo && bHookCollectParam && bHookCollectString && bHookStoreParam;

		Log("************ SCRLOG *************\r\n> Logging started\r\n*********************************\r\n");
	}

	// Finishes logging
	inline void CloseFactory()
	{
		SYSTEMTIME time;
		GetLocalTime(&time);

		int len = sprintf(StringBuffer,
			"\r\n\r\n*********************************\r\n"
			"> Logging finished: %.2d:%.2d:%.2d\r\n"				// XXX
			"  Powered by SCRLog (by LINK/2012)\r\n"				// Okay, if any one contrib. to this project
			"  GTA:VC JP support by lazyuselessman\r\n"				// sorry i can't :) \/
			"  Improvements by Junior_Djjr\r\n"				        // me too \/
			"*********************************\r\n",				// remove the credits from this string 
			time.wHour, time.wMinute, time.wSecond);

		Log(StringBuffer, len);
	}


	// Create a string buffer and open a factory for the game TScript
	inline void BuildFactory()
	{
		typedef LogClassic<CRunningScript_III> LogClassicIII;
		typedef LogClassic<CRunningScript_VC>  LogClassicVC;
		typedef LogClassic<CRunningScript_SA>  LogClassicSA;
		typedef LogModern<CRunningScript_III>  LogModernIII;
		typedef LogModern<CRunningScript_VC>   LogModernVC;
		typedef LogModern<CRunningScript_SA>   LogModernSA;


		// Open a factory for the running game
		if(bClassicLog)
		{
			switch(info.GetGame())
			{
				case info.III: OpenFactory<LogClassicIII>(); break;
				case info.VC:  OpenFactory<LogClassicVC> (); break;
				case info.SA:  OpenFactory<LogClassicSA> (); break;
			}
		}
		else
		{
			switch(info.GetGame())
			{
				case info.III: OpenFactory<LogModernIII>(); break;
				case info.VC:  OpenFactory<LogModernVC> (); break;
				case info.SA:  OpenFactory<LogModernSA> (); break;
			}
		}
	}

	// Close the factory and frees our string buffer
	inline void DropBombAtFactory()
	{
		// CreatePlaneAt(0x6C8E20).BombIt(0xB74498);
		CloseFactory();
	}


	// INI reader callback
	void CommandReaderCallback(char* key, char* value)
	{
		char* comma;
		unsigned int command;

		// if could not get command or command is too high, ignore this pair
		if(sscanf(key, "%X", &command) < 1 || command > (unsigned int)(nHighestCommand))
			return;

		SScriptCommand& pCommand = Commands[command];

		if((comma = strchr(value, ',')) == 0)
			return;
		if((comma - value) >= sizeof(pCommand.cArguments)) // check strlen(CommandName)
			return;

		*comma++ = 0;			// separate tokens
		command &= 0x7FFF;		// remove not flag

		// If command was already taken, clear the argument list before continuing.
		if(pCommand.bValid)
		{
			memset(pCommand.cArguments, 0, sizeof(pCommand.cArguments));
		}

		// Setup command index
		pCommand.bValid = true;
		strcpy(pCommand.cCommandName, value);
		if(!(comma[0] == '*' && comma[1] == 0) && comma[0] != 0)
			strcpy(pCommand.cArguments, comma);
	}

	// Initialize SCRLog
	bool Open()
	{
		char buf[32];

		// Open file... Use binary mode, it is faster :)
		if( !(log = fopen(szLogName, "wb")) )
			return false;

		//
		GetPathFor(szIniName, ini, sizeof(ini));

		// Read basic configurations
		bShowCrashWindow     = GetPrivateProfileBoolA("CONFIG", "SHOW_CRASH_WINDOW", bShowCrashWindow, ini);
		bClassicLog          = GetPrivateProfileBoolA("CONFIG", "CLASSIC_LOG", bClassicLog, ini);
		bUseParamInfo        = GetPrivateProfileBoolA("CONFIG", "USE_PARAM_INFO", bUseParamInfo, ini);
		bUseSimpleFloat      = GetPrivateProfileBoolA("CONFIG", "USE_SIMPLE_FLOAT", bUseSimpleFloat, ini);
		bUseExpressions      = GetPrivateProfileBoolA("CONFIG", "USE_EXPRESSIONS", bUseExpressions, ini);    
		nStreamBufferSize    = GetPrivateProfileIntA ("CONFIG", "BUFFER_SIZE", nStreamBufferSize, ini);
		nMaxOpcodesOnLog     = GetPrivateProfileIntA ("CONFIG", "MAX_OPCODES", nMaxOpcodesOnLog, ini);
		nMaxScriptsOnLog     = GetPrivateProfileIntA ("CONFIG", "MAX_SCRIPTS", nMaxScriptsOnLog, ini);
		bClearLogEachFrame   = GetPrivateProfileBoolA("CONFIG", "CLEAR_LOG_EACH_FRAME", bClearLogEachFrame, ini);
		bUseBreakpointOpcode = GetPrivateProfileBoolA("CONFIG", "USE_BREAKPOINT_OPCODE", bUseBreakpointOpcode, ini);
		bHookAfterScripts     = GetPrivateProfileBoolA("CONFIG", "HOOK_AFTER_SCRIPTS", bHookAfterScripts, ini);
		bHookOnlyRegisterScript= GetPrivateProfileBoolA("CONFIG", "HOOK_ONLY_REGISTER_SCRIPT", bHookOnlyRegisterScript, ini);
		bHookCollectParam    = GetPrivateProfileBoolA("CONFIG", "HOOK_COLLECT_PARAM", bHookCollectParam, ini);
		bHookCollectString   = GetPrivateProfileBoolA("CONFIG", "HOOK_COLLECT_STRING", bHookCollectString, ini);
		bHookFindDatatype    = GetPrivateProfileBoolA("CONFIG", "HOOK_FIND_DATATYPE", bHookFindDatatype, ini);
		bHookStoreParam      = GetPrivateProfileBoolA("CONFIG", "HOOK_STORE_PARAM", bHookStoreParam, ini);
		bHookUpdateCompareFlag=GetPrivateProfileBoolA("CONFIG", "HOOK_COMP_FLAG", bHookUpdateCompareFlag, ini);
		bHookCollectVarPointer=GetPrivateProfileBoolA("CONFIG", "HOOK_COLLECT_PTR", bHookCollectVarPointer, ini);
		nHighestCommand		 = GetPrivateProfileIntA("CONFIG", "HIGHEST_COMMAND", nHighestCommand, ini);
		bEnabled		     = GetPrivateProfileBoolA("CONFIG", "ENABLED", bEnabled, ini);

		if (!bHookAfterScripts) {
			bShowCrashWindow = false; // dependency...
		}
		
		// Read FLUSH_TIME
		{
			buf[16] = 0;
			if(GetPrivateProfileStringA("CONFIG", "FLUSH_TIME", 0, buf, 16, ini))
				for(int i = 0; aFlushTime[i]; ++i)
				{
					if(!_stricmp(buf, aFlushTime[i]))
					{
						FlushTime = i;
						break;
					}
				}
		}

		// Expressions wont work in III\VC 'cause they dont use CollectVariablePointer (or the func has been inlined)
		if(info.GetGame() != info.SA)
			bUseExpressions = false;

		// Set stream buffering mode based on FlushTime
		/*
		Using another method instead, sorry setvbuf 
		if(FlushTime == FLUSH_ON_WRITE)
			setvbuf(log, 0, _IONBF, 0);
		else
			setvbuf(log, 0, _IOFBF, nStreamSize);
		*/

		try
		{
			// We need this buffer 'cause we are not going to send things directly to stream,
			// but sprintf things to this buffer, then send to the stream.
			// [ (1024 * 12) for local vars dump ] + [ (512) for other text ]
			StringBuffer = new char[ (1024 * 12) + 512 ];

			// For crash window use
			lastLineBuffer = new char[256];
			lastLineBuffer[0] = 0;
			scname[0] = 0;

			// Commands array
			Commands = new SScriptCommand[nHighestCommand+1];

			// Buf
			if(FlushTime != FLUSH_ON_WRITE)
				LogBuffer.reserve(nStreamBufferSize);
		}
		catch(const std::bad_alloc&)
		{
			MessageBoxA(0,
				"Not enough memory for allocation.\nCheck if INI's HIGHEST_COMMAND or BUFFER_SIZE values are not too high",
				info.PluginName, MB_ICONERROR);

			fclose(log);
			return false;
		}

		// Read and Setup SCR info
		ReadINI(CommandReaderCallback);
		if(bUseExpressions) SetupCommandsExpression();

		BuildFactory();
		bOpened = true;

		if (!bEnabled) {
			Log("SCRLog is DISABLED!");
			Flush(true);
		}

		return true;
	}
	
	// Unitialize SCRLog
	void Close()
	{
		if(bOpened)
		{
			bOpened = false;
			DropBombAtFactory();
			if(log)
			{
				Flush(true);

				// Exclude some commands, like WAIT, don't need to be accurated, this may never occur, but just to make it safer
				if (bShowCrashWindow && lastCommand != 0x001 && lastCommand != 0xA93 && lastCommand != 0xABA && lastCommand != 0x04E && lastCommand != 0x459 && lastCommand != 0xED0) {
					int len = strnlen_s(scname, 8);
					if (len > 0 && len < 8) {

						char message[512] = "Crash on script named '";
						strcat(message, scname);
						strcat(message, "'\r\n\r\n");

						int lineLen = strnlen_s(lastLineBuffer, 256);
						if (lineLen > 0 && lineLen < 256) {
							strcat(message, "Last command:");
							strcat(message, lastLineBuffer);
							strcat(message, "\r\n\r\n");
						}

						strcat(message, "Check 'scrlog.log' for more information.\r\nYou may send it to script author.\r\n");

						MessageBoxA(0, message, info.PluginName, MB_ICONERROR);
					}
				}
				fclose(log);
				log = 0;
			}
		}

		delete[] lastLineBuffer;
		LogBuffer.reserve(0);
		delete[] StringBuffer;
		StringBuffer = 0;
		delete[] Commands;
		Commands = 0;
	}


	// Reads the ini file in search of command names and parameters
	// Note that this is a very basic ini parser and it does not accept white-spaces in a good way
	// So, dont use white spaces! Use KEY=VALUE
	static void ReadINI(void (*callback)(char* key, char* value))
	{
		char buffer[1024], *line, *ptr, *key, *value;
		size_t max, place;
		FILE* file = fopen(szIniName, "rt");
		if(file == 0) return;

		enum { SEC_NONE, SEC_COMMON, SEC_III, SEC_VC, SEC_SA }
			sec = SEC_NONE, valid_sec;

		// get game section
		switch(info.GetGame())
		{
			case info.III: valid_sec = SEC_III;  break;
			case info.VC:  valid_sec = SEC_VC;   break;
			case info.SA:  valid_sec = SEC_SA;   break;
			default:       return;
		}
		
		while(fgets(buffer, sizeof(buffer), file))
		{
			// Find the first non-whitespace character
			for(line = buffer; *line && isspace(*line); ++line);
			
			// Comment or nothing on line
			if(*line == ';' || *line == 0) continue;

			// Section
			if(*line == '[')
			{
				sec = SEC_NONE;

				ptr = strchr(++line, ']');
				if(ptr == 0) continue;
				else max = ptr - line;
				if(!max) continue;

				if(!strncmp(line, "III", max))
					sec = SEC_III;
				else if(!strncmp(line, "VC", max))
					sec = SEC_VC;
				else if(!strncmp(line, "SA", max))
					sec = SEC_SA;
				else if(!strncmp(line, "COMMON", max))
					sec = SEC_COMMON;

				continue;
			}

			if(sec != SEC_COMMON && sec != valid_sec)
				continue;

			// Find key and value
			{
				key = value = 0;
				place = 0;
				for(ptr = line; *ptr; ++ptr)
				{
					if( *ptr == ';' || (place == 1 && isspace(*ptr)) )
					{ *ptr = 0; break; }
					else if(place == 0 && *ptr == '=')
					{ *ptr = 0; key = line; value = ptr+1; ++place; continue; }
				}
			}

			// Check key and value validity
			if(!key || *key == 0 || !value || *value == 0)
				continue;

			callback(key, value);
		}

		fclose(file);
	}


}



