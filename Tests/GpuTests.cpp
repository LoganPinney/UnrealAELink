#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "UnrealAELink/GpuTransport.h"
#include "FrameClient.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <atomic>
using Microsoft::WRL::ComPtr;
using namespace UnrealAELink;
void Check(bool Ok, const char* Message) { if (!Ok) throw std::runtime_error(Message); }
void Hr(HRESULT Code) { Check(SUCCEEDED(Code), "DirectX operation failed"); }

int Receive()
{
    GpuConsumer Consumer; bool Open = false; unsigned Count = 0;
    const auto Begin = GetTickCount64();
    while (GetTickCount64() - Begin < 10000)
    {
        if (!Open) Open = Consumer.Open() == Result::Ok;
        BeautyFrame Frame;
        if (Open && Consumer.Read(Frame) == Result::Ok)
        {
            Check(Frame.Rgba.size() == BeautyWidth * BeautyHeight * 4, "Unexpected packed image size");
            const auto Red = static_cast<unsigned char>(Frame.Camera.FrameNumber);
            for (std::size_t P = 0; P < Frame.Rgba.size(); P += 4)
                Check(Frame.Rgba[P] == Red && Frame.Rgba[P+1] == 73 && Frame.Rgba[P+2] == 149 && Frame.Rgba[P+3] == 255,
                      "Shared frame torn, stale, channel-swapped or incorrectly pitched");
            Check(Frame.Camera.AspectRatio == float(BeautyWidth) / BeautyHeight, "Camera/image aspect mismatch");
            if (++Count >= 12) return 0;
        }
        Sleep(10);
    }
    return 1;
}

int ReceiveClient()
{
    FrameClient Client;
    const auto Id = Client.Register(1); const auto Other = Client.Register(2);
    unsigned Count = 0; std::uint64_t Previous = 0;
    const auto Begin = GetTickCount64();
    while (GetTickCount64() - Begin < 10000)
    {
        auto Frame = Client.Snapshot(Id, true, true);
        if (Frame && Frame->Sequence != Previous)
        {
            Previous = Frame->Sequence;
            const auto Red = static_cast<unsigned char>(Frame->Camera.FrameNumber);
            for (std::size_t P = 0; P < Frame->Rgba.size(); P += 4)
                Check(Frame->Rgba[P] == Red && Frame->Rgba[P+1] == 73 && Frame->Rgba[P+2] == 149 && Frame->Rgba[P+3] == 255,
                    "Adobe worker snapshot is inconsistent");
            if (++Count == 6)
            {
                Client.Snapshot(Other, true, true);
                auto Frozen = Client.Snapshot(Id, true, false);
                Check(bool(Frozen), "Freeze has a completed image");
                Sleep(120);
                Check(Client.Snapshot(Id, true, false) == Frozen, "Another live instance must not change a frozen snapshot");
                Client.Remove(Other);
                Client.Snapshot(Id, true, true);
            }
            if (Count >= 12)
            {
                Check(!Client.Snapshot(Id, false, true), "Disconnected instance must not return a frame");
                Client.Remove(Id);
                return 0;
            }
        }
        Sleep(10);
    }
    return 1;
}

