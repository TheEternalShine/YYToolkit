
#include "API.hpp"
#include <Windows.h>
#undef min
#undef max
#include <utility>
#include <filesystem>
#include "../../Utils/Error.hpp"
#include "../../Utils/PEParser.hpp"
#include "../../Utils/MH/hde/hde32.h"

namespace API
{
	ModuleInfo_t GetModuleInfo()
	{
		using Fn = int(__stdcall*)(HANDLE, HMODULE, ModuleInfo_t*, DWORD);

		static HMODULE Module = GetModuleHandleA("kernel32.dll");
		static Fn K32GetModuleInformation = (Fn)GetProcAddress(Module, "K32GetModuleInformation");

		ModuleInfo_t modinfo = { 0 };
		HMODULE hModule = GetModuleHandleA(NULL);
		if (hModule == 0)
			return modinfo;
		K32GetModuleInformation(GetCurrentProcess(), hModule, &modinfo, sizeof(ModuleInfo_t));
		return modinfo;
	}


	YYTKStatus Initialize(void* pModule)
	{
		AllocConsole();
		FILE* fDummy;
		freopen_s(&fDummy, "CONIN$", "r", stdin);
		freopen_s(&fDummy, "CONOUT$", "w", stderr);
		freopen_s(&fDummy, "CONOUT$", "w", stdout);

		SetConsoleTitleA("YYToolkit Log");

		Utils::Error::Message(CLR_LIGHTBLUE, "YYToolkit %s by Archie", YYSDK_VERSION);

		bool ErrorOccured = false;
		if (!gAPIVars.Code_Execute)
		{
			YYTKStatus Status;
			ErrorOccured |= (Status = GetCodeExecuteAddr(gAPIVars.Code_Execute));

			// This is crucial to the API workings, we have to crash the game if this is not found.
			if (Status != YYTK_OK)
				Utils::Error::Error(true, "GetCodeExecuteAddr() returned %s", Utils::Error::YYTKStatus_ToString(Status));
		}

		if (!gAPIVars.Code_Function_GET_the_function)
		{
			YYTKStatus Status;
			ErrorOccured |= (Status = GetCodeFunctionAddr(gAPIVars.Code_Function_GET_the_function));
			
			// This is crucial to the API workings, we have to crash the game if this is not found.
			if (Status != YYTK_OK)
				Utils::Error::Error(true, "GetCodeFunctionAddr() returned %s", Utils::Error::YYTKStatus_ToString(Status));
		}

		if (!gAPIVars.g_pGlobal)
		{
			YYTKStatus Status;
			ErrorOccured |= (Status = GetGlobalInstance(&gAPIVars.g_pGlobal));

			// This is crucial to the API workings, we have to crash the game if this is not found.
			if (Status != YYTK_OK)
				Utils::Error::Error(true, "GetGlobalInstance() returned %s", Utils::Error::YYTKStatus_ToString(Status));
		}

		if (!gAPIVars.Window_Handle)
		{
			FunctionInfo_t Info; RValue Result; YYTKStatus Status;
			ErrorOccured |= (Status = GetFunctionByName("window_handle", Info));

			Info.Function(&Result, 0, 0, 0, 0);
			gAPIVars.Window_Handle = Result.Pointer;

			if (Status != YYTK_OK)
				Utils::Error::Error(false, "GetFunctionByName(\"window_handle\") returned %s", Utils::Error::YYTKStatus_ToString(Status));
		}

		if (!gAPIVars.Window_Device)
		{
			FunctionInfo_t Info; RValue Result; YYTKStatus Status;
			ErrorOccured |= (Status = GetFunctionByName("window_device", Info));

			Info.Function(&Result, 0, 0, 0, 0);
			gAPIVars.Window_Device = Result.Pointer;

			if (Status != YYTK_OK)
				Utils::Error::Error(false, "GetFunctionByName(\"window_device\") returned %s", Utils::Error::YYTKStatus_ToString(Status));
		}

		if (!gAPIVars.ppScripts)
		{
			YYTKStatus Status;
			ErrorOccured |= (Status = GetScriptArray(gAPIVars.ppScripts));

			if (Status != YYTK_OK)
				Utils::Error::Error(false, "GetScriptArray() returned %s", Utils::Error::YYTKStatus_ToString(Status));
		}

		gAPIVars.MainModule = pModule;

		Utils::Error::Message(CLR_DEFAULT, "- Core plugin mapped to: 0x%p", gAPIVars.MainModule);
		Utils::Error::Message(CLR_DEFAULT, "- Encountered error: %s", ErrorOccured ? "Yes" : "No");
		Utils::Error::Message(CLR_DEFAULT, "- Game Type: %s\n", IsYYC() ? "YYC" : "VM"); // Has one more newline, for spacing.

		namespace fs = std::filesystem;
		std::wstring Path = fs::current_path().wstring().append(L"\\autoexec");
		
		if (fs::is_directory(Path) && !fs::is_empty(Path))
		{
			Utils::Error::Message(CLR_LIGHTBLUE, "'autoexec' directory exists and isn't empty - loading plugins!");

			for (auto& entry : fs::directory_iterator(Path))
			{
				if (entry.path().extension().string().find(".dll") != std::string::npos)
				{
					// We have a DLL, try loading it
					if (YYTKPlugin* p = Plugins::LoadPlugin(entry.path().string().c_str()))
						Utils::Error::Message(CLR_GREEN, "[+] Loaded '%s' - mapped to 0x%p.", entry.path().filename().string().c_str(), p->PluginStart);
					else
						Utils::Error::Message(CLR_RED, "[-] Failed to load '%s' - the file may not be a plugin.", entry.path().filename().string().c_str());
				}
			}
		}

		return ErrorOccured ? YYTK_FAIL : YYTK_OK;
	}

