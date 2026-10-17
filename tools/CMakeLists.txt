cmake_minimum_required(VERSION 3.16)
project(AIProjectsPortableTests LANGUAGES CXX)

include(CTest)
get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# These executables inspect source contracts or execute platform-neutral logic.
# The products themselves still require Windows and are built by their .cmd files.
function(add_portable_test name source working_directory)
    add_executable(${name} "${REPOSITORY_ROOT}/${source}")
    target_compile_features(${name} PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /utf-8 /EHsc)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        WORKING_DIRECTORY "${REPOSITORY_ROOT}/${working_directory}"
        TIMEOUT 90)
endfunction()

add_portable_test(SharedBaselineSourceCheck tools/SharedBaselineSourceCheck.cpp .)
add_portable_test(DesktopStubSourceCheck DesktopStub/tools/DesktopStubSourceCheck.cpp DesktopStub)
add_portable_test(ADBControllerSourceCheck legacy/ADBController/tools/ADBControllerSourceCheck.cpp legacy/ADBController)
add_portable_test(CharmTraySourceCheck legacy/CharmTray/tools/CharmTraySourceCheck.cpp legacy/CharmTray)
add_portable_test(SecureDesktopLauncherSourceCheck legacy/SecureDesktopLauncher/tools/SecureDesktopLauncherSourceCheck.cpp legacy/SecureDesktopLauncher)
add_portable_test(SecureDesktopIdentityTests tools/SecureDesktopIdentityTests.cpp .)
add_portable_test(TileTextLayoutTests DesktopStub/tools/TileTextLayoutTests.cpp .)
add_portable_test(AdaptiveTileTextLayoutTests DesktopStub/tools/AdaptiveTileTextLayoutTests.cpp .)
add_portable_test(TileTextTemplateTests DesktopStub/tools/TileTextTemplateTests.cpp .)
add_portable_test(ContentEngineTests tools/ContentEngineTests.cpp .)
add_portable_test(ContentSourceHostTests tools/ContentSourceHostTests.cpp .)
add_portable_test(ContentPresetTests tools/ContentPresetTests.cpp .)
add_portable_test(ContentQueueTests tools/ContentQueueTests.cpp .)
add_portable_test(SecondaryTileTests tools/SecondaryTileTests.cpp .)
add_portable_test(TileVisualAssetsTests tools/TileVisualAssetsTests.cpp .)
add_portable_test(NotesTileRowsTests tools/NotesTileRowsTests.cpp .)
add_portable_test(MediaTileLayoutTests tools/MediaTileLayoutTests.cpp .)
add_portable_test(GalleryEngineTests tools/GalleryEngineTests.cpp .)
add_portable_test(GalleryInventoryTests tools/GalleryInventoryTests.cpp .)
add_portable_test(GalleryRuntimeTests tools/GalleryRuntimeTests.cpp .)
add_portable_test(GalleryTransactionTests tools/GalleryTransactionTests.cpp .)
add_portable_test(TileNotificationPayloadTests tools/TileNotificationPayloadTests.cpp .)
add_portable_test(PngCrc32Tests tools/PngCrc32Tests.cpp .)
add_portable_test(CaptureAttemptTests tools/CaptureAttemptTests.cpp .)
add_portable_test(AsusEngineTests tools/AsusEngineTests.cpp .)
add_portable_test(CapsBlinkEngineTests tools/CapsBlinkEngineTests.cpp .)
target_sources(AsusEngineTests PRIVATE "${REPOSITORY_ROOT}/dependencies/asusblink/service.cpp")
target_compile_definitions(AsusEngineTests PRIVATE AIP_ASUS_NO_NATIVE_BACKEND)
find_package(Threads REQUIRED)
target_link_libraries(SecondaryTileTests PRIVATE Threads::Threads)
target_link_libraries(GalleryRuntimeTests PRIVATE Threads::Threads)
target_link_libraries(ContentSourceHostTests PRIVATE Threads::Threads)
target_link_libraries(CaptureAttemptTests PRIVATE Threads::Threads)
target_link_libraries(AsusEngineTests PRIVATE Threads::Threads)
target_link_libraries(CapsBlinkEngineTests PRIVATE Threads::Threads)