int ReceiveRequests()
{
    FrameClient Client; const auto Id = Client.Register(1);
    FrameStatus AStatus{}, BStatus{}; std::shared_ptr<const BeautyFrame> A, B;
    std::atomic<unsigned> Waiting{0}; std::atomic<bool> Go{false};
    auto SameRequest = [&](std::shared_ptr<const BeautyFrame>& Image, FrameStatus& Status)
    {
        ++Waiting; while (!Go) Sleep(1);
        Image = Client.RequestFrame(Id,20,30,Status);
    };
    std::thread First(SameRequest,std::ref(A),std::ref(AStatus));
    std::thread Second(SameRequest,std::ref(B),std::ref(BStatus));
    while (Waiting < 2) Sleep(1); Go = true;
    First.join(); Second.join();
    Check(A && B && A == B && AStatus == FrameStatus::Success && BStatus == FrameStatus::Success,
        "Identical outstanding PreRender requests must share one immutable result");
    auto Previous = A->Identity.RequestId;
    for (const int Time : {3,17,0,29})
    {
        FrameStatus Status{}; auto Image = Client.RequestFrame(Id,Time,30,Status);
        Check(Image && Status == FrameStatus::Success && Image->Identity.RequestId > Previous,
            "Nonsequential requests need fresh monotonically increasing ids");
        Previous = Image->Identity.RequestId;
        Check(Image->Identity.TimeValue == Time && Image->Identity.TimeScale == 30,
            "GPU slot must preserve exact rational time");
        for (std::size_t P=0;P<Image->Rgba.size();P+=4)
            Check(Image->Rgba[P] == Time && Image->Rgba[P+1] == 73 && Image->Rgba[P+2] == 149,
                "Requested pixels must not be substituted by live or unrelated request pixels");
    }
    // Server deliberately returns a GPU slot with the right id but wrong time.
    FrameStatus Status{}; auto Bad = Client.RequestFrame(Id,99,30,Status);
    Check(!Bad && Status == FrameStatus::CaptureFailed, "Wrong rational slot identity must fail cleanly");
    Check(A->Identity.TimeValue == 20 && A->Rgba[0] == 20, "Earlier PreRender remains immutable after later requests");
    return 0;
}