	YYTKStatus Uninitialize()
	{
		for (auto& Plugin : gAPIVars.Plugins)
		{
			Plugins::UnloadPlugin(&Plugin.second, true);
		}

		gAPIVars.Plugins.clear();
		
		ShowWindow(GetConsoleWindow(), SW_HIDE);
		FreeConsole();

		return YYTK_OK;
	}

	DllExport YYTKStatus GetAPIVersion(char* outBuffer)
	{
		strncpy(outBuffer, YYSDK_VERSION, strlen(YYSDK_VERSION));
		return YYTK_OK;
	}

	DllExport YYTKStatus CreateCodeObject(CCode& out, char* pBytecode, size_t BytecodeSize, unsigned int Locals, const char* pName)
	{
		if (!pBytecode)
			return YYTK_INVALID;

		memset(&out, 0, sizeof(CCode));

		out.i_pVM = new VMBuffer;
		if (!out.i_pVM)
			return YYTK_NO_MEMORY;

		out.i_pVM->m_pBuffer = new char[BytecodeSize + 1];
		if (!out.i_pVM->m_pBuffer)
			return YYTK_NO_MEMORY;

		out.i_compiled = true;
		out.i_kind = 1;
		out.i_pName = pName;
		out.i_pVM->m_size = BytecodeSize;
		out.i_pVM->m_numLocalVarsUsed = Locals;
		memcpy(out.i_pVM->m_pBuffer, pBytecode, BytecodeSize);
		out.i_locals = Locals;
		out.i_flags = YYTK_MAGIC; //magic value

		return YYTK_OK;
	}

	DllExport YYTKStatus CreateYYCCodeObject(CCode& out, PFUNC_YYGML Routine, const char* pName)
	{
		if (!Routine)
			return YYTK_INVALID;

		memset(&out, 0, sizeof(CCode));

		out.i_compiled = 1;
		out.i_kind = 1;
		out.i_pName = pName;
		out.i_pFunc = new YYGMLFuncs;

		if (!out.i_pFunc)
			return YYTK_NO_MEMORY;

		out.i_pFunc->pFunc = Routine;
		out.i_pFunc->pName = pName;

		out.i_flags = YYTK_MAGIC;

		return YYTK_OK;
	}

