#pragma once
#include <Psapi.h>
#include <TlHelp32.h>
#include <Windows.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;

//////////////////////////////////// structs //////////////////////////////////

typedef NTSTATUS(WINAPI* pNtReadVirtualMemory)(HANDLE ProcessHandle,
    PVOID BaseAddress, PVOID Buffer,
    ULONG NumberOfBytesToRead,
    PULONG NumberOfBytesRead);
typedef NTSTATUS(WINAPI* pNtWriteVirtualMemory)(HANDLE ProcessHandle,
    PVOID BaseAddress, PVOID Buffer,
    ULONG NumberOfBytesToWrite,
    PULONG NumberOfBytesWritten);

////////////////////////////////////// classes
/////////////////////////////////////

class MemorysenseMem {
private:
    HANDLE Handle = nullptr;
    DWORD ProcessID = 0;
    pNtReadVirtualMemory VRead = nullptr;
    pNtWriteVirtualMemory VWrite = nullptr;

    uintptr_t GetProcessId(string_view ProcessName) {
        PROCESSENTRY32 Pe;
        Pe.dwSize = sizeof(PROCESSENTRY32);
        HANDLE Ss = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (Ss == INVALID_HANDLE_VALUE) {
            return 0;
        }

        bool Found = false;
        if (Process32First(Ss, &Pe)) {
            do {
                if (ProcessName.compare(Pe.szExeFile) == 0) {
                    ProcessID = Pe.th32ProcessID;
                    Found = true;
                    break;
                }
            } while (Process32Next(Ss, &Pe));
        }

        CloseHandle(Ss);
        return Found ? ProcessID : 0;
    }

    uintptr_t GetBaseModule(string_view ModuleName) {
        if (!Handle) return 0;

        HMODULE Modules[1024];
        DWORD NeededModule;

        if (EnumProcessModules(Handle, Modules, sizeof(Modules), &NeededModule)) {
            int ModuleCount = NeededModule / sizeof(HMODULE);
            for (int I = 0; I < ModuleCount; ++I) {
                char Buffer[MAX_PATH];
                if (GetModuleBaseNameA(Handle, Modules[I], Buffer, sizeof(Buffer))) {
                    if (ModuleName.compare(Buffer) == 0) {
                        return reinterpret_cast<uintptr_t>(Modules[I]);
                    }
                }
            }
        }
        return 0;
    }

public:
    struct {
        bool ProcessAttached{ false };
        bool DebugMode{ false };
        bool ValidHandle{ false };
        bool NtApiLoaded{ false };
    } Status;

    MemorysenseMem(const vector<string>& Processes) {
        VRead = (pNtReadVirtualMemory)GetProcAddress(GetModuleHandleA("ntdll.dll"),
            "NtReadVirtualMemory");
        VWrite = (pNtWriteVirtualMemory)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "NtWriteVirtualMemory");

        Status.NtApiLoaded = (VRead && VWrite);
        if (!Status.NtApiLoaded) {
            if (Status.DebugMode)
                cerr << "[Memory.cpp] Failed to get NT API function addresses." << endl;
            return;
        }

