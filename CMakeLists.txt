cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
option(LUDISSEY_UNIVERSAL "Compila per arm64 e x86_64" OFF)
if(LUDISSEY_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architetture" FORCE)
endif()
project(Ludissey VERSION 1.0.0 LANGUAGES CXX OBJCXX)
if(NOT APPLE)
    message(FATAL_ERROR "The current renderer and OS adapters require macOS.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(LUDISSEY_SANITIZE "" CACHE STRING "Sanitizer: address oppure thread")
set_property(CACHE LUDISSEY_SANITIZE PROPERTY STRINGS "" address thread)
set(LUDISSEY_APPS "${CMAKE_BINARY_DIR}/Apps" CACHE PATH "Destinazione di Ludissey.app e Ludissey Studio.app")
set(LUDISSEY_BUILD_NUMBER "7")
include(CTest)

add_executable(LudisseyGenerate Tools/Generate.cpp Engine/Core/Json.cpp Engine/Core/Text.cpp)
target_include_directories(LudisseyGenerate PRIVATE "${PROJECT_SOURCE_DIR}")
add_custom_target(LudisseyGenerated COMMAND LudisseyGenerate "${PROJECT_SOURCE_DIR}" WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)

add_library(LudisseyOptions INTERFACE)
target_include_directories(LudisseyOptions INTERFACE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/ThirdParty" "${PROJECT_SOURCE_DIR}/ThirdParty/MetalCpp")
target_compile_definitions(LudisseyOptions INTERFACE LUDISSEY_VERSION="${PROJECT_VERSION}")
target_compile_options(LudisseyOptions INTERFACE -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Werror=return-type -Werror=uninitialized "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
if(LUDISSEY_SANITIZE STREQUAL "address")
    target_compile_options(LudisseyOptions INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=undefined -g)
    target_link_options(LudisseyOptions INTERFACE -fsanitize=address,undefined)
elseif(LUDISSEY_SANITIZE STREQUAL "thread")
    target_compile_options(LudisseyOptions INTERFACE -fsanitize=thread -g)
    target_link_options(LudisseyOptions INTERFACE -fsanitize=thread)
elseif(NOT LUDISSEY_SANITIZE STREQUAL "")
    message(FATAL_ERROR "LUDISSEY_SANITIZE ammette address oppure thread")
endif()

add_library(LudisseyMacLibraries INTERFACE)
foreach(framework Foundation AppKit Metal QuartzCore CoreText CoreGraphics CoreVideo ImageIO AVFoundation UniformTypeIdentifiers ModelIO)
    find_library(LUDISSEY_${framework} ${framework} REQUIRED)
    target_link_libraries(LudisseyMacLibraries INTERFACE "${LUDISSEY_${framework}}")
endforeach()
target_link_libraries(LudisseyMacLibraries INTERFACE z curl)

function(ludissey_library target directory)
    file(GLOB_RECURSE source CONFIGURE_DEPENDS "${directory}/*.cpp" "${directory}/*.mm")
    list(FILTER source EXCLUDE REGEX "/Main\\.cpp$")
    add_library(${target} STATIC ${source})
    target_link_libraries(${target} PUBLIC LudisseyOptions ${ARGN})
    add_dependencies(${target} LudisseyGenerated)
endfunction()
ludissey_library(LudisseyLocalization Localization LudisseyMacLibraries)
ludissey_library(LudisseyLim Lim LudisseyLocalization)
ludissey_library(LudisseyEngine Engine LudisseyLim LudisseyMacLibraries)
ludissey_library(LudisseyInterface Interface LudisseyEngine)
ludissey_library(LudisseyPlatformMac Platform/Mac LudisseyInterface)
ludissey_library(LudisseyStudio Studio LudisseyInterface)
ludissey_library(LudisseyRuntime Player LudisseyStudio)
add_executable(Ludissey Player/Main.cpp)
target_link_libraries(Ludissey PRIVATE LudisseyRuntime LudisseyPlatformMac)

add_executable(LudisseyBackdrop Tools/Backdrop.cpp Tools/Scenery.cpp Tools/City.cpp Tools/Harbor.cpp)
target_include_directories(LudisseyBackdrop PRIVATE "${PROJECT_SOURCE_DIR}/Tools")
target_link_libraries(LudisseyBackdrop PRIVATE LudisseyRuntime LudisseyPlatformMac)

add_executable(LudisseyCharacters Tools/Characters.cpp)
target_link_libraries(LudisseyCharacters PRIVATE LudisseyEngine)

add_executable(LudisseyPublish Tools/Publish.cpp Engine/Core/Json.cpp)
target_include_directories(LudisseyPublish PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(LudisseyPublish PRIVATE curl)

set(LUDISSEY_STAGING "${CMAKE_BINARY_DIR}/Bundles")
set(LUDISSEY_BUNDLE_COMMANDS)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Resources/Icons/icon-names.json")
file(READ "${PROJECT_SOURCE_DIR}/Resources/Icons/icon-names.json" LUDISSEY_ICON_REGISTRY)
set(LUDISSEY_ICON_FILES "")
foreach(group semantic app planned)
    string(JSON count LENGTH "${LUDISSEY_ICON_REGISTRY}" ${group})
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON key MEMBER "${LUDISSEY_ICON_REGISTRY}" ${group} ${index})
        string(JSON value GET "${LUDISSEY_ICON_REGISTRY}" ${group} "${key}")
        string(REPLACE " " "" value "${value}")
        if(EXISTS "${PROJECT_SOURCE_DIR}/Resources/Icons/Pack/${value}.png")
            list(APPEND LUDISSEY_ICON_FILES "${PROJECT_SOURCE_DIR}/Resources/Icons/Pack/${value}.png")
        elseif(NOT group STREQUAL "planned")
            message(FATAL_ERROR "Icona del pacchetto mancante: ${value}.png")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES LUDISSEY_ICON_FILES)
set(LUDISSEY_SITE_ICON_COMMANDS "")
set(LUDISSEY_SITE_ICON_OUTPUTS "")
string(JSON count LENGTH "${LUDISSEY_ICON_REGISTRY}" app)
math(EXPR last "${count} - 1")
foreach(index RANGE ${last})
    string(JSON key MEMBER "${LUDISSEY_ICON_REGISTRY}" app ${index})
    string(JSON value GET "${LUDISSEY_ICON_REGISTRY}" app "${key}")
    string(REPLACE " " "" value "${value}")
    foreach(pixels 16 20 22 24 32 34 40 44 48 64 68)
        list(APPEND LUDISSEY_SITE_ICON_COMMANDS COMMAND /usr/bin/sips -s format png -z ${pixels} ${pixels} "${PROJECT_SOURCE_DIR}/Resources/Icons/Pack/${value}.png" --out "${CMAKE_BINARY_DIR}/Website/icons/${value}-${pixels}.png")
        list(APPEND LUDISSEY_SITE_ICON_OUTPUTS "${CMAKE_BINARY_DIR}/Website/icons/${value}-${pixels}.png")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES LUDISSEY_SITE_ICON_OUTPUTS)

if(DEFINED ENV{LUDISSEY_SIGN_IDENTITY} AND NOT "$ENV{LUDISSEY_SIGN_IDENTITY}" STREQUAL "")
    set(LUDISSEY_SIGN_OPTIONS --options runtime --timestamp --sign "$ENV{LUDISSEY_SIGN_IDENTITY}")
else()
    set(LUDISSEY_SIGN_OPTIONS --sign -)
    message(STATUS "LUDISSEY_SIGN_IDENTITY non impostata: le app hanno una firma locale, valida solo per prove su questo Mac.")
endif()
function(ludissey_bundle name identifier icon schemes)
    set(LUDISSEY_NAME "${name}")
    set(LUDISSEY_IDENTIFIER "${identifier}")
    set(LUDISSEY_SCHEMES "")
    foreach(scheme IN LISTS schemes)
        string(APPEND LUDISSEY_SCHEMES "                <string>${scheme}</string>\n")
    endforeach()
    set(plist "${LUDISSEY_STAGING}/${name}.plist")
    file(CONFIGURE OUTPUT "${plist}" @ONLY CONTENT [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleAllowMixedLocalizations</key>
    <true/>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>@LUDISSEY_NAME@</string>
    <key>CFBundleExecutable</key>
    <string>Ludissey</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon.icns</string>
    <key>CFBundleIdentifier</key>
    <string>@LUDISSEY_IDENTIFIER@</string>
    <key>CFBundleLocalizations</key>
    <array>
        <string>en</string>
        <string>it</string>
    </array>
    <key>CFBundleName</key>
    <string>@LUDISSEY_NAME@</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>@PROJECT_VERSION@</string>
    <key>CFBundleURLTypes</key>
    <array>
        <dict>
            <key>CFBundleURLName</key>
            <string>@LUDISSEY_IDENTIFIER@</string>
            <key>CFBundleURLSchemes</key>
            <array>
@LUDISSEY_SCHEMES@            </array>
        </dict>
    </array>
    <key>CFBundleVersion</key>
    <string>@LUDISSEY_BUILD_NUMBER@</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.games</string>
    <key>LSMinimumSystemVersion</key>
    <string>@CMAKE_OSX_DEPLOYMENT_TARGET@</string>
    <key>LSMultipleInstancesProhibited</key>
    <true/>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
]=])
    set(app "${LUDISSEY_STAGING}/${name}.app")
    set(resources "${app}/Contents/Resources")
    list(APPEND LUDISSEY_BUNDLE_COMMANDS
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${app}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${app}/Contents/MacOS" "${resources}/Registry" "${resources}/Licenses"
        COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/ThirdParty/MetalCpp/LICENSE.txt" "${resources}/Licenses/metal-cpp.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:Ludissey>" "${app}/Contents/MacOS/Ludissey"
        COMMAND "${CMAKE_COMMAND}" -E copy "${plist}" "${app}/Contents/Info.plist"
        COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/Resources/AppIcons/${icon}.icns" "${resources}/AppIcon.icns"
        COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/HistoricalAPI/2017.json" "${resources}/Registry/2017.json"
        COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/Resources/class-rules.json" "${resources}/Registry/class-rules.json"
        COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/Resources/experience-rules.json" "${resources}/experience-rules.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources/Characters" "${resources}/Characters"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources/Faces" "${resources}/Faces"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources/Portraits" "${resources}/Portraits"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources/Backgrounds" "${resources}/Backgrounds"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${resources}/Icons"
        COMMAND "${CMAKE_COMMAND}" -E copy ${LUDISSEY_ICON_FILES} "${resources}/Icons/"
        COMMAND /usr/bin/find "${app}" -name .DS_Store -delete
        COMMAND /usr/bin/codesign --force --deep ${LUDISSEY_SIGN_OPTIONS} "${app}"
        COMMAND /usr/bin/codesign --verify --deep --strict "${app}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LUDISSEY_APPS}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${LUDISSEY_APPS}/${name}.app"
        COMMAND "${CMAKE_COMMAND}" -E rename "${app}" "${LUDISSEY_APPS}/${name}.app")
    set(LUDISSEY_BUNDLE_COMMANDS "${LUDISSEY_BUNDLE_COMMANDS}" PARENT_SCOPE)
endfunction()
ludissey_bundle("Ludissey" "com.ludissey.mac" "Ludissey App" "ludissey-player;ludissey")
ludissey_bundle("Ludissey Studio" "com.ludissey.studio" "Ludissey Studio" "ludissey-studio")
add_custom_target(LudisseyBundles ${LUDISSEY_BUNDLE_COMMANDS} DEPENDS Ludissey VERBATIM)

add_executable(LudisseyImages Tools/Images.cpp Engine/Core/Png.cpp)
target_include_directories(LudisseyImages PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(LudisseyImages PRIVATE "-framework CoreFoundation" "-framework CoreGraphics" "-framework ImageIO")
set(LUDISSEY_WEBSITE "${CMAKE_BINARY_DIR}/Website")
add_custom_target(LudisseyWebsite
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${LUDISSEY_WEBSITE}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Web" "${LUDISSEY_WEBSITE}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources/Backgrounds" "${LUDISSEY_WEBSITE}/backgrounds"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources/Faces" "${LUDISSEY_WEBSITE}/faces"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources/Portraits" "${LUDISSEY_WEBSITE}/portraits"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LUDISSEY_WEBSITE}/download" "${LUDISSEY_WEBSITE}/icons"
    ${LUDISSEY_SITE_ICON_COMMANDS}
    COMMAND /usr/bin/sips -s format png -z 192 192 "${PROJECT_SOURCE_DIR}/Resources/AppIcons/Ludissey App.icns" --out "${LUDISSEY_WEBSITE}/download/Ludissey.png"
    COMMAND /usr/bin/sips -s format png -z 32 32 "${PROJECT_SOURCE_DIR}/Resources/AppIcons/Ludissey App.icns" --out "${LUDISSEY_WEBSITE}/favicon-32.png"
    COMMAND /usr/bin/sips -s format png -z 180 180 "${PROJECT_SOURCE_DIR}/Resources/AppIcons/Ludissey App.icns" --out "${LUDISSEY_WEBSITE}/apple-touch-icon.png"
    COMMAND /usr/bin/sips -s format png -z 192 192 "${PROJECT_SOURCE_DIR}/Resources/AppIcons/Ludissey Studio.icns" --out "${LUDISSEY_WEBSITE}/download/Ludissey-Studio.png"
    COMMAND LudisseyImages clean ${LUDISSEY_SITE_ICON_OUTPUTS}
    COMMAND LudisseyImages clean "${LUDISSEY_WEBSITE}/favicon-32.png" "${LUDISSEY_WEBSITE}/apple-touch-icon.png" "${LUDISSEY_WEBSITE}/download/Ludissey.png" "${LUDISSEY_WEBSITE}/download/Ludissey-Studio.png"
    DEPENDS LudisseyGenerated LudisseyImages VERBATIM)

add_executable(LudisseyDownloadsTool Tools/Downloads.cpp Engine/Core/Json.cpp Engine/Core/Text.cpp)
target_include_directories(LudisseyDownloadsTool PRIVATE "${PROJECT_SOURCE_DIR}")
add_custom_target(LudisseyDownloads
    COMMAND LudisseyDownloadsTool "${LUDISSEY_APPS}" "${LUDISSEY_WEBSITE}/download" "${PROJECT_VERSION}" "${LUDISSEY_BUILD_NUMBER}" "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    DEPENDS LudisseyBundles LudisseyWebsite VERBATIM)
add_dependencies(LudisseyWebsite LudisseyGenerated)

if(BUILD_TESTING)
    add_library(Catch2 STATIC ThirdParty/Catch2/catch_amalgamated.cpp)
    target_include_directories(Catch2 PUBLIC ThirdParty)
    find_library(LUDISSEY_IOKIT IOKit REQUIRED)
    find_library(LUDISSEY_JAVASCRIPTCORE JavaScriptCore REQUIRED)
    find_library(LUDISSEY_WEBKIT WebKit REQUIRED)
    function(ludissey_test target)
        add_executable(${target} ${ARGN} Tests/Support/Environment.cpp Tests/Support/Results.cpp Tests/Support/Timing.mm)
        target_compile_definitions(${target} PRIVATE LUDISSEY_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
        target_link_libraries(${target} PRIVATE LudisseyRuntime LudisseyPlatformMac Catch2 "${LUDISSEY_IOKIT}")
    endfunction()
    add_executable(LudisseyCrashProbe Tests/Support/CrashProbe.cpp)
    target_link_libraries(LudisseyCrashProbe PRIVATE LudisseyEngine)
    file(GLOB_RECURSE test_source CONFIGURE_DEPENDS Tests/Cpp/*.cpp Tests/Cpp/*.mm)
    ludissey_test(LudisseyTests ${test_source})
    target_compile_definitions(LudisseyTests PRIVATE LUDISSEY_CRASH_PROBE="$<TARGET_FILE:LudisseyCrashProbe>")
    add_dependencies(LudisseyTests LudisseyCrashProbe)
    target_link_libraries(LudisseyTests PRIVATE "${LUDISSEY_JAVASCRIPTCORE}" "${LUDISSEY_WEBKIT}")
    file(GLOB_RECURSE stress_source CONFIGURE_DEPENDS Tests/Stress/*.cpp Tests/Stress/*.mm)
    ludissey_test(LudisseyStress ${stress_source} Tools/Scenery.cpp Tools/City.cpp)
    target_sources(LudisseyTests PRIVATE Tools/Scenery.cpp Tools/City.cpp Tools/Harbor.cpp)
    target_include_directories(LudisseyTests PRIVATE "${PROJECT_SOURCE_DIR}/Tools")
    target_include_directories(LudisseyStress PRIVATE "${PROJECT_SOURCE_DIR}/Tools")
    file(GLOB_RECURSE hygiene_source CONFIGURE_DEPENDS Tests/Hygiene/*.cpp)
    ludissey_test(LudisseyHygiene ${hygiene_source})
    add_test(NAME LudisseyGenerated COMMAND LudisseyGenerate "${PROJECT_SOURCE_DIR}" --check)
    add_test(NAME LudisseyHygiene COMMAND LudisseyHygiene)
    add_test(NAME LudisseyTests COMMAND LudisseyTests --reporter console --reporter "junit::out=${CMAKE_BINARY_DIR}/test-results.xml")
    add_test(NAME LudisseyStress COMMAND LudisseyStress)
    set_tests_properties(LudisseyGenerated LudisseyHygiene PROPERTIES TIMEOUT 300)
    set_tests_properties(LudisseyTests PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    set_tests_properties(LudisseyStress PROPERTIES TIMEOUT 1800 ENVIRONMENT "LUDISSEY_STRESS_PROFILE=quick" RUN_SERIAL TRUE)
endif()