int main(int Argc, char** Argv)
{
    try
    {
        if (Argc == 2 && !std::strcmp(Argv[1], "--receive")) return Receive();
        if (Argc == 2 && !std::strcmp(Argv[1], "--receive-client")) return ReceiveClient();
        if (Argc == 2 && !std::strcmp(Argv[1], "--receive-request")) return ReceiveRequests();
        const bool TestClient = Argc == 2 && !std::strcmp(Argv[1], "--client-test");
        const bool TestRequest = Argc == 2 && !std::strcmp(Argv[1], "--request-test");
        RequestChannel Requests;
        if (TestRequest) Check(Requests.Open(true) == Result::Ok, "Request server open");
        ComPtr<ID3D12Device> Device; Hr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)));
        GpuProducer Producer; Check(Producer.Open(Device.Get()) == Result::Ok, "GPU producer open failed (close any running UE bridge)");
        GpuProducer Duplicate; Check(Duplicate.Open(Device.Get()) == Result::Busy, "Duplicate producer must fail");
        uint32_t Slot = 0; uint64_t Value = 0; bool Connected = false;
        Check(Producer.Reserve(Slot, Value, Connected) == Result::NoFrame && !Connected, "Unwatched producer should not copy");
        ComPtr<ID3D12CommandQueue> Queue; D3D12_COMMAND_QUEUE_DESC Q{};
        Hr(Device->CreateCommandQueue(&Q, IID_PPV_ARGS(&Queue)));
        ComPtr<ID3D12CommandAllocator> Allocator; Hr(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator)));
        ComPtr<ID3D12GraphicsCommandList> List;
        Hr(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS(&List))); Hr(List->Close());
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint{}; UINT Rows; UINT64 RowSize, Size;
        const auto TextureDesc = Producer.Texture(0)->GetDesc();
        Device->GetCopyableFootprints(&TextureDesc, 0, 1, 0, &Footprint, &Rows, &RowSize, &Size);
        D3D12_HEAP_PROPERTIES Heap{}; Heap.Type = D3D12_HEAP_TYPE_UPLOAD; Heap.CreationNodeMask = Heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC Buffer{}; Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        Buffer.Width = Size; Buffer.Height = 1; Buffer.DepthOrArraySize = Buffer.MipLevels = 1;
        Buffer.SampleDesc.Count = 1; Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> Upload;
        Hr(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&Upload)));
        wchar_t Path[MAX_PATH]{}; Check(GetModuleFileNameW(nullptr, Path, MAX_PATH) != 0, "Executable path");
        wchar_t Command[MAX_PATH + 32]{}; swprintf_s(Command, L"\"%s\" %s", Path, TestRequest ? L"--receive-request" : TestClient ? L"--receive-client" : L"--receive");
        STARTUPINFOW Startup{}; Startup.cb = sizeof(Startup); PROCESS_INFORMATION Child{};
        Check(CreateProcessW(nullptr, Command, nullptr, nullptr, 0, CREATE_NO_WINDOW, nullptr, nullptr, &Startup, &Child) != 0, "Child process");
        CloseHandle(Child.hThread);
        const auto Begin = GetTickCount64(); uint64_t Last = 0; bool Passed = false;
        FrameRequest Request; FrameResponse Response; bool Pending = false, PublishedRequest = false;
        unsigned RequestCount = 0, Distractors = 0; std::uint64_t ReceivedAt = 0;
        while (GetTickCount64() - Begin < 12000)
        {
            if (WaitForSingleObject(Child.hProcess, 0) == WAIT_OBJECT_0)
            { DWORD Code = 1; GetExitCodeProcess(Child.hProcess, &Code); Passed = Code == 0; break; }
            if (TestRequest)
            {
                bool Deterministic = false; FrameRequest Incoming;
                if (!Pending && Requests.Poll(Incoming, Deterministic) == Result::Ok)
                {
                    Request = Incoming; Pending = true; PublishedRequest = false; Distractors = 0; ReceivedAt = GetTickCount64(); ++RequestCount;
                }
                if (Pending && PublishedRequest)
                {
                    if (Producer.Fence()->GetCompletedValue() >= Response.BeautySequence)
                    {
                        const auto Code = Requests.Complete(Response);
                        if (Code == Result::Ok || Code == Result::NoFrame) Pending = false;
                    }
                    Producer.Heartbeat(); Sleep(2); continue;
                }
                if (Pending && GetTickCount64()-ReceivedAt < 100) { Producer.Heartbeat(); Sleep(2); continue; }
                if (!Pending && Deterministic) { Producer.Heartbeat(); Sleep(2); continue; }
            }
            if (Producer.Reserve(Slot, Value, Connected) != Result::Ok) { Sleep(5); continue; }
            // One upload allocation: don't change it until our previous GPU copy retires.
            const auto WaitBegin = GetTickCount64();
            while (Producer.Fence()->GetCompletedValue() < Last && GetTickCount64() - WaitBegin < 2000) Sleep(1);
            Check(Producer.Fence()->GetCompletedValue() >= Last, "Producer fence timeout");
            unsigned char* Pixels = nullptr; D3D12_RANGE NoReads{0,0}; Hr(Upload->Map(0, &NoReads, reinterpret_cast<void**>(&Pixels)));
            for (UINT Y = 0; Y < BeautyHeight; ++Y)
                for (UINT X = 0; X < BeautyWidth; ++X)
                {
                    auto* P = Pixels + Footprint.Offset + Y * Footprint.Footprint.RowPitch + X * 4;
                    P[0] = static_cast<unsigned char>(TestRequest && Pending ? Request.TimeValue : Value); P[1] = 73; P[2] = 149; P[3] = 0;
                }
            Upload->Unmap(0, nullptr); Hr(Allocator->Reset()); Hr(List->Reset(Allocator.Get(), nullptr));
            auto* Texture = Producer.Texture(Slot);
            D3D12_RESOURCE_BARRIER B{}; B.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            B.Transition.pResource = Texture; B.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            B.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; B.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            List->ResourceBarrier(1, &B);
            D3D12_TEXTURE_COPY_LOCATION Source{}; Source.pResource = Upload.Get(); Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; Source.PlacedFootprint = Footprint;
            D3D12_TEXTURE_COPY_LOCATION Dest{}; Dest.pResource = Texture; Dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            List->CopyTextureRegion(&Dest, 0, 0, 0, &Source, nullptr);
            B.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; B.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON; List->ResourceBarrier(1, &B);
            Hr(List->Close()); ID3D12CommandList* Lists[] = {List.Get()}; Queue->ExecuteCommandLists(1, Lists);
            Hr(Queue->Signal(Producer.Fence(), Value)); Last = Value;
            FrameMetadata Camera{}; Camera.FrameNumber = Value;
            FrameIdentity Identity;
            if (TestRequest && Pending)
            {
                Identity = {Request.RequestId,Request.TimeValue,Request.TimeScale};
                // Both Live and a wrong request id are newer than old frames.
                // The exact receiver must skip these completed distractors.
                if (Distractors == 0) Identity = {};
                else if (Distractors == 1) ++Identity.RequestId;
                else if (Request.TimeValue == 99) ++Identity.TimeValue;
            }
            const auto Published = Producer.Publish(Slot, Value, Camera, Identity);
            Check(Published == Result::Ok || Published == Result::Busy, "GPU frame publish");
            if (TestRequest && Pending && Published == Result::Ok && ++Distractors >= 3)
            {
                Response = FrameResponse{}; Response.RequestId = Request.RequestId;
                Response.RequestedTimeValue = Request.TimeValue; Response.RequestedTimeScale = Request.TimeScale;
                Response.BeautySession = Producer.Session(); Response.BeautySequence = Value; Response.Status = FrameStatus::Success;
                PublishedRequest = true;
            }
            Sleep(10);
        }
        if (!Passed && WaitForSingleObject(Child.hProcess, 0) != WAIT_OBJECT_0) { TerminateProcess(Child.hProcess, 1); WaitForSingleObject(Child.hProcess, 2000); }
        CloseHandle(Child.hProcess);
        Check(Passed, "Cross-process GPU receiver failed");
        if (TestRequest) Check(RequestCount == 6, "Duplicate outstanding requests should cause only one producer render");
        while (Producer.Fence()->GetCompletedValue() < Last) Sleep(1);
        // Exercise the dangerous lease transition directly: a reader can depart
        // with a shared slot pinned. No old resource may be recycled afterwards.
        HANDLE Guard = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, 0, GpuGuardName);
        HANDLE Memory = OpenFileMappingW(FILE_MAP_ALL_ACCESS, 0, GpuMappingName);
        Check(Guard && Memory, "GPU protocol test access");
        auto* Block = static_cast<GpuBlock*>(MapViewOfFile(Memory, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GpuBlock)));
        Check(Block != nullptr, "GPU protocol mapping");
        Check(WaitForSingleObject(Guard, 1000) == WAIT_OBJECT_0, "GPU protocol guard");
        for (auto& S : Block->Slots) { S.State = GpuSlotState::Reading; S.ReaderPid = 123; }
        Block->ReceiverPid = 123; Block->ReceiverHeartbeatMs = GetTickCount64(); ReleaseMutex(Guard);
        Check(Producer.Reserve(Slot, Value, Connected) == Result::Busy, "Never overwrite live reader slots");
        Check(WaitForSingleObject(Guard, 1000) == WAIT_OBJECT_0, "GPU protocol guard");
        Block->ReceiverHeartbeatMs = GetTickCount64() - LeaseTimeoutMs - 1; ReleaseMutex(Guard);
        Check(Producer.Reserve(Slot, Value, Connected) == Result::Stale, "Expired pinned reader requires new resources");
        Check(WaitForSingleObject(Guard, 1000) == WAIT_OBJECT_0, "GPU protocol guard");
        Block->ReceiverPid = 456; Block->ReceiverHeartbeatMs = GetTickCount64(); ReleaseMutex(Guard);
        Check(Producer.Reserve(Slot, Value, Connected) == Result::Stale, "New receiver cannot inherit departed reader slots");
        const auto OldSession = Producer.Session();
        Producer.Close(); Check(Producer.Open(Device.Get()) == Result::Ok, "GPU producer generation restart");
        Check(Producer.Session() != OldSession, "Restart changes resource session");
        UnmapViewOfFile(Block); CloseHandle(Memory); CloseHandle(Guard);
        Producer.Close();
        GpuConsumer Absent; Check(Absent.Open() != Result::Ok, "Closed producer should disconnect");
        std::puts("PASS: DX12 cross-process pixels/camera pairing, pitch, alpha, ownership, pinned-reader protection, restart, disconnect.");
        if (TestClient) std::puts("PASS: Adobe worker thread, immutable snapshots, multiple subscribers, per-instance freeze/resume, disconnect.");
        if (TestRequest) std::puts("PASS: serialized exact DX12 request frames, rational/id/session matching, outstanding deduplication, random access, no latest fallback, immutable PreRender snapshots.");
        return 0;
    }
    catch (const std::exception& E) { std::fprintf(stderr, "FAIL: %s\n", E.what()); return 1; }
}