        for (const auto& Name : Processes) {
            ProcessID = GetProcessId(Name);
            if (ProcessID != 0) {
                Handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessID);
                if (Handle && Handle != INVALID_HANDLE_VALUE) {
                    Status.ValidHandle = true;
                    Status.ProcessAttached = true;
                    break;
                }
                else if (Handle) {
                    CloseHandle(Handle);
                    Handle = nullptr;
                }
            }
        }

        if (!Status.ProcessAttached && Status.DebugMode) {
            cerr << "[Memory.cpp] Failed to open handle to any of the specified "
                "processes."
                << endl;
        }
    }

    MemorysenseMem(string_view ProcessName) {
        VRead = (pNtReadVirtualMemory)GetProcAddress(GetModuleHandleA("ntdll.dll"),
            "NtReadVirtualMemory");
        VWrite = (pNtWriteVirtualMemory)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "NtWriteVirtualMemory");

        Status.NtApiLoaded = (VRead && VWrite);
        if (!Status.NtApiLoaded) {
            if (Status.DebugMode)
                cerr << "[Memory.cpp] Failed to get NT API function addresses." << endl;
            return;
        }

        ProcessID = GetProcessId(ProcessName);
        if (ProcessID != 0) {
            Handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessID);
            if (Handle && Handle != INVALID_HANDLE_VALUE) {
                Status.ValidHandle = true;
                Status.ProcessAttached = true;
            }
            else {
                if (Status.DebugMode)
                    cerr << "[Memory.cpp] Failed to open handle to process." << endl;
                Handle = nullptr;
            }
        }
        else {
            if (Status.DebugMode)
                cerr << "[Memory.cpp] Process not found: " << ProcessName << endl;
        }
    }

    ~MemorysenseMem() {
        if (Handle && Handle != INVALID_HANDLE_VALUE) {
            CloseHandle(Handle);
        }
    }

    MemorysenseMem(const MemorysenseMem&) = delete;
    MemorysenseMem& operator=(const MemorysenseMem&) = delete;

    MemorysenseMem(MemorysenseMem&& Other) noexcept
        : Handle(Other.Handle),
        ProcessID(Other.ProcessID),
        VRead(Other.VRead),
        VWrite(Other.VWrite),
        Status(Other.Status) {
        Other.Handle = nullptr;
        Other.ProcessID = 0;
        Other.VRead = nullptr;
        Other.VWrite = nullptr;
        Other.Status = {};
    }

    MemorysenseMem& operator=(MemorysenseMem&& Other) noexcept {
        if (this != &Other) {
            if (Handle && Handle != INVALID_HANDLE_VALUE) {
                CloseHandle(Handle);
            }

            Handle = Other.Handle;
            ProcessID = Other.ProcessID;
            VRead = Other.VRead;
            VWrite = Other.VWrite;
            Status = Other.Status;

            Other.Handle = nullptr;
            Other.ProcessID = 0;
            Other.VRead = nullptr;
            Other.VWrite = nullptr;
            Other.Status = {};
        }
        return *this;
    }

    uintptr_t GetBase(string_view ModuleName) {
        return GetBaseModule(ModuleName);
    }

    template <typename T>
    T Read(uintptr_t Address) {
        T Buffer{};
        if (!Status.ValidHandle || !VRead) return Buffer;

        ULONG BytesRead = 0;
        NTSTATUS Result = VRead(Handle, reinterpret_cast<void*>(Address), &Buffer,
            sizeof(T), &BytesRead);

        if (Result != 0 || BytesRead != sizeof(T)) {
            return T{};
        }

        return Buffer;
    }

    template <typename T>
    bool Write(uintptr_t Address, const T& Value) {
        if (!Status.ValidHandle || !VWrite) return false;

        ULONG BytesWritten = 0;
        NTSTATUS Result = VWrite(Handle, reinterpret_cast<void*>(Address),
            const_cast<T*>(&Value), sizeof(T), &BytesWritten);

        return (Result == 0 && BytesWritten == sizeof(T));
    }

    bool ReadRaw(uintptr_t Address, void* Buffer, size_t Size) {
        if (!Status.ValidHandle || !VRead || !Buffer || Size == 0) return false;

        ULONG BytesRead = 0;
        NTSTATUS Result = VRead(Handle, reinterpret_cast<void*>(Address), Buffer,
            static_cast<ULONG>(Size), &BytesRead);

        return (Result == 0 && BytesRead == Size);
    }

    bool WriteRaw(uintptr_t Address, const void* Buffer, size_t Size) {
        if (!Status.ValidHandle || !VWrite || !Buffer || Size == 0) return false;

        ULONG BytesWritten = 0;
        NTSTATUS Result = VWrite(Handle, reinterpret_cast<void*>(Address),
            const_cast<void*>(Buffer),
            static_cast<ULONG>(Size), &BytesWritten);

        return (Result == 0 && BytesWritten == Size);
    }

    bool ProcessIsOpen(string_view ProcessName) {
        return GetProcessId(ProcessName) != 0;
    }

    bool InForeground(const string& WindowName) {
        HWND Current = GetForegroundWindow();
        if (!Current) return false;

        char Title[256];
        int Length = GetWindowTextA(Current, Title, sizeof(Title));

        if (Length > 0) {
            return (strstr(Title, WindowName.c_str()) != nullptr);
        }

        return false;
    }

    DWORD GetProcessID() const { return ProcessID; }

    HANDLE GetHandle() const { return Handle; }
};

inline MemorysenseMem* Memory = nullptr;