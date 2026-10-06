#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define PF_DEEP_COLOR_AWARE 1
#include <Windows.h>
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_PluginData.h"
#include "FrameClient.h"
#include <algorithm>
#include <cstdio>
#include <new>
#include <fstream>
#include <filesystem>

namespace
{
enum { Input, Connect, Live, Source, Status, FrameNumber, NumParams };
constexpr PF_OutFlags Flags = PF_OutFlag_NON_PARAM_VARY | PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_SEND_UPDATE_PARAMS_UI;
constexpr PF_OutFlags2 Flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_REVEALS_ZERO_ALPHA;
std::unique_ptr<UnrealAELink::FrameClient> Client;
struct Sequence { std::uint64_t Id; };
struct PreFrame { std::shared_ptr<const UnrealAELink::BeautyFrame> Image; };
void Report(const char* Text)
{
    wchar_t Directory[1024]{};
    const auto Count = GetEnvironmentVariableW(L"UNREAL_AE_LINK_DIAGNOSTIC_DIR", Directory, 1024);
    if (!Count || Count >= 1024) return;
    std::ofstream Log(std::filesystem::path(Directory) / "ae-native.log", std::ios::app);
    Log << Text << '\n';
}
std::uint64_t InstanceId(PF_InData* In)
{
    if (!In->sequence_data) return 0;
    auto* S = static_cast<Sequence*>((*In->utils->host_lock_handle)(In->sequence_data));
    const auto Id = S ? S->Id : 0;
    (*In->utils->host_unlock_handle)(In->sequence_data); return Id;
}
PF_Err SequenceSetup(PF_InData* in_data, PF_OutData* out_data, bool Resetup)
{
    PF_Handle H = Resetup ? in_data->sequence_data : nullptr;
    if (!H) H = PF_NEW_HANDLE(sizeof(Sequence));
    if (!H) return PF_Err_OUT_OF_MEMORY;
    auto* S = static_cast<Sequence*>(PF_LOCK_HANDLE(H));
    if (!S) return PF_Err_OUT_OF_MEMORY;
    S->Id = Client->Register(reinterpret_cast<std::uintptr_t>(in_data->effect_ref));
    PF_UNLOCK_HANDLE(H); out_data->sequence_data = H; Report(Resetup ? "SEQUENCE_RESETUP" : "SEQUENCE_SETUP"); return PF_Err_NONE;
}
std::shared_ptr<const UnrealAELink::BeautyFrame> GetImage(PF_InData* In, bool Connected, bool IsLive)
{ return Client ? Client->Snapshot(InstanceId(In), Connected, IsLive) : nullptr; }

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_ParamDef def{};
    const auto ParameterFlags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_CHECKBOXX("Connect", 0, ParameterFlags, Connect);
    PF_ADD_CHECKBOXX("Live", 1, ParameterFlags, Live);
    AEFX_CLR_STRUCT(def); def.ui_flags = PF_PUI_DISABLED;
    PF_ADD_POPUP("Source", 1, 1, "Beauty", Source);
    PF_ADD_BUTTON("Disconnected", "Refresh", 0, ParameterFlags, Status);
    PF_ADD_BUTTON("Frame: none", "Details", 0, ParameterFlags, FrameNumber);
    out_data->num_params = NumParams; return PF_Err_NONE;
}

template<class Pixel, class Channel>
PF_Err Paint(PF_InData* In, PF_EffectWorld* Out, const UnrealAELink::BeautyFrame* Frame, Channel Maximum)
{
    const int FullWidth = std::max<A_long>(1, In->width * In->downsample_x.num / std::max<A_long>(1, In->downsample_x.den));
    const int FullHeight = std::max<A_long>(1, In->height * In->downsample_y.num / std::max<A_long>(1, In->downsample_y.den));
    const double Par = double(In->pixel_aspect_ratio.num) / std::max<A_long>(1, In->pixel_aspect_ratio.den);
    const double Ratio = double(UnrealAELink::BeautyWidth) / UnrealAELink::BeautyHeight / std::max(0.001, Par);
    const double FitWidth = std::min(double(FullWidth), FullHeight * Ratio);
    const double FitHeight = FitWidth / Ratio;
    const double Left = (FullWidth - FitWidth) * 0.5, Top = (FullHeight - FitHeight) * 0.5;
    for (A_long Y = 0; Y < Out->height; ++Y)
    {
        auto* Row = reinterpret_cast<Pixel*>(reinterpret_cast<unsigned char*>(Out->data) + std::ptrdiff_t(Y) * Out->rowbytes);
        for (A_long X = 0; X < Out->width; ++X)
        {
            Pixel P{}; P.alpha = Maximum;
            const double U = (X + Out->origin_x + 0.5 - Left) / FitWidth;
            const double V = (Y + Out->origin_y + 0.5 - Top) / FitHeight;
            if (Frame && U >= 0 && U < 1 && V >= 0 && V < 1)
            {
                const auto SX = std::min(Frame->Width - 1, static_cast<std::uint32_t>(U * Frame->Width));
                const auto SY = std::min(Frame->Height - 1, static_cast<std::uint32_t>(V * Frame->Height));
                const auto* RGBA = Frame->Rgba.data() + (std::size_t(SY) * Frame->Width + SX) * 4;
                P.red = static_cast<Channel>(double(RGBA[0]) * Maximum / 255.0);
                P.green = static_cast<Channel>(double(RGBA[1]) * Maximum / 255.0);
                P.blue = static_cast<Channel>(double(RGBA[2]) * Maximum / 255.0);
            }
            Row[X] = P;
        }
        if (Y % 32 == 0) { const auto Error = (*In->inter.abort)(In->effect_ref); if (Error) return Error; }
    }
    return PF_Err_NONE;
}

