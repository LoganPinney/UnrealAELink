// Exercise the actual renderer against official Adobe world structures. This
// checks buffer math and conversion without pretending to be an Adobe host run.
#include "../AfterEffectsPlugin/UnrealAELink/Effect.cpp"
#include <cmath>
#include <vector>
#include <stdexcept>

namespace
{
PF_PixelFormat TestFormat = PF_PixelFormat_ARGB32;
int Acquires = 0, Releases = 0;
bool Cancel = false;
PF_WorldSuite2 WorldSuite{};
PF_Err GetFormat(const PF_EffectWorld*, PF_PixelFormat* Format) { *Format = TestFormat; return PF_Err_NONE; }
SPErr SPAPI Acquire(const char* Name, int32 Version, const void** Suite)
{
    if (std::strcmp(Name, kPFWorldSuite) || Version != kPFWorldSuiteVersion2) return -1;
    *Suite = &WorldSuite; ++Acquires; return 0;
}
SPErr SPAPI Release(const char*, int32) { ++Releases; return 0; }
PF_Err Abort(PF_ProgPtr) { return Cancel ? PF_Err_INTERNAL_STRUCT_DAMAGED : PF_Err_NONE; }
void Require(bool Ok, const char* Message) { if (!Ok) throw std::runtime_error(Message); }

template<class Pixel>
void CheckFormat(PF_PixelFormat Format, double Maximum, const UnrealAELink::BeautyFrame& Image, bool Tile, bool Half)
{
    TestFormat = Format;
    SPBasicSuite Basic{}; Basic.AcquireSuite = Acquire; Basic.ReleaseSuite = Release;
    PF_InData In{}; In.width = In.height = Half ? 8 : 4;
    In.downsample_x = In.downsample_y = {1, Half ? 2u : 1u};
    In.pixel_aspect_ratio = {1,1}; In.pica_basicP = &Basic; In.inter.abort = Abort;
    PF_EffectWorld World{}; World.width = Tile ? 2 : 4; World.height = Tile ? 2 : 4;
    World.origin_x = Tile ? 2 : 0; World.origin_y = Tile ? 1 : 0;
    World.rowbytes = World.width * sizeof(Pixel) + 32;
    std::vector<unsigned char> Bytes(std::size_t(World.rowbytes) * World.height, 0xCD);
    World.data = reinterpret_cast<PF_PixelPtr>(Bytes.data());
    Require(RenderWorld(&In, &World, &Image) == PF_Err_NONE, "Actual renderer returned error");
    for (int Y=0; Y<World.height; ++Y)
    {
        const auto* Row = reinterpret_cast<const Pixel*>(Bytes.data() + std::size_t(Y) * World.rowbytes);
        for (int X=0; X<World.width; ++X)
        {
            const auto& P = Row[X]; const int GX = X + World.origin_x, GY = Y + World.origin_y;
            int R=0,G=0,B=0;
            if (GY==1) { if (GX<2) {R=255;G=32;B=64;} else {R=16;G=255;B=80;} }
            if (GY==2) { if (GX<2) {R=17;G=80;B=127;} else {R=32;G=48;B=255;} }
            const double Tolerance = Maximum == 1 ? 0.000001 : 1.0;
            Require(std::abs(double(P.alpha) - Maximum) < 0.001, "Opaque alpha conversion");
            Require(std::abs(double(P.red) - R * Maximum / 255.0) <= Tolerance &&
                std::abs(double(P.green) - G * Maximum / 255.0) <= Tolerance &&
                std::abs(double(P.blue) - B * Maximum / 255.0) <= Tolerance, "ARGB channels / image orientation / tile placement");
        }
        for (int P = World.width * sizeof(Pixel); P < World.rowbytes; ++P)
            Require(Bytes[std::size_t(Y) * World.rowbytes + P] == 0xCD, "Row padding overwritten");
    }
    Cancel = true;
    Require(RenderWorld(&In, &World, &Image) == PF_Err_INTERNAL_STRUCT_DAMAGED, "Host cancellation must propagate"); Cancel = false;
    Require(RenderWorld(&In, &World, nullptr) == PF_Err_NONE, "Disconnected render error");
    const auto* Pixel0 = reinterpret_cast<const Pixel*>(Bytes.data());
    Require(Pixel0->red == 0 && Pixel0->green == 0 && Pixel0->blue == 0 && Pixel0->alpha == Maximum, "Disconnected output must be opaque black");
}
}
int main()
{
    try
    {
        WorldSuite.PF_GetPixelFormat = GetFormat;
        UnrealAELink::BeautyFrame Image; Image.Width=1280; Image.Height=720; Image.Rgba.resize(1280*720*4);
        for (unsigned Y=0;Y<Image.Height;++Y) for(unsigned X=0;X<Image.Width;++X)
        {
            auto* P=Image.Rgba.data()+(std::size_t(Y)*Image.Width+X)*4;
            if (Y<360) { if(X<640){P[0]=255;P[1]=32;P[2]=64;}else{P[0]=16;P[1]=255;P[2]=80;} }
            else {if(X<640){P[0]=17;P[1]=80;P[2]=127;}else{P[0]=32;P[1]=48;P[2]=255;}}
            P[3]=255;
        }
        for (const bool Tile : {false,true}) for (const bool Half : {false,true})
        {
            CheckFormat<PF_Pixel8>(PF_PixelFormat_ARGB32,255,Image,Tile,Half);
            CheckFormat<PF_Pixel16>(PF_PixelFormat_ARGB64,32768,Image,Tile,Half);
            CheckFormat<PF_PixelFloat>(PF_PixelFormat_ARGB128,1,Image,Tile,Half);
        }
        Require(Acquires == Releases, "Adobe suite references must balance");
        std::puts("PASS: actual native renderer with Adobe SDK worlds: 8/16/32 bpc, row stride, crop origins, downsampling, letterbox, opaque disconnect, cancellation.");
        return 0;
    }
    catch (const std::exception& E) { std::fprintf(stderr,"FAIL: %s\n",E.what()); return 1; }
}