if(WIN32)
    add_portable_test(GalleryInventoryNativeTests tools/GalleryInventoryNativeTests.cpp .)
    target_compile_definitions(GalleryInventoryNativeTests PRIVATE UNICODE _UNICODE)
    target_link_libraries(GalleryInventoryNativeTests PRIVATE windowscodecs ole32 advapi32 shell32)
    add_portable_test(TileVisualAssetsRenderTests tools/TileVisualAssetsRenderTests.cpp .)
    target_compile_definitions(TileVisualAssetsRenderTests PRIVATE UNICODE _UNICODE)
    target_link_libraries(TileVisualAssetsRenderTests PRIVATE gdiplus gdi32 user32 shell32 shlwapi advapi32)
    # Data/profile fixtures never start the product entry point or real sources.
    # This fixture creates only its own sleeping/synthetic child executables;
    # no actual window/screen capture or product entry point is used.
    add_portable_test(BoundedCaptureProcessTests tools/BoundedCaptureProcessTests.cpp .)
    target_compile_definitions(BoundedCaptureProcessTests PRIVATE UNICODE _UNICODE)
    target_link_libraries(BoundedCaptureProcessTests PRIVATE user32 shell32 shlwapi advapi32 ole32 uuid crypt32)
    if(MINGW)
        target_link_options(BoundedCaptureProcessTests PRIVATE -municode)
    endif()
    add_portable_test(HardwareProfileImportTests tools/HardwareProfileImportTests.cpp .)
    add_portable_test(NowPlayingProfileImportTests tools/NowPlayingProfileImportTests.cpp .)
    add_portable_test(NotesSourceTests tools/NotesSourceTests.cpp .)
    add_portable_test(SmtcSourceTests tools/SmtcSourceTests.cpp .)
    add_portable_test(PackagedStartupTests tools/PackagedStartupTests.cpp .)
    add_portable_test(WinrtFactoryLifetimeTests tools/WinrtFactoryLifetimeTests.cpp .)
    add_portable_test(DiscordProfileEditTests tools/DiscordProfileEditTests.cpp .)
    target_sources(DiscordProfileEditTests PRIVATE
        "${REPOSITORY_ROOT}/dependencies/DiscordRPC/service.cpp"
        "${REPOSITORY_ROOT}/dependencies/DiscordRPC/profile_edit.cpp")
    target_link_libraries(HardwareProfileImportTests PRIVATE user32 shell32 shlwapi advapi32)
    target_link_libraries(NowPlayingProfileImportTests PRIVATE user32 shell32 shlwapi advapi32)
    target_link_libraries(NotesSourceTests PRIVATE winhttp advapi32)
    target_link_libraries(SmtcSourceTests PRIVATE windowscodecs advapi32 windowsapp runtimeobject ole32)
    target_link_libraries(PackagedStartupTests PRIVATE user32 shell32 ole32 uuid windowsapp runtimeobject)
    target_link_libraries(WinrtFactoryLifetimeTests PRIVATE windowsapp runtimeobject ole32)
    target_link_libraries(DiscordProfileEditTests PRIVATE user32 shell32 shlwapi advapi32 ole32 uuid winhttp crypt32)
    foreach(fixture HardwareProfileImportTests NowPlayingProfileImportTests NotesSourceTests SmtcSourceTests DiscordProfileEditTests)
        target_compile_definitions(${fixture} PRIVATE UNICODE _UNICODE)
    endforeach()

    add_library(ContentIntegrationDiscord STATIC
        "${REPOSITORY_ROOT}/dependencies/DiscordRPC/service.cpp"
        "${REPOSITORY_ROOT}/dependencies/DiscordRPC/profile_edit.cpp")
    add_library(ContentIntegrationAsus STATIC "${REPOSITORY_ROOT}/dependencies/asusblink/service.cpp")
    target_compile_features(ContentIntegrationDiscord PRIVATE cxx_std_17)
    target_compile_features(ContentIntegrationAsus PRIVATE cxx_std_17)
    target_compile_definitions(ContentIntegrationDiscord PRIVATE UNICODE _UNICODE)
    target_compile_definitions(ContentIntegrationAsus PRIVATE AIP_ASUS_NO_NATIVE_BACKEND)
    if(MSVC)
        target_compile_options(ContentIntegrationDiscord PRIVATE /W4 /utf-8 /EHsc)
        target_compile_options(ContentIntegrationAsus PRIVATE /W4 /utf-8 /EHsc)
    endif()
    add_portable_test(ContentRuntimeTests DesktopStub/tools/ContentRuntimeTests.cpp .)
    add_portable_test(ContentRevisionHostTests DesktopStub/tools/ContentRevisionHostTests.cpp .)
    add_portable_test(ContentRuntimeNoHardwareTests DesktopStub/tools/ContentRuntimeNoHardwareTests.cpp .)
    add_portable_test(TransparentBackgroundTests DesktopStub/tools/TransparentBackgroundTests.cpp .)
    add_portable_test(AdaptiveTileTextRenderTests DesktopStub/tools/AdaptiveTileTextRenderTests.cpp .)
    foreach(fixture ContentRuntimeTests ContentRevisionHostTests ContentRuntimeNoHardwareTests TransparentBackgroundTests AdaptiveTileTextRenderTests)
        target_compile_definitions(${fixture} PRIVATE UNICODE _UNICODE)
        target_link_libraries(${fixture} PRIVATE ContentIntegrationDiscord
            gdiplus windowscodecs gdi32 user32 shlwapi shell32 ole32 uuid comdlg32
            advapi32 winhttp crypt32 windowsapp runtimeobject)
    endforeach()
    target_link_libraries(ContentRuntimeTests PRIVATE ContentIntegrationAsus)
    target_link_libraries(ContentRevisionHostTests PRIVATE ContentIntegrationAsus)
    target_link_libraries(TransparentBackgroundTests PRIVATE ContentIntegrationAsus)
    target_link_libraries(AdaptiveTileTextRenderTests PRIVATE ContentIntegrationAsus)
