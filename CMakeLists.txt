# =============================================================================
# MyuEngine - Editor & Runtime
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(MyuEngine
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "MyuEngine – cross-platform 2D/3D application & game engine"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On Windows MSVC: use /utf-8 for source and execution charset
if(MSVC)
    add_compile_options(/utf-8)
endif()

# =============================================================================
# 1. MyuDoglibs-Cpp (header-only utils only for now)
# =============================================================================
set(MYUDOG_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_GRAPHICS     OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_GAME         OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_WEB          OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_HTTP         OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_DB           OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_ML           OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_CRYPTO       OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_NETWORK      OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_SERIALIZATION OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_SYSTEM       OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_FILE         OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_EVENT        OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_TERMINAL     OFF CACHE BOOL "" FORCE)
set(MYUDOG_BUILD_UTILS        ON  CACHE BOOL "" FORCE)

set(MYUDOG_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../MyuDoglibs-Cpp")
if(EXISTS "${MYUDOG_LIBS_DIR}/CMakeLists.txt")
    add_subdirectory("${MYUDOG_LIBS_DIR}" "${CMAKE_BINARY_DIR}/_deps/myudog-libs")
else()
    include(FetchContent)
    FetchContent_Declare(myudog_libs
        GIT_REPOSITORY https://github.com/M4ng0D0g/MyuDoglibs-Cpp.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(myudog_libs)
endif()

# =============================================================================
# 2. Third-party dependencies
# =============================================================================
include(FetchContent)
find_package(OpenGL REQUIRED)

# --- SDL2 (static) ----------------------------------------------------------
set(SDL2_DISABLE_INSTALL  ON  CACHE BOOL "" FORCE)
set(SDL_SHARED            OFF CACHE BOOL "" FORCE)
set(SDL_STATIC            ON  CACHE BOOL "" FORCE)
set(SDL_TEST              OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.7
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(SDL2)

# --- glad (pre-generated GL 3.3 Core, bundled in external/glad) -------------
add_library(glad STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/gl.c"
)
target_include_directories(glad PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include"
)
target_link_libraries(glad PRIVATE ${CMAKE_DL_LIBS})

# --- ImGui (docking branch) -------------------------------------------------
FetchContent_Declare(imgui_fetch
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui_fetch)
set(IMGUI_DIR "${imgui_fetch_SOURCE_DIR}")
add_library(imgui_lib STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui_lib PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)
# Tell imgui_impl_opengl3 to use its own built-in GL loader
# (we use glad separately in our own code)
target_link_libraries(imgui_lib PUBLIC
    SDL2::SDL2-static
    OpenGL::GL
)

# =============================================================================
# 3. MyuEngine editor executable
# =============================================================================
add_executable(MyuEngine
    src/main.cpp
)

target_compile_definitions(MyuEngine PRIVATE SDL_MAIN_HANDLED)

target_link_libraries(MyuEngine PRIVATE
    SDL2::SDL2-static
    OpenGL::GL
    glad
    imgui_lib
    myudog::utils
)

if(WIN32)
    target_link_libraries(MyuEngine PRIVATE SDL2::SDL2main)
endif()

set_target_properties(MyuEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "MyuEngine"
)

# =============================================================================
# 4. Install & CPack (installer / setup.exe on Windows)
# =============================================================================
install(TARGETS MyuEngine RUNTIME DESTINATION bin COMPONENT Editor)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" DESTINATION . COMPONENT Editor)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lang/" DESTINATION lang COMPONENT Editor)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/extras/Tools/" DESTINATION tools COMPONENT Tools)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/extras/Samples/" DESTINATION samples COMPONENT Samples)

set(CPACK_PACKAGE_NAME            "MyuEngine")
set(CPACK_PACKAGE_VENDOR           "Myu")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "MyuEngine")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_COMPONENTS_ALL           Editor Tools Samples)
set(CPACK_COMPONENT_EDITOR_DISPLAY_NAME  "MyuEngine Editor")
set(CPACK_COMPONENT_TOOLS_DISPLAY_NAME   "Tools")
set(CPACK_COMPONENT_SAMPLES_DISPLAY_NAME "Samples")
set(CPACK_COMPONENT_EDITOR_REQUIRED ON)

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME       "MyuEngine ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME       "MyuEngine")
    set(CPACK_NSIS_INSTALL_ROOT       "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH        ON)
    set(CPACK_NSIS_CREATE_ICONS       "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\MyuEngine.lnk' '$INSTDIR\\\\bin\\\\MyuEngine.exe'")
    set(CPACK_NSIS_DELETE_ICONS       "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\MyuEngine.lnk'")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "$INSTDIR\\\\bin\\\\MyuEngine.exe")
    set(CPACK_NSIS_CONTACT            "chiayu.myu@icloud.com")
    set(CPACK_NSIS_URL_INFO_ABOUT     "https://myuengine.dev")
    set(CPACK_NSIS_MENU_LINKS
        "https://myuengine.dev" "MyuEngine Home"
        "https://myuengine.dev/docs" "Documentation"
    )
    set(CPACK_NSIS_COMPONENT_INSTALL ON)
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\MyuEngine.exe")
    # Maintenance mode: detect previous install, offer Repair / Uninstall / Cancel
    # Use NSIS backtick strings to avoid CMake/NSIS quote escaping issues
    set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "
        ReadRegStr $0 HKCU `Software\\MyuEngine` `InstallDir`
        StrCmp $0 \"\" done_maint
        MessageBox MB_YESNOCANCEL|MB_ICONQUESTION `MyuEngine is already installed at $0.$\\r$\\nYes = Repair / Reinstall$\\nNo = Uninstall$\\nCancel = Abort` IDYES do_repair IDNO do_uninstall
        Abort
      do_uninstall:
        ExecWait `\"$0\\uninstall.exe\" /S`
        Goto done_maint
      do_repair:
        StrCpy $INSTDIR $0
      done_maint:
    ")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKCU `Software\\MyuEngine` `InstallDir` `$INSTDIR`
        WriteRegStr HKCU `Software\\MyuEngine` `Version` `${PROJECT_VERSION}`
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegKey HKCU `Software\\MyuEngine`
    ")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
