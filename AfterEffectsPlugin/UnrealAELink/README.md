# Reserved for a later milestone

No After Effects plugin code is implemented. Milestone zero stops at metadata.

No Adobe SDK was found in the inspected installation, Documents, Downloads, or
project locations on 2026-10-05. After Effects itself was also not found in the
standard Adobe installation folder or installed-program registry entries.
This is a scoped discovery result, not proof that no custom installation exists.

Before the AE milestone, supply the official Windows After Effects SDK root
(for example via `AE_SDK_ROOT`). It needs the effect headers `AE_Effect.h`,
`AE_EffectCB.h`, `AE_EffectSuites.h`, SDK utility/sample sources, and Windows
PiPL resource tooling/sample build configuration to produce a host-loadable `.aex`.
The Adobe application alone does not provide these development files.

Do not copy anything into the Adobe installation for milestone zero.