	DllExport YYTKStatus FreeCodeObject(CCode& out)
	{
		if (out.i_flags != YYTK_MAGIC)
			return YYTK_INVALID;

		if (out.i_pVM)
		{
			if (out.i_pVM->m_pBuffer)
				delete[] out.i_pVM->m_pBuffer;

			delete out.i_pVM;
		}

		if (out.i_pFunc)
			delete out.i_pFunc;

		return YYTK_OK;
	}

	DllExport YYTKStatus GetFunctionByIndex(int index, FunctionInfo_t& outInfo)
	{
		if (index < 0)
			return YYTK_INVALID;

		void* pUnknown = nullptr;

		gAPIVars.Code_Function_GET_the_function(index, &outInfo.Name, (PVOID*)&outInfo.Function, &outInfo.Argc, &pUnknown);

		if (!outInfo.Name)
			return YYTK_NOT_FOUND;

		if (!*outInfo.Name)
			return YYTK_NOT_FOUND;

		outInfo.Index = index;

		return YYTK_OK;
	}

	DllExport YYTKStatus GetFunctionByName(const char* Name, FunctionInfo_t& outInfo)
	{
		int Index = 0;
		while (GetFunctionByIndex(Index, outInfo) == YYTK_OK)
		{
			if (_strnicmp(Name, outInfo.Name, 64) == 0)
			{
				return YYTK_OK;
			}

			Index++;
		}

		return YYTK_NOT_FOUND;
	}

	DllExport YYTKStatus GetAPIVars(APIVars_t** ppoutVars)
	{
		if (!ppoutVars)
			return YYTK_INVALID;

		*ppoutVars = &gAPIVars;
		return YYTK_OK;
	}

	DllExport YYTKStatus GetScriptArray(CDynamicArray<CScript*>*& pOutArray)
	{
		TRoutine script_exists = GetBuiltin("script_exists");

		if (!script_exists)
			return YYTK_FAIL;

		// call Script_exists
		// xor ecx, ecx
		// add esp, 0Ch
		unsigned long FuncCallPattern = FindPattern("\xE8\x00\x00\x00\x00\x00\xC9\x83\xC4\x0C", "x?????xxxx", reinterpret_cast<long>(script_exists), 0xFF);

		unsigned long Relative = *reinterpret_cast<unsigned long*>(FuncCallPattern + 1);
		Relative = (FuncCallPattern + 5) + Relative; // eip = instruction base + 5 + relative offset

		// Find pattern with HDE
		{
			hde32s Instruction;

			for (int n = 0; n < 32; n++) // Try 32 instructions
			{
				hde32_disasm(reinterpret_cast<const void*>(Relative), &Instruction);

				if ((Instruction.opcode == 0xA1 || (Instruction.opcode == 0x8B && Instruction.modrm)) && Instruction.len > 4)
				{
					if (Instruction.imm.imm32)
						pOutArray = reinterpret_cast<CDynamicArray<CScript*>*>(Instruction.imm.imm32 - sizeof(long*));
					else
						pOutArray = reinterpret_cast<CDynamicArray<CScript*>*>(Instruction.disp.disp32 - sizeof(long*));
					return YYTK_OK;
				}

				Relative += Instruction.len;
			}
		}

		return YYTK_FAIL;
	}

	DllExport YYTKStatus GetScriptByName(const char* Name, CScript*& outScript)
	{
		std::string sName(Name);

		// If the script name doesn't start with gml_Script, we have to add it before the search.
		if (!sName._Starts_with("gml_Script_"))
		{
			sName = "gml_Script_" + sName;
		}

		if (!gAPIVars.ppScripts)
		{
			if (YYTKStatus Result = GetScriptArray(gAPIVars.ppScripts))
				return Result; // Only true if the status isn't YYTK_OK
		}
		
		for (int n = 1; n < gAPIVars.ppScripts->m_arrayLength; n++)
		{
			CScript* pScript = gAPIVars.ppScripts->Elements[n];

			if (!pScript)
				continue;

			if (!pScript->s_code)
				continue;

			if (!pScript->s_code->i_pName)
				continue;

			if (!strcmp(pScript->s_code->i_pName, sName.c_str()))
			{
				outScript = pScript;
				return YYTK_OK;
			}
		}

		return YYTK_NOT_FOUND;
	}

