#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "UnrealAELink/Transport.h"
#include <iostream>
#include <string>
#include <cstdlib>

using namespace UnrealAELink;

namespace
{
int Failures = 0; // Test process only; no production global mutable state.
void Check(bool Passed, const char* Name)
{
    std::cout << (Passed ? "PASS: " : "FAIL: ") << Name << std::endl;
    if (!Passed) ++Failures;
}
FrameMetadata Sample(std::uint64_t Number)
{
    FrameMetadata F{};
    F.Flags = CameraValid;
    F.Source = CameraSource::EditorViewport;
    F.FrameNumber = Number;
    F.Position[0] = static_cast<double>(Number);
    F.Position[1] = -2.0 * F.Position[0];
    F.Quaternion[3] = 1;
    F.FieldOfView = 50;
    F.AspectRatio = 16.0f / 9.0f;
    return F;
}
class Child
{
public:
    explicit Child(const wchar_t* Role)
    {
        wchar_t Path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, Path, MAX_PATH);
        std::wstring Command = L"\"" + std::wstring(Path) + L"\" " + Role;
        STARTUPINFOW Startup{}; Startup.cb = sizeof(Startup);
        Started = CreateProcessW(nullptr, Command.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &Startup, &Info) != 0;
        if (Started) CloseHandle(Info.hThread);
    }
    ~Child()
    {
        if (Started) { if (WaitForSingleObject(Info.hProcess, 0) == WAIT_TIMEOUT) Kill(); CloseHandle(Info.hProcess); }
    }
    bool Running() const { return Started && WaitForSingleObject(Info.hProcess, 0) == WAIT_TIMEOUT; }
    DWORD ExitCode() const { DWORD Code = 999; if (Started) GetExitCodeProcess(Info.hProcess, &Code); return Code; }
    void Kill() { if (Started) { TerminateProcess(Info.hProcess, 99); WaitForSingleObject(Info.hProcess, 2000); } }
private:
    PROCESS_INFORMATION Info{};
    bool Started = false;
};
}