PF_Err RenderWorld(PF_InData* In, PF_EffectWorld* Out, const UnrealAELink::BeautyFrame* Image)
{
    if (!Out || !Out->data) return PF_Err_BAD_CALLBACK_PARAM;
    const PF_WorldSuite2* Suite = nullptr;
    auto E = In->pica_basicP->AcquireSuite(kPFWorldSuite, kPFWorldSuiteVersion2, reinterpret_cast<const void**>(&Suite));
    if (E || !Suite) return PF_Err_BAD_CALLBACK_PARAM;
    PF_PixelFormat Format = PF_PixelFormat_INVALID;
    const auto Error = Suite->PF_GetPixelFormat(Out, &Format);
    In->pica_basicP->ReleaseSuite(kPFWorldSuite, kPFWorldSuiteVersion2);
    if (Error) return Error;
    char Message[256]{};
    std::snprintf(Message, sizeof(Message), "RENDER format=%d sequence=%llu session=%llu hash=%llu colored=%llu",
        int(Format), Image ? static_cast<unsigned long long>(Image->Sequence) : 0,
        Image ? static_cast<unsigned long long>(Image->Session) : 0,
        Image ? static_cast<unsigned long long>(Image->Checksum) : 0,
        Image ? static_cast<unsigned long long>(Image->ColoredPixels) : 0);
    Report(Message);
    if (Format == PF_PixelFormat_ARGB32) return Paint<PF_Pixel8, A_u_char>(In, Out, Image, PF_MAX_CHAN8);
    else if (Format == PF_PixelFormat_ARGB64) return Paint<PF_Pixel16, A_u_short>(In, Out, Image, PF_MAX_CHAN16);
    else if (Format == PF_PixelFormat_ARGB128) return Paint<PF_PixelFloat, PF_FpShort>(In, Out, Image, 1.0f);
    else return PF_Err_BAD_CALLBACK_PARAM;
}

PF_Err PreRender(PF_InData* in_data, PF_PreRenderExtra* Extra)
{
    PF_ParamDef C{}, L{};
    auto Error = PF_CHECKOUT_PARAM(in_data, Connect, in_data->current_time, in_data->time_step, in_data->time_scale, &C);
    if (Error) return Error;
    Error = PF_CHECKOUT_PARAM(in_data, Live, in_data->current_time, in_data->time_step, in_data->time_scale, &L);
    if (Error) return Error;
    auto Data = std::make_unique<PreFrame>(); Data->Image = GetImage(in_data, C.u.bd.value != 0, L.u.bd.value != 0);
    const std::uint64_t Key[] = {Data->Image ? Data->Image->Session : 0, Data->Image ? Data->Image->Sequence : 0};
    if (Extra->cb->GuidMixInPtr)
    {
        Error = Extra->cb->GuidMixInPtr(in_data->effect_ref, sizeof(Key), Key);
        if (Error) return Error;
    }
    Extra->output->max_result_rect = {0, 0, in_data->width, in_data->height};
    auto Rect = Extra->input->output_request.rect;
    Rect.left = std::max<A_long>(0, Rect.left); Rect.top = std::max<A_long>(0, Rect.top);
    Rect.right = std::min(in_data->width, Rect.right); Rect.bottom = std::min(in_data->height, Rect.bottom);
    Extra->output->result_rect = Rect; Extra->output->solid = 1;
    Extra->output->pre_render_data = Data.release();
    Extra->output->delete_pre_render_data_func = [](void* P) { delete static_cast<PreFrame*>(P); };
    return PF_Err_NONE;
}