	DllExport YYTKStatus GetScriptByID(int id, CScript*& outScript)
	{
		// Replicating game functions ftw
		if (id > 100000)
			id -= 100000;

		CDynamicArray<CScript*>* pScriptsArray;

		if (auto Result = GetScriptArray(pScriptsArray))
			return Result;

		if (id >= pScriptsArray->m_arrayLength || id <= 0)
			return YYTK_INVALID;

		outScript = pScriptsArray->Elements[id];
		return YYTK_OK;
	}

	DllExport YYTKStatus ScriptExists(const char* Name)
	{
		CScript* pScript;
		return GetScriptByName(Name, pScript);
	}

	DllExport YYTKStatus GetCodeExecuteAddr(FNCodeExecute& outAddress)
	{
		ModuleInfo_t CurInfo = GetModuleInfo();
		unsigned long Base = FindPattern("\x8A\xD8\x83\xC4\x14\x80\xFB\x01\x74", "xxxxxxxxx", CurInfo.Base, CurInfo.Size);

		if (!Base)
			return YYTK_NOT_FOUND;

		while (*(WORD*)Base != 0xCCCC)
			Base -= 1;

		Base += 2; // Compensate for the extra CC bytes

		outAddress = (FNCodeExecute)Base;

		return YYTK_OK;
	}

	DllExport YYTKStatus GetCodeFunctionAddr(FNCodeFunctionGetTheFunction& outAddress)
	{
		ModuleInfo_t CurInfo = GetModuleInfo();

		if ((outAddress = (FNCodeFunctionGetTheFunction)FindPattern("\x8B\x44\x24\x04\x3B\x05\x00\x00\x00\x00\x7F", "xxxxxx????x", CurInfo.Base, CurInfo.Size)))
			return YYTK_OK;

		return YYTK_NOT_FOUND;
	}

	DllExport unsigned long FindPattern(const char* Pattern, const char* Mask, long base, unsigned size)
	{
		size_t PatternSize = strlen(Mask);

		for (unsigned i = 0; i < size - PatternSize; i++)
		{
			int found = 1;
			for (unsigned j = 0; j < PatternSize; j++)
			{
				found &= Mask[j] == '?' || Pattern[j] == *(char*)(base + i + j);
			}

			if (found)
				return (base + i);
		}

		return (unsigned long)NULL;
	}

	DllExport YYTKStatus GetGlobalInstance(YYObjectBase** ppoutGlobal)
	{
		if (!ppoutGlobal)
			return YYTK_INVALID;

		FunctionInfo_t FunctionEntry;
		RValue Result;

		if (YYTKStatus _ret = GetFunctionByName("@@GlobalScope@@", FunctionEntry))
			return _ret;

		if (!FunctionEntry.Function)
			return YYTK_NOT_FOUND;

		FunctionEntry.Function(&Result, NULL, NULL, 0, NULL);

		*ppoutGlobal = Result.Object;

		return YYTK_OK;
	}

	DllExport YYTKStatus CallBuiltinFunction(CInstance* _pSelf, CInstance* _pOther, YYRValue& _result, int _argc, const char* Name, YYRValue* Args)
	{
		FunctionInfo_t Info;
		if (GetFunctionByName(Name, Info) == YYTK_OK)
		{
			YYGML_CallLegacyFunction(_pSelf, _pOther, _result.As<RValue>(), _argc, Info.Index, reinterpret_cast<RValue**>(&Args));
			return YYTK_OK;
		}

		return YYTK_NOT_FOUND;
	}

	DllExport TRoutine GetBuiltin(const char* Name)
	{
		FunctionInfo_t Info;
		if (GetFunctionByName(Name, Info) == YYTK_OK)
		{
			return Info.Function;
		}
		return nullptr;
	}

