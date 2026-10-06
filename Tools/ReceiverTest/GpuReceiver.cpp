#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "UnrealAELink/GpuTransport.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>

int RunGpuReceiver(int Argc, char** Argv)
{
    unsigned Required = 0, Timeout = 0;
    bool ExpectChange = false, ExpectMotion = false, Moved = false;
    UnrealAELink::FrameMetadata Baseline{};
    const char* Snapshot = nullptr;
    for (int I = 1; I < Argc; ++I)
    {
        if (!std::strcmp(Argv[I], "--gpu")) continue;
        if (!std::strcmp(Argv[I], "--expect-change")) { ExpectChange = true; continue; }
        if (!std::strcmp(Argv[I], "--expect-motion")) { ExpectMotion = true; continue; }
        if (!std::strcmp(Argv[I], "--snapshot") && I + 1 < Argc) { Snapshot = Argv[++I]; continue; }
        if ((!std::strcmp(Argv[I], "--frames") || !std::strcmp(Argv[I], "--timeout")) && I + 1 < Argc)
        {
            const bool Frames = !std::strcmp(Argv[I], "--frames");
            char* End = nullptr; const auto N = std::strtoul(Argv[++I], &End, 10);
            if (!End || *End || N == 0 || N > 3600000) return 2;
            if (Frames) Required = static_cast<unsigned>(N); else Timeout = static_cast<unsigned>(N);
        }
        else { std::cerr << "ReceiverTest --gpu [--frames N] [--timeout seconds] [--expect-change] [--snapshot diagnostic.ppm]\n"; return 2; }
    }
    using namespace UnrealAELink;
    GpuConsumer Consumer;
    bool Open = false, Changed = false, Nonblack = false;
    unsigned Count = 0;
    std::uint64_t FirstHash = 0;
    Result Previous = Result::Ok;
    const auto Begin = GetTickCount64();
    std::cout << "Waiting for GPU Beauty 1280x720 RGBA8. Ctrl+C to exit.\n" << std::flush;
    while (!Timeout || GetTickCount64() - Begin < std::uint64_t(Timeout) * 1000)
    {
        auto Status = Result::Ok;
        if (!Open) { Status = Consumer.Open(); Open = Status == Result::Ok; }
        BeautyFrame Frame;
        if (Open) Status = Consumer.Read(Frame);
        if (Status == Result::Ok)
        {
            if (!Count) { FirstHash = Frame.Checksum; Baseline = Frame.Camera; }
            for (int C = 0; C < 3; ++C) Moved |= Frame.Camera.Position[C] != Baseline.Position[C];
            Changed |= Frame.Checksum != FirstHash; Nonblack |= Frame.ColoredPixels > 100;
            ++Count;
            if (Count <= 3 || Count % 10 == 0)
                std::cout << "Beauty sequence=" << Frame.Sequence << " session=" << Frame.Session
                          << " hash=" << Frame.Checksum << " coloredPixels=" << Frame.ColoredPixels
                          << " cameraX=" << Frame.Camera.Position[0] << '\n' << std::flush;
            if (Snapshot && Nonblack && Changed && (!ExpectMotion || Moved))
            {
                // Optional diagnostic export AFTER GPU receipt. Never a transport path.
                std::ofstream File(Snapshot, std::ios::binary);
                BITMAPFILEHEADER Header{}; Header.bfType = 0x4d42;
                Header.bfOffBits = sizeof(Header) + sizeof(BITMAPINFOHEADER);
                Header.bfSize = Header.bfOffBits + BeautyWidth * BeautyHeight * 4;
                BITMAPINFOHEADER Info{}; Info.biSize = sizeof(Info); Info.biWidth = BeautyWidth;
                Info.biHeight = -static_cast<LONG>(BeautyHeight); Info.biPlanes = 1; Info.biBitCount = 32;
                File.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
                File.write(reinterpret_cast<const char*>(&Info), sizeof(Info));
                for (std::size_t P = 0; P < Frame.Rgba.size(); P += 4)
                { const unsigned char Bgra[] = {Frame.Rgba[P+2], Frame.Rgba[P+1], Frame.Rgba[P], 255}; File.write(reinterpret_cast<const char*>(Bgra), 4); }
                if (!File) return 3;
                Snapshot = nullptr;
            }
            if (Required && Count >= Required && (!ExpectChange || (Changed && Nonblack)) && (!ExpectMotion || Moved))
            { std::cout << "PASS: GPU frames=" << Count << "; pixels changed=" << Changed << "; nonblack=" << Nonblack << "; camera moved=" << Moved << '\n'; return 0; }
        }
        else if (Status != Result::NoFrame && Status != Result::Busy)
        {
            if (Status != Previous) std::cout << ResultName(Status) << " HRESULT=" << std::hex << Consumer.LastError() << std::dec << '\n' << std::flush;
            Consumer.Close(); Open = false;
        }
        Previous = Status;
        Sleep(Open ? 30 : 200);
    }
    std::cerr << "FAIL: GPU timeout; frames=" << Count << "; changed=" << Changed << "; nonblack=" << Nonblack << '\n';
    return 1;
}

