#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "UnrealAELink/RequestProtocol.h"
#include "FrameClient.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <vector>
#include <utility>
using namespace UnrealAELink;
static void Check(bool Ok, const char* Message) { if (!Ok) throw std::runtime_error(Message); }
static int ClientTest()
{
    RequestChannel Client;
    Check(Client.Open(false) == Result::Ok, "Open request client");
    std::uint64_t Id = 100;
    for (const auto Time : {20,3,17,0,29})
    {
        FrameRequest R; R.RequestId = ++Id; R.TimeValue = Time * 1001; R.TimeScale = 30000;
        Check(Client.Submit(R) == Result::Ok, "Submit exact rational request");
        Check(Client.Submit(R) == Result::Busy, "Outstanding mailbox must serialize requests");
        FrameResponse Response;
        const auto Begin = GetTickCount64(); Result Code = Result::NoFrame;
        while (GetTickCount64()-Begin < 3000 && (Code = Client.Receive(Id, Response)) == Result::NoFrame) Sleep(2);
        Check(Code == Result::Ok && Response.RequestId == Id && Response.RequestedTimeValue == R.TimeValue &&
            Response.RequestedTimeScale == R.TimeScale && Response.BeautySession == 123 && Response.BeautySequence == Id+1000,
            "Response must preserve request/time/resource identity");
        Check(Client.Receive(Id+1, Response) == Result::NoFrame, "Unrelated response id must never match");
        Check(Client.Release(Id) == Result::Ok, "Acknowledge copied result");
    }
    return 0;
}
static int UnrealTest()
{
    FrameClient Client; const auto Id=Client.Register(1);
    std::vector<std::pair<std::int64_t,std::int64_t>> Times{{0,30},{30,30},{60,30}};
    for (int Time=0;Time<=30;++Time) Times.emplace_back(Time,30);
    for (const int Time : {20,3,17,0,29}) Times.emplace_back(Time,30);
    Times.emplace_back(1,60); Times.emplace_back(17*1001,30000);
    std::vector<double> Sequential;
    std::size_t Index=0;
    for(const auto& Time : Times)
    {
        FrameStatus Status{}; auto Image=Client.RequestFrame(Id,Time.first,Time.second,Status);
        Check(Image && Status==FrameStatus::Success,"Unreal exact request failed");
        double Sum=0, Count=0;
        for(std::uint32_t Y=0;Y<Image->Height;++Y) for(std::uint32_t X=0;X<Image->Width;++X)
        {
            const auto* P=Image->Rgba.data()+(std::size_t(Y)*Image->Width+X)*4;
            if(unsigned(P[0])+P[1]+P[2]>90) { Sum+=X; ++Count; }
        }
        std::printf("UNREAL REQUEST id=%llu time=%lld/%lld beauty=%llu pixels=%llu centroid=%.3f hash=%llu\n",
            static_cast<unsigned long long>(Image->Identity.RequestId),static_cast<long long>(Time.first),static_cast<long long>(Time.second),static_cast<unsigned long long>(Image->Sequence),
            static_cast<unsigned long long>(Count),Count?Sum/Count:-1,static_cast<unsigned long long>(Image->Checksum));
        Check(Count>1000,"Unreal evaluated capture is empty");
        const double Seconds=double(Time.first)/double(Time.second);
        const double Expected=640+(-240+240*Seconds)*(640/std::tan(35*3.141592653589793/180))/550;
        Check(std::abs(Sum/Count-Expected)<45,"Unreal captured incorrect requested state");
        if (Index>=3 && Index<34)
        {
            if (!Sequential.empty()) Check(Sum/Count>Sequential.back()+5,"Unreal sequential animation must advance every frame");
            Sequential.push_back(Sum/Count);
        }
        if (Index>=34 && Index<39) Check(std::abs(Sum/Count-Sequential[std::size_t(Time.first)])<1,"Random access differs from sequential pixels");
        ++Index;
    }
    std::puts("PASS: actual UE Sequencer/capture, 31-frame progression, random access, half-frame and NTSC rational times (native receiver; not AE host).");
    return 0;
}
int main(int Argc, char** Argv)
{
    try
    {
        if (Argc == 2 && !std::strcmp(Argv[1], "--client")) return ClientTest();
        if (Argc == 2 && !std::strcmp(Argv[1], "--unreal")) return UnrealTest();
        int32_t Frame = 0; float Fraction = 0;
        Check(RationalToFrame(17*1001,30000,30,1,Frame,Fraction) && Frame==17 && Fraction > 0.016999f && Fraction < 0.017001f, "NTSC rational to Sequencer subframe");
        Check(RationalToFrame(1,60,30,1,Frame,Fraction) && Frame==0 && Fraction==0.5f, "AE time must retain subframes");
        Check(RationalToFrame(120,24,24000,1,Frame,Fraction) && Frame==120000 && Fraction==0, "Tick resolution conversion");
        Check(RationalToFrame(1001,30000,30000,1001,Frame,Fraction) && Frame==1 && Fraction==0, "Noninteger Sequence frame rate");
        Check(!RationalToFrame(-1,30,30,1,Frame,Fraction) && !RationalToFrame(0,0,30,1,Frame,Fraction), "Reject unsupported time");
        Check(!RationalToFrame(INT64_MAX,1,24000,1,Frame,Fraction), "Reject rational multiplication overflow");
        RequestChannel Server; Check(Server.Open(true)==Result::Ok, "Open server (close UE first)");
        RequestChannel Duplicate; Check(Duplicate.Open(true)==Result::Busy, "Exclusive request producer");
        wchar_t Path[MAX_PATH]{}; GetModuleFileNameW(nullptr,Path,MAX_PATH);
        wchar_t Command[MAX_PATH+32]{}; swprintf_s(Command,L"\"%s\" --client",Path);
        STARTUPINFOW Startup{}; Startup.cb=sizeof(Startup); PROCESS_INFORMATION Child{};
        Check(CreateProcessW(nullptr,Command,nullptr,nullptr,0,CREATE_NO_WINDOW,nullptr,nullptr,&Startup,&Child)!=0,"Launch cross-process requester");
        CloseHandle(Child.hThread);
        unsigned Count=0; const auto Begin=GetTickCount64();
        while (GetTickCount64()-Begin<8000 && WaitForSingleObject(Child.hProcess,0)!=WAIT_OBJECT_0)
        {
            FrameRequest R; bool Deterministic=false;
            const auto Code=Server.Poll(R,Deterministic);
            if (Code==Result::Ok)
            {
                Check(Deterministic,"Requested mailbox suspends Live");
                FrameResponse Response; Response.RequestId=R.RequestId;
                Response.RequestedTimeValue=R.TimeValue; Response.RequestedTimeScale=R.TimeScale;
                Check(Server.Complete(Response)==Result::NoFrame,"Pending GPU work must not be acknowledged");
                Response.Status=FrameStatus::Success; Response.BeautySession=123; Response.BeautySequence=R.RequestId+1000;
                auto Wrong=Response; ++Wrong.RequestId;
                Check(Server.Complete(Wrong)==Result::NoFrame,"Reject completion for a different request");
                Wrong=Response; ++Wrong.RequestedTimeScale;
                Check(Server.Complete(Wrong)==Result::Incompatible,"Reject completion for a different rational time");
                Check(Server.Complete(Response)==Result::Ok,"Complete matching request"); ++Count;
            }
            Sleep(2);
        }
        DWORD Exit=1;
        if (WaitForSingleObject(Child.hProcess,1000)!=WAIT_OBJECT_0) { TerminateProcess(Child.hProcess,1); WaitForSingleObject(Child.hProcess,1000); }
        GetExitCodeProcess(Child.hProcess,&Exit); CloseHandle(Child.hProcess);
        Check(Exit==0 && Count==5,"Five non-sequential cross-process requests"); Server.Close();
        // Real absence/timeout path: no live fallback and bounded callback wait.
        FrameClient Frames; const auto Id=Frames.Register(1); FrameStatus Status=FrameStatus::Pending;
        const auto Start=GetTickCount64(); auto Image=Frames.RequestFrame(Id,1,60,Status);
        Check(!Image && Status==FrameStatus::Timeout && GetTickCount64()-Start < RequestTimeoutMs+1000,"Missing producer must fail with bounded timeout");
        bool Abort=true; Image=Frames.RequestFrame(Id,2,60,Status,[&] { return Abort; });
        Check(!Image && Status==FrameStatus::Cancelled,"Host abort must cancel deterministic wait");
        std::puts("PASS: rational/subframe conversion, cross-process random-access mailbox, identities, serialized ownership, no premature ack, bounded timeout, cancellation.");
        return 0;
    }
    catch(const std::exception& E) { std::fprintf(stderr,"FAIL: %s\n",E.what()); return 1; }
}