	DllExport YYTKStatus Global_CallBuiltin(const char* Name, int argc, YYRValue& _result, YYRValue* Args)
	{
		CInstance* g_pGlobal = reinterpret_cast<CInstance*>(gAPIVars.g_pGlobal);

		return CallBuiltinFunction(g_pGlobal, g_pGlobal, _result, argc, Name, Args);
	}

	DllExport bool IsYYC()
	{
		YYRValue Result;
		API::CallBuiltinFunction(nullptr, nullptr, Result, 0, "code_is_compiled", &Result);

		return Result;
	}

	DllExport RValue* YYGML_CallLegacyFunction(CInstance* _pSelf, CInstance* _pOther, RValue& _result, int _argc, int _id, RValue** _args)
	{
		FunctionInfo_t Info;
		if (GetFunctionByIndex(_id, Info) == YYTK_OK)
		{
			Info.Function(&_result, _pSelf, _pOther, _argc, *_args);
		}

		return &_result;
	}

	DllExport void YYGML_array_set_owner(long long _owner)
	{
		YYRValue Result;
		YYRValue Owner = _owner;
		CallBuiltinFunction(0, 0, Result, 1, "@@array_set_owner@@", &Owner);
	}

	DllExport YYRValue* YYGML_method(CInstance* _pSelf, YYRValue& _result, YYRValue& _pRef)
	{
		return &_result;
	}

	DllExport void YYGML_window_set_caption(const char* _pStr)
	{
		SetWindowTextA((HWND)gAPIVars.Window_Handle, _pStr);
	}
}

namespace Plugins
{
	DllExport void* GetPluginRoutine(const char* Name)
	{
		for (auto& Element : gAPIVars.Plugins)
		{
			if (void* Result = Element.second.GetExport<void*>(Name))
				return Result;
		}

		return nullptr;
	}

	DllExport YYTKPlugin* LoadPlugin(const char* Path)
	{
		char Buffer[MAX_PATH] = { 0 };
		FNPluginEntry lpPluginEntry = nullptr;

		GetFullPathNameA(Path, MAX_PATH, Buffer, 0);

		if (!Utils::DoesPEExportRoutine(Buffer, "PluginEntry"))
			return nullptr;

		HMODULE PluginModule = LoadLibraryA(Buffer);

		if (!PluginModule)
			return nullptr;

		lpPluginEntry = (FNPluginEntry)GetProcAddress(PluginModule, "PluginEntry");

		// Emplace in map scope
		{
			auto PluginObject = YYTKPlugin();
			memset(&PluginObject, 0, sizeof(YYTKPlugin));

			PluginObject.PluginEntry = lpPluginEntry;
			PluginObject.PluginStart = PluginModule;
			PluginObject.CoreStart = gAPIVars.MainModule;

			gAPIVars.Plugins.emplace(std::make_pair(reinterpret_cast<unsigned long>(PluginObject.PluginStart), PluginObject));
		}
		
		YYTKPlugin* refVariableInMap = &gAPIVars.Plugins.at(reinterpret_cast<unsigned long>(PluginModule));

		lpPluginEntry(refVariableInMap);

		return refVariableInMap;
	}

	bool UnloadPlugin(YYTKPlugin* pPlugin, bool Notify)
	{
		if (!pPlugin)
			return false;

		if (pPlugin->PluginUnload && Notify)
			pPlugin->PluginUnload(pPlugin);

		FreeLibrary(reinterpret_cast<HMODULE>(pPlugin->PluginStart));

		return true;
	}

	DllExport void RunHooks(YYTKEventBase* pEvent)
	{
		for (auto& Pair : gAPIVars.Plugins)
		{
			if (Pair.second.PluginHandler)
				Pair.second.PluginHandler(&Pair.second, pEvent);
		}
	}

	DllExport void CallTextCallbacks(float& x, float& y, const char*& str, int& linesep, int& linewidth)
	{
		for (auto& Pair : gAPIVars.Plugins)
		{
			if (Pair.second.OnTextRender)
				Pair.second.OnTextRender(x, y, str, linesep, linewidth);
		}
	}
}

