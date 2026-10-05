#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "UnrealAELink/Transport.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

int main(int Argc, char** Argv)
{
    unsigned FramesRequired = 0;
    unsigned TimeoutSeconds = 0;
    bool ExpectMotion = false;
    for (int I = 1; I < Argc; ++I)
    {
        if (!std::strcmp(Argv[I], "--expect-motion")) ExpectMotion = true;
        else if ((!std::strcmp(Argv[I], "--frames") || !std::strcmp(Argv[I], "--timeout")) && I + 1 < Argc)
        {
            const bool IsFrames = !std::strcmp(Argv[I], "--frames");
            char* End = nullptr;
            const auto Value = std::strtoul(Argv[++I], &End, 10);
            if (!End || *End || Value == 0 || Value > 3600000) return 2;
            if (IsFrames) FramesRequired = static_cast<unsigned>(Value);
            else TimeoutSeconds = static_cast<unsigned>(Value);
        }
        else
        {
            std::cout << "ReceiverTest [--frames N] [--timeout seconds] [--expect-motion]\n"
                         "No arguments: keep watching until Ctrl+C.\n";
            return std::strcmp(Argv[I], "--help") ? 2 : 0;
        }
    }
    using namespace UnrealAELink;
    Consumer Receiver;
    bool Open = false, Connected = false, Moved = false;
    std::uint64_t LastSequence = 0, LastSession = 0;
    FrameMetadata Baseline{};
    unsigned Count = 0;
    Result LastStatus = Result::Ok;
    const auto Begin = GetTickCount64();
    std::cout << "Waiting for UnrealAELink (protocol 1). Ctrl+C to exit.\n" << std::flush;
    while (!TimeoutSeconds || GetTickCount64() - Begin < std::uint64_t(TimeoutSeconds) * 1000)
    {
        Result Status = Result::Ok;
        if (!Open) { Status = Receiver.Open(); Open = Status == Result::Ok; }
        FrameMetadata Frame{};
        std::uint64_t Sequence = 0, Session = 0;
        if (Open) Status = Receiver.Read(Frame, Sequence, Session);
        if (Status == Result::Ok)
        {
            if (!Connected || Session != LastSession)
            {
                std::cout << "Connected to UnrealAELink; session=" << Session << '\n';
                Connected = true;
                Baseline = Frame;
                LastSequence = 0;
            }
            if (Sequence != LastSequence || Session != LastSession)
            {
                for (int I = 0; I < 3; ++I)
                    Moved |= Frame.Position[I] != Baseline.Position[I] || Frame.Rotation[I] != Baseline.Rotation[I];
                Moved |= Frame.FieldOfView != Baseline.FieldOfView;
                std::cout << std::fixed << std::setprecision(4)
                          << "\nFrame: " << Frame.FrameNumber << "  Sequence: " << Sequence
                          << "\nTime: " << Frame.TimeSeconds
                          << "\nCamera: " << (Frame.Source == CameraSource::EditorViewport ? "Editor viewport" : "Player camera")
                          << "\nPosition: X=" << Frame.Position[0] << " Y=" << Frame.Position[1] << " Z=" << Frame.Position[2]
                          << "\nRotation: P=" << Frame.Rotation[0] << " Y=" << Frame.Rotation[1] << " R=" << Frame.Rotation[2]
                          << "\nFOV: " << Frame.FieldOfView << "\nAspect: " << Frame.AspectRatio << '\n' << std::flush;
                LastSequence = Sequence;
                LastSession = Session;
                ++Count;
                if (FramesRequired && Count >= FramesRequired && (!ExpectMotion || Moved))
                {
                    std::cout << "PASS: received " << Count << " distinct samples; camera changed=" << Moved << '\n';
                    return 0;
                }
            }
        }
        else
        {
            if (Status != LastStatus)
                std::cout << ResultName(Status) << " (Win32=" << Receiver.LastError() << ")\n" << std::flush;
            if (Status != Result::Busy && Status != Result::NoFrame)
            {
                if (Connected) std::cout << "Disconnected from UnrealAELink\n" << std::flush;
                Connected = false;
                Receiver.Close();
                Open = false;
                // A retained mapping may belong to a dead producer. Reopen, don't pin it forever.
                Sleep(200);
            }
        }
        LastStatus = Status;
        Sleep(100); // 10 Hz output/lease refresh, independent of Unreal's publication rate.
    }
    std::cerr << "FAIL: timeout; samples=" << Count << ", camera changed=" << Moved << '\n';
    return 1;
}
