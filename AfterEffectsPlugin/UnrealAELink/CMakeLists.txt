set(AE_EXAMPLES "${AE_SDK_ROOT}/Examples")
if(NOT EXISTS "${AE_EXAMPLES}/Headers/AE_Effect.h")
    message(FATAL_ERROR "AE_SDK_ROOT must contain Examples/Headers/AE_Effect.h from the official Adobe Windows SDK")
endif()
enable_language(RC)
find_program(LINK_PWSH NAMES pwsh REQUIRED)
set(PIPL_RC "${CMAKE_CURRENT_BINARY_DIR}/UnrealAELinkPiPL.rc")
add_custom_command(OUTPUT "${PIPL_RC}"
    COMMAND "${LINK_PWSH}" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "${PROJECT_SOURCE_DIR}/Tools/GeneratePiPL.ps1"
        -Compiler "${CMAKE_CXX_COMPILER}" -SDKRoot "${AE_SDK_ROOT}"
        -Source "${CMAKE_CURRENT_SOURCE_DIR}/UnrealAELinkPiPL.r" -Output "${PIPL_RC}"
    DEPENDS UnrealAELinkPiPL.r "${PROJECT_SOURCE_DIR}/Tools/GeneratePiPL.ps1" VERBATIM)
add_library(UnrealAELinkEffect MODULE Effect.cpp "${PIPL_RC}")
target_include_directories(UnrealAELinkEffect PRIVATE "${AE_EXAMPLES}/Headers" "${AE_EXAMPLES}/Headers/SP" "${AE_EXAMPLES}/Util")
target_compile_definitions(UnrealAELinkEffect PRIVATE MSWindows WIN32 _WINDOWS)
target_link_libraries(UnrealAELinkEffect PRIVATE UnrealAELinkFrameClient)
set_target_properties(UnrealAELinkEffect PROPERTIES PREFIX "" SUFFIX ".aex" OUTPUT_NAME "UnrealAELink"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${PROJECT_SOURCE_DIR}/AfterEffectsPlugin/UnrealAELink/Binaries/Win64"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PROJECT_SOURCE_DIR}/AfterEffectsPlugin/UnrealAELink/Binaries/Win64")
if(MSVC)
    target_compile_options(UnrealAELinkEffect PRIVATE /W4 /permissive-)
endif()
add_executable(AdobeRenderTests "${PROJECT_SOURCE_DIR}/Tests/AdobeRenderTests.cpp")
target_include_directories(AdobeRenderTests PRIVATE "${AE_EXAMPLES}/Headers" "${AE_EXAMPLES}/Headers/SP" "${AE_EXAMPLES}/Util")
target_compile_definitions(AdobeRenderTests PRIVATE MSWindows WIN32 _WINDOWS)
target_link_libraries(AdobeRenderTests PRIVATE UnrealAELinkFrameClient)
if(MSVC)
    target_compile_options(AdobeRenderTests PRIVATE /W4 /WX /permissive-)
endif()
