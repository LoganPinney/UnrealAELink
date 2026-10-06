#include "AEConfig.h"
#include "AE_EffectVers.h"
resource 'PiPL' (16000) {
    {
        Kind { AEEffect },
        Name { "UnrealAELink" },
        Category { "UnrealAELink" },
        CodeWin64X86 { "EffectMain" },
        AE_PiPL_Version { 2, 0 },
        AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
        AE_Effect_Version { 32769 },
        AE_Effect_Info_Flags { 0 },
        AE_Effect_Global_OutFlags { 0x06000004 },
        AE_Effect_Global_OutFlags_2 { 0x00201480 },
        AE_Effect_Match_Name { "UnrealAELink.Beauty" },
        AE_Reserved_Info { 0 },
        AE_Effect_Support_URL { "https://developer.adobe.com/after-effects/" }
    }
};