int wmain(int Argc, wchar_t** Argv)
{
    if (Argc > 1)
    {
        if (std::wstring(Argv[1]) == L"--producer-busy")
        {
            Producer P;
            return P.Open() == Result::Busy ? 0 : 15;
        }
        if (std::wstring(Argv[1]) == L"--producer-crash")
        {
            Producer P;
            if (P.Open() != Result::Ok) return 10;
            bool Connected = false;
            for (;;) { P.Publish(Sample(77), Connected); Sleep(10); }
        }
        if (std::wstring(Argv[1]) == L"--own-guard")
        {
            HANDLE Guard = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, GuardName);
            if (!Guard || WaitForSingleObject(Guard, 1000) != WAIT_OBJECT_0) return 11;
            // Let parent detect ownership with a short WaitForSingleObject timeout.
            for (;;) Sleep(100);
        }
        Consumer C;
        for (int Retry = 0; Retry < 200 && C.Open() != Result::Ok; ++Retry) Sleep(10);
        if (std::wstring(Argv[1]) == L"--receiver-crash")
        {
            FrameMetadata F{}; std::uint64_t Seq = 0, Session = 0;
            for (;;) { C.Read(F, Seq, Session); Sleep(10); }
        }
        if (std::wstring(Argv[1]) == L"--receiver-busy")
        {
            FrameMetadata F{}; std::uint64_t Seq = 0, Session = 0;
            return C.Read(F, Seq, Session) == Result::Busy ? 0 : 12;
        }
        std::uint64_t Last = 0;
        int Count = 0;
        const auto Begin = GetTickCount64();
        while (GetTickCount64() - Begin < 5000)
        {
            FrameMetadata F{}; std::uint64_t Seq = 0, Session = 0;
            if (C.Read(F, Seq, Session) == Result::Ok && Seq != Last)
            {
                if (F.Position[0] != static_cast<double>(F.FrameNumber) || F.Position[1] != -2.0 * F.Position[0]) return 13;
                Last = Seq;
                if (++Count >= 100) return 0;
            }
            Sleep(1);
        }
        return 14;
    }

    Producer P;
    if (P.Open() != Result::Ok)
    { std::cerr << "Stop UnrealAELink before running exclusive IPC tests.\n"; return 1; }
    Producer Duplicate;
    Check(Duplicate.Open() == Result::Busy, "same-thread second producer refused");
    {
        Child Other(L"--producer-busy");
        const auto Begin = GetTickCount64();
        while (Other.Running() && GetTickCount64() - Begin < 2000) Sleep(10);
        Check(Other.ExitCode() == 0, "second producer process refused");
    }
    Consumer C;
    Check(C.Open() == Result::Ok, "consumer maps producer memory");
    FrameMetadata F{}; std::uint64_t Seq = 0, Session = 0;
    Check(C.Read(F, Seq, Session) == Result::NoFrame, "no uninitialized frame exposed");
    bool Connected = false;
    P.Publish(Sample(1), Connected);
    Check(C.Read(F, Seq, Session) == Result::Ok && F.FrameNumber == 1, "valid sample decoded");
    {
        HANDLE Memory = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, MappingName);
        HANDLE Guard = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, GuardName);
        auto* Block = static_cast<SharedBlock*>(MapViewOfFile(Memory, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedBlock)));
        WaitForSingleObject(Guard, 1000);
        Block->Version = 99;
        ReleaseMutex(Guard);
        Check(C.Read(F, Seq, Session) == Result::Incompatible, "unknown protocol version rejected");
        WaitForSingleObject(Guard, 1000);
        Block->Version = ProtocolVersion;
        Block->BlockBytes = sizeof(SharedBlock) + 8;
        ReleaseMutex(Guard);
        Check(C.Read(F, Seq, Session) == Result::Incompatible, "wrong protocol size rejected");
        WaitForSingleObject(Guard, 1000);
        Block->BlockBytes = sizeof(SharedBlock);
        ReleaseMutex(Guard);
        UnmapViewOfFile(Block); CloseHandle(Guard); CloseHandle(Memory);
    }
    const auto FirstSession = Session;
    const auto FirstSeq = Seq;
    Check(C.Read(F, Seq, Session) == Result::Ok && Seq == FirstSeq, "repeated reads keep sample identity");
    P.Publish(Sample(2), Connected);
    Check(Connected, "receiver heartbeat visible to producer");
    {
        Child Other(L"--receiver-busy");
        const auto Begin = GetTickCount64();
        while (Other.Running() && GetTickCount64() - Begin < 2000) Sleep(10);
        Check(Other.ExitCode() == 0, "second receiver process refused");
    }
    Sleep(static_cast<DWORD>(LeaseTimeoutMs + 100));
    Check(C.Read(F, Seq, Session) == Result::Stale, "stale producer rejected");
    P.Publish(Sample(3), Connected);
    Check(C.Read(F, Seq, Session) == Result::Ok, "heartbeat resumes after stall");
    C.Close();
    P.Publish(Sample(4), Connected);
    Check(!Connected, "graceful receiver disconnect");
    {
        Child Reader(L"--reader");
        const auto Begin = GetTickCount64();
        std::uint64_t Number = 5;
        while (Reader.Running() && GetTickCount64() - Begin < 6000)
        { P.Publish(Sample(Number++), Connected); Sleep(2); }
        Check(Reader.ExitCode() == 0, "100 coherent changing samples across native processes");
    }
    {
        Child Reader(L"--receiver-crash");
        const auto Begin = GetTickCount64();
        Connected = false;
        while (!Connected && GetTickCount64() - Begin < 3000) { P.Publish(Sample(200), Connected); Sleep(10); }
        Check(Connected, "crash-test receiver connects");
        Reader.Kill();
        Sleep(static_cast<DWORD>(LeaseTimeoutMs + 100));
        P.Publish(Sample(201), Connected);
        Check(!Connected, "killed receiver lease expires without crashing producer");
    }
    C.Open(); C.Read(F, Seq, Session);
    P.Close();
    Check(C.Read(F, Seq, Session) == Result::Stale, "graceful producer stop rejects retained memory");
    Check(P.Open() == Result::Ok, "producer restart with receiver retaining mapping");
    P.Publish(Sample(300), Connected);
    Check(C.Read(F, Seq, Session) == Result::Ok && Session != FirstSession, "restart changes session identity");
    C.Close(); P.Close();
    {
        Child Writer(L"--producer-crash");
        const auto Begin = GetTickCount64();
        bool Received = false;
        while (!Received && GetTickCount64() - Begin < 3000)
        { C.Open(); Received = C.Read(F, Seq, Session) == Result::Ok; Sleep(10); }
        Check(Received && F.FrameNumber == 77, "crash-test producer connects");
        Writer.Kill();
        Sleep(static_cast<DWORD>(LeaseTimeoutMs + 100));
        const auto Dead = C.Read(F, Seq, Session);
        Check(Dead == Result::Stale || Dead == Result::Abandoned, "killed producer is rejected");
        Check(P.Open() == Result::Ok, "ownership reclaimed after producer crash");
        P.Publish(Sample(400), Connected);
        Check(C.Read(F, Seq, Session) == Result::Ok && F.FrameNumber == 400, "consumer follows replacement producer");
    }
    {
        Child Holder(L"--own-guard");
        HANDLE Guard = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, GuardName);
        bool Held = false;
        const auto Begin = GetTickCount64();
        while (!Held && GetTickCount64() - Begin < 2000)
        {
            const DWORD Wait = WaitForSingleObject(Guard, 1);
            Held = Wait == WAIT_TIMEOUT;
            if (Wait == WAIT_OBJECT_0 || Wait == WAIT_ABANDONED) ReleaseMutex(Guard);
            Sleep(10);
        }
        Check(Held, "child owns data mutex");
        Holder.Kill();
        Check(C.Read(F, Seq, Session) == Result::Abandoned, "abandoned write never exposed as a valid sample");
        P.Publish(Sample(401), Connected);
        Check(C.Read(F, Seq, Session) == Result::Ok, "producer repairs data after abandoned lock");
        CloseHandle(Guard);
    }
    C.Close(); P.Close();
    std::cout << "Failures: " << Failures << std::endl;
    return Failures ? 1 : 0;
}