endif()

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
if(POWERSHELL_EXECUTABLE)
    add_test(NAME TileTextLayoutSpecification
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File "${CMAKE_CURRENT_LIST_DIR}/TestTileTextLayout.ps1")
    set_tests_properties(TileTextLayoutSpecification PROPERTIES TIMEOUT 90)
    add_test(NAME ChromeProfileCounterSourceCheck
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${REPOSITORY_ROOT}/legacy/ChromeProfileCounter/tools/ChromeProfileCounterSourceCheck.ps1"
            -RepositoryRoot "${REPOSITORY_ROOT}")
    set_tests_properties(ChromeProfileCounterSourceCheck PROPERTIES
        WORKING_DIRECTORY "${REPOSITORY_ROOT}"
        TIMEOUT 90)
    add_test(NAME ChromeProfileCounterTests
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File
            "${REPOSITORY_ROOT}/legacy/ChromeProfileCounter/tools/ChromeProfileCounterTests.ps1"
            -RepositoryRoot "${REPOSITORY_ROOT}")
    set_tests_properties(ChromeProfileCounterTests PROPERTIES TIMEOUT 30)
    add_test(NAME YouTubeMusicMigrateSourceCheck
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -NonInteractive -File
            "${REPOSITORY_ROOT}/legacy/YouTubeMusicMigrate/tools/YouTubeMusicMigrateSourceCheck.ps1"
            -RepositoryRoot "${REPOSITORY_ROOT}")
    set_tests_properties(YouTubeMusicMigrateSourceCheck PROPERTIES TIMEOUT 30)
    add_test(NAME YouTubeMusicMigrateWrapperTests
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -NonInteractive -File
            "${REPOSITORY_ROOT}/legacy/YouTubeMusicMigrate/tools/YouTubeMusicMigrateWrapperTests.ps1"
            -RepositoryRoot "${REPOSITORY_ROOT}")
    set_tests_properties(YouTubeMusicMigrateWrapperTests PROPERTIES TIMEOUT 30)
endif()
