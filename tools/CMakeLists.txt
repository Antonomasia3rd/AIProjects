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
add_portable_test(DiscordRPCSourceCheck DiscordRPC/tools/DiscordRPCSourceCheck.cpp DiscordRPC)
add_portable_test(ADBControllerSourceCheck legacy/ADBController/tools/ADBControllerSourceCheck.cpp legacy/ADBController)
add_portable_test(CharmTraySourceCheck legacy/CharmTray/tools/CharmTraySourceCheck.cpp legacy/CharmTray)
add_portable_test(NowPlayingTileSourceCheck legacy/NowPlayingTile/tools/NowPlayingTileSourceCheck.cpp legacy/NowPlayingTile)
add_portable_test(RealTimeNotesDeskbandSourceCheck legacy/RealTimeNotesDeskband/tools/RealTimeNotesDeskbandSourceCheck.cpp legacy/RealTimeNotesDeskband)
add_portable_test(SecureDesktopLauncherSourceCheck legacy/SecureDesktopLauncher/tools/SecureDesktopLauncherSourceCheck.cpp legacy/SecureDesktopLauncher)
add_portable_test(TileTextLayoutTests DesktopStub/tools/TileTextLayoutTests.cpp .)
add_portable_test(TileTextTemplateTests DesktopStub/tools/TileTextTemplateTests.cpp .)
add_portable_test(ContentEngineTests tools/ContentEngineTests.cpp .)
add_portable_test(ContentSourceHostTests tools/ContentSourceHostTests.cpp .)
add_portable_test(AsusEngineTests tools/AsusEngineTests.cpp .)
add_portable_test(CapsBlinkEngineTests tools/CapsBlinkEngineTests.cpp .)
target_sources(AsusEngineTests PRIVATE "${REPOSITORY_ROOT}/dependencies/asusblink/service.cpp")
target_compile_definitions(AsusEngineTests PRIVATE AIP_ASUS_NO_NATIVE_BACKEND)
find_package(Threads REQUIRED)
target_link_libraries(ContentSourceHostTests PRIVATE Threads::Threads)
target_link_libraries(AsusEngineTests PRIVATE Threads::Threads)
target_link_libraries(CapsBlinkEngineTests PRIVATE Threads::Threads)

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
if(POWERSHELL_EXECUTABLE)
    add_test(NAME TileTextLayoutSpecification
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -File "${CMAKE_CURRENT_LIST_DIR}/TestTileTextLayout.ps1")
    set_tests_properties(TileTextLayoutSpecification PROPERTIES TIMEOUT 90)
endif()
