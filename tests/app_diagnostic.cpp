// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostic launcher for the test SDRangel runtime only; never attaches to another application.
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv){
    if(argc!=4){std::cerr<<"app_diagnostic executable dump-file stop-file\n";return 2;}
    std::wstring command=L"\""+std::wstring(argv[1])+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS,nullptr,std::filesystem::path(argv[1]).parent_path().c_str(),&startup,&process)){std::cerr<<"CreateProcess failed "<<GetLastError()<<std::endl;return 3;}
    DebugSetProcessKillOnExit(FALSE);std::cout<<"Started diagnostic SDRangel PID "<<process.dwProcessId<<std::endl;
    bool done=false;bool initialBreakpoint=true;
    while(!done){
        DEBUG_EVENT event{};
        if(!WaitForDebugEvent(&event,250)){
            if(std::filesystem::exists(argv[3])){DebugActiveProcessStop(process.dwProcessId);std::cout<<"Detached; SDRangel remains running"<<std::endl;break;}
            continue;
        }
        DWORD status=DBG_CONTINUE;
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);}
        if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT&&event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
        if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT)CloseHandle(event.u.CreateThread.hThread);
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){
            const auto code=event.u.Exception.ExceptionRecord.ExceptionCode;
            if(code==EXCEPTION_BREAKPOINT&&initialBreakpoint)initialBreakpoint=false;
            else {
                status=DBG_EXCEPTION_NOT_HANDLED;
                std::cout<<"Exception 0x"<<std::hex<<code<<" at "<<event.u.Exception.ExceptionRecord.ExceptionAddress<<std::dec<<" firstChance="<<event.u.Exception.dwFirstChance<<" thread="<<event.dwThreadId<<std::endl;
                if(!event.u.Exception.dwFirstChance){
                    HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,event.dwThreadId);
                    CONTEXT context{};context.ContextFlags=CONTEXT_FULL;GetThreadContext(thread,&context);
                    EXCEPTION_POINTERS pointers{&event.u.Exception.ExceptionRecord,&context};
                    MINIDUMP_EXCEPTION_INFORMATION info{event.dwThreadId,&pointers,FALSE};
                    HANDLE file=CreateFileW(argv[2],GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                    if(file!=INVALID_HANDLE_VALUE){MiniDumpWriteDump(process.hProcess,process.dwProcessId,file,MiniDumpWithThreadInfo,&info,nullptr,nullptr);CloseHandle(file);}
                    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_LOAD_LINES|SYMOPT_DEFERRED_LOADS);
                    SymInitialize(process.hProcess,nullptr,TRUE);
                    STACKFRAME64 frame{};frame.AddrPC.Offset=context.Rip;frame.AddrPC.Mode=AddrModeFlat;frame.AddrStack.Offset=context.Rsp;frame.AddrStack.Mode=AddrModeFlat;frame.AddrFrame.Offset=context.Rbp;frame.AddrFrame.Mode=AddrModeFlat;
                    for(int i=0;i<40;++i){
                        char storage[sizeof(SYMBOL_INFO)+1024]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;DWORD64 displacement=0;
                        std::cout<<"  0x"<<std::hex<<frame.AddrPC.Offset<<std::dec;
                        if(SymFromAddr(process.hProcess,frame.AddrPC.Offset,&displacement,symbol))std::cout<<" "<<symbol->Name<<" + "<<displacement;
                        std::cout<<std::endl;
                        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process.hProcess,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
                    }
                    SymCleanup(process.hProcess);CloseHandle(thread);
                }
            }
        }
        if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){std::cout<<"SDRangel exit code 0x"<<std::hex<<event.u.ExitProcess.dwExitCode<<std::dec<<std::endl;done=true;}
        ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status);
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
}