PF_Err UpdateUI(PF_InData* In, PF_ParamDef* Params[])
{
    auto Image = GetImage(In, Params[Connect]->u.bd.value != 0, Params[Live]->u.bd.value != 0);
    const PF_ParamUtilsSuite3* Suite = nullptr;
    if (In->pica_basicP->AcquireSuite(kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3, reinterpret_cast<const void**>(&Suite)) || !Suite)
        return PF_Err_BAD_CALLBACK_PARAM;
    PF_ParamDef S = *Params[Status], F = *Params[FrameNumber];
    const char* Label = !Params[Connect]->u.bd.value ? "Disconnected" : Image ?
        (Params[Live]->u.bd.value ? "Connected / Beauty" : "Frozen / Beauty") : "Waiting for Unreal";
    std::snprintf(S.PF_DEF_NAME, sizeof(S.PF_DEF_NAME), "%s", Label);
    if (Image) std::snprintf(F.PF_DEF_NAME, sizeof(F.PF_DEF_NAME), "Frame: %llu", static_cast<unsigned long long>(Image->Sequence));
    else std::snprintf(F.PF_DEF_NAME, sizeof(F.PF_DEF_NAME), "Frame: none");
    auto Error = Suite->PF_UpdateParamUI(In->effect_ref, Status, &S);
    if (!Error) Error = Suite->PF_UpdateParamUI(In->effect_ref, FrameNumber, &F);
    In->pica_basicP->ReleaseSuite(kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3); return Error;
}
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd Cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* Params[], PF_LayerDef* Output, void* Extra)
{
    try
    {
        switch (Cmd)
        {
        case PF_Cmd_ABOUT:
            std::snprintf(out_data->return_msg, sizeof(out_data->return_msg), "UnrealAELink 0.1\rLive DX12 Beauty from Unreal. 1280x720 RGBA8. No timeline or camera control."); break;
        case PF_Cmd_GLOBAL_SETUP:
            out_data->my_version = PF_VERSION(0,1,0,PF_Stage_DEVELOP,1);
            out_data->out_flags = Flags; out_data->out_flags2 = Flags2;
            Client = std::make_unique<UnrealAELink::FrameClient>(); Report("GLOBAL_SETUP"); break;
        case PF_Cmd_GLOBAL_SETDOWN: Client.reset(); Report("GLOBAL_SETDOWN"); break;
        case PF_Cmd_PARAMS_SETUP: return ParamsSetup(in_data, out_data);
        case PF_Cmd_SEQUENCE_SETUP: return SequenceSetup(in_data, out_data, false);
        case PF_Cmd_SEQUENCE_RESETUP: return SequenceSetup(in_data, out_data, true);
        case PF_Cmd_SEQUENCE_SETDOWN:
            Report("SEQUENCE_SETDOWN");
            if (Client) Client->Remove(InstanceId(in_data));
            if (in_data->sequence_data) PF_DISPOSE_HANDLE(in_data->sequence_data);
            out_data->sequence_data = nullptr; break;
        case PF_Cmd_RENDER:
        { auto Image = GetImage(in_data, Params[Connect]->u.bd.value != 0, Params[Live]->u.bd.value != 0); return RenderWorld(in_data, Output, Image.get()); }
        case PF_Cmd_SMART_PRE_RENDER: return PreRender(in_data, static_cast<PF_PreRenderExtra*>(Extra));
        case PF_Cmd_SMART_RENDER:
        {
            auto* S = static_cast<PF_SmartRenderExtra*>(Extra); PF_EffectWorld* Out = nullptr;
            const auto Error = S->cb->checkout_output(in_data->effect_ref, &Out); if (Error) return Error;
            auto* Data = static_cast<PreFrame*>(S->input->pre_render_data);
            return RenderWorld(in_data, Out, Data ? Data->Image.get() : nullptr);
        }
        case PF_Cmd_USER_CHANGED_PARAM:
            out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;
            return UpdateUI(in_data, Params);
        case PF_Cmd_UPDATE_PARAMS_UI: return UpdateUI(in_data, Params);
        default: break;
        }
        return PF_Err_NONE;
    }
    catch (const std::bad_alloc&) { return PF_Err_OUT_OF_MEMORY; }
    catch (...) { return PF_Err_INTERNAL_STRUCT_DAMAGED; }
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr In, PF_PluginDataCB2 Callback,
    SPBasicSuite*, const char*, const char*)
{
    PF_Err result = PF_Err_NONE;
    PF_REGISTER_EFFECT_EXT2(In, Callback, "UnrealAELink", "UnrealAELink.Beauty", "UnrealAELink", AE_RESERVED_INFO,
        "EffectMain", "https://developer.adobe.com/after-effects/");
    Report(result == PF_Err_NONE ? "REGISTER_OK" : "REGISTER_ERROR");
    return result;
}
