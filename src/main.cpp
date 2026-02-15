// =============================================================================
// MyuEngine – Editor entry point
// SDL2 + OpenGL 3.3 Core + Dear ImGui (docking)
// Cross-platform: Windows x86-64 / Ubuntu
// =============================================================================

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include <glad/gl.h>  // glad2: gl.h (not glad.h)

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>          // DockBuilder API

#include <myudog/utils/Logger.hpp>
#include <myudog/utils/Time.hpp>

#include "ui_designer/UIDesigner.h"
#include "editor/GameEditor.h"
#include "editor/VoxelEditor.h"
#include "engine/ECS.h"
#include "engine/Resources.h"
#include "engine/EventBus.h"
#include "tools/BlockbenchImport.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shlobj.h>
#  ifdef min
#    undef min
#  endif
#  ifdef max
#    undef max
#  endif
#endif

namespace fs = std::filesystem;
using myudog::utils::Logger;

// ============================================================================
// In-app log buffer (also forwards to stdout / MyuDog Logger)
// ============================================================================
struct LogEntry {
    enum Level { kInfo, kWarn, kError };
    Level       level;
    std::string text;
};

struct LogBuffer {
    std::vector<LogEntry> entries;
    bool autoScroll = true;

    void info(const std::string& msg)  { push(LogEntry::kInfo,  msg); }
    void warn(const std::string& msg)  { push(LogEntry::kWarn,  msg); }
    void error(const std::string& msg) { push(LogEntry::kError, msg); }

private:
    void push(LogEntry::Level lvl, const std::string& msg) {
        entries.push_back({lvl, msg});
        // Also route to myudog Logger (stdout + optional file)
        switch (lvl) {
            case LogEntry::kInfo:  Logger::instance().info(msg);  break;
            case LogEntry::kWarn:  Logger::instance().warn(msg);  break;
            case LogEntry::kError: Logger::instance().error(msg); break;
        }
    }
};

static LogBuffer gLog;

// ============================================================================
// Platform: allocate a real console window on Windows for log output
// ============================================================================
#ifdef _WIN32
static void allocDebugConsole() {
    if (GetConsoleWindow() != nullptr) return;
    if (!AllocConsole()) return;
    SetConsoleTitle(TEXT("MyuEngine – Log Console"));
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$",  "r", stdin);
}
#else
static void allocDebugConsole() {
    // On Linux, stdout already goes to the launching terminal.
}
#endif

// ============================================================================
// SDL log callback → forward into gLog
// ============================================================================
static void sdlLogCb(void*, int, SDL_LogPriority pri, const char* msg) {
    if (!msg) return;
    std::string text = std::string("[SDL] ") + msg;
    if (pri >= SDL_LOG_PRIORITY_ERROR)
        gLog.error(text);
    else if (pri >= SDL_LOG_PRIORITY_WARN)
        gLog.warn(text);
    else
        gLog.info(text);
}

// ============================================================================
// User data directory (cross-platform)
// ============================================================================
static std::string readFirstLine(const fs::path& path) {
    std::ifstream f(path);
    std::string line;
    if (f && std::getline(f, line)) {
        if (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
    }
    return line;
}

static fs::path getExecutableDir() {
    fs::path dir;
    if (char* base = SDL_GetBasePath()) {
        dir = fs::path(base);
        SDL_free(base);
    } else {
        dir = fs::current_path();
    }
    return dir;
}

static fs::path getPortableDataDir() {
    if (const char* e = std::getenv("MYUENGINE_HOME"); e && *e)
        return fs::path(e);
    fs::path marker = getExecutableDir() / "myuengine_home.txt";
    std::error_code ec;
    if (fs::exists(marker, ec)) {
        std::string line = readFirstLine(marker);
        if (!line.empty()) return fs::path(line);
    }
    return {};
}

static bool writePortableHomeFile(const fs::path& dataRoot, std::string& err) {
    fs::path marker = getExecutableDir() / "myuengine_home.txt";
    std::ofstream f(marker);
    if (!f) { err = "Cannot write myuengine_home.txt"; return false; }
    f << dataRoot.string();
    return true;
}

static bool clearPortableHomeFile(std::string& err) {
    fs::path marker = getExecutableDir() / "myuengine_home.txt";
    std::error_code ec;
    fs::remove(marker, ec);
    if (ec) { err = ec.message(); return false; }
    return true;
}

static bool moveAppData(const fs::path& fromBase, const fs::path& toBase, std::string& err) {
    if (fromBase == toBase) return true;
    std::error_code ec;
    fs::path from = fromBase / "MyuEngine";
    fs::path to = toBase / "MyuEngine";
    if (!fs::exists(from, ec)) return true;
    fs::create_directories(toBase, ec);
    if (ec) { err = ec.message(); return false; }

    fs::rename(from, to, ec);
    if (!ec) return true;

    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) { err = ec.message(); return false; }
    fs::remove_all(from, ec);
    if (ec) { err = ec.message(); return false; }
    return true;
}

static bool simulateInstall(const fs::path& targetDir, std::string& err) {
    std::error_code ec;
    if (targetDir.empty()) { err = "Target path is empty"; return false; }
    fs::path binDir = targetDir / "bin";
    fs::create_directories(binDir, ec);
    if (ec) { err = ec.message(); return false; }

#ifdef _WIN32
    fs::path exeName = "MyuEngine.exe";
#else
    fs::path exeName = "MyuEngine";
#endif
    fs::path exePath = getExecutableDir() / exeName;
    if (!fs::exists(exePath, ec)) { err = "Executable not found"; return false; }

    fs::copy_file(exePath, binDir / exeName, fs::copy_options::overwrite_existing, ec);
    if (ec) { err = ec.message(); return false; }
    return true;
}

static fs::path getUserDataDir() {
    if (auto portable = getPortableDataDir(); !portable.empty())
        return portable;
#ifdef _WIN32
    PWSTR wpath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wpath))) {
        fs::path p(wpath);
        CoTaskMemFree(wpath);
        return p;
    }
    if (const char* e = std::getenv("LOCALAPPDATA")) return fs::path(e);
    if (const char* e = std::getenv("USERPROFILE"))  return fs::path(e);
    return fs::current_path();
#else
    if (const char* e = std::getenv("XDG_DATA_HOME"); e && *e) return fs::path(e);
    if (const char* e = std::getenv("HOME"); e && *e)          return fs::path(e) / ".local" / "share";
    return fs::current_path();
#endif
}

static fs::path getProjectsRoot() {
    fs::path root = getUserDataDir() / "MyuEngine" / "Projects";
    std::error_code ec;
    fs::create_directories(root, ec);
    return root;
}

// ============================================================================
// Project templates
// ============================================================================
enum class ProjectTemplate {
    Game2D,
    Game3D,
    BoardCardGame,
    WebFrontend,
    WebBackend,
    WebFullStack,
    DesktopApp,
    Library,
    Empty,
    kCount
};

static constexpr int kTemplateCount = static_cast<int>(ProjectTemplate::kCount);

struct TemplateInfo {
    const char* label;
    const char* icon;        // short emoji/symbol prefix
    const char* description;
};

static const TemplateInfo kTemplates[] = {
    { "2D Game",           "[2D]",   "2D game with sprites, tilemaps, physics, audio." },
    { "3D Game",           "[3D]",   "3D game with meshes, lighting, camera, physics." },
    { "Board Card Game",   "[BCG]",  "Chess + card strategy game with board, pieces, cards, shop." },
    { "Web Frontend",      "[FE]",   "Frontend web app (HTML/CSS/JS assets, UI)." },
    { "Web Backend",       "[BE]",   "Backend server application (API, DB)." },
    { "Web Full-Stack",    "[FS]",   "Combined frontend + backend project." },
    { "Desktop App",       "[APP]",  "Native desktop application with GUI." },
    { "Library / Module",  "[LIB]",  "Reusable library or shared module." },
    { "Empty Project",     "[ ]",    "Blank project – create your own structure." },
};
static_assert(sizeof(kTemplates) / sizeof(kTemplates[0]) == kTemplateCount);

// ============================================================================
// File writer helper
// ============================================================================
static void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream f(path);
    if (f) f << content;
}

// ============================================================================
// Per-template scaffolding
// ============================================================================
static void scaffoldGame2D(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Assets" / "Sprites", ec);
    fs::create_directories(dir / "Assets" / "Tilemaps", ec);
    fs::create_directories(dir / "Assets" / "Audio", ec);
    fs::create_directories(dir / "Assets" / "Fonts", ec);
    fs::create_directories(dir / "Scenes", ec);
    fs::create_directories(dir / "Scripts", ec);
    fs::create_directories(dir / "Config", ec);

    writeFile(dir / "Scripts" / "main.cpp",
        "// Auto-generated by MyuEngine\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    std::cout << \"Hello 2D Game!\\n\";\n"
        "    return 0;\n"
        "}\n");

    writeFile(dir / "Scenes" / "MainScene.scene",
        "{\n"
        "  \"name\": \"MainScene\",\n"
        "  \"type\": \"2D\",\n"
        "  \"entities\": []\n"
        "}\n");

    writeFile(dir / "Config" / "game.cfg",
        "# Game configuration\n"
        "window_width=800\n"
        "window_height=600\n"
        "title=My 2D Game\n"
        "target_fps=60\n");
}

static void scaffoldGame3D(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Assets" / "Models", ec);
    fs::create_directories(dir / "Assets" / "Textures", ec);
    fs::create_directories(dir / "Assets" / "Shaders", ec);
    fs::create_directories(dir / "Assets" / "Audio", ec);
    fs::create_directories(dir / "Assets" / "Fonts", ec);
    fs::create_directories(dir / "Scenes", ec);
    fs::create_directories(dir / "Scripts", ec);
    fs::create_directories(dir / "Config", ec);

    writeFile(dir / "Scripts" / "main.cpp",
        "// Auto-generated by MyuEngine\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    std::cout << \"Hello 3D Game!\\n\";\n"
        "    return 0;\n"
        "}\n");

    writeFile(dir / "Assets" / "Shaders" / "default.vert",
        "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec3 aNormal;\n"
        "layout (location = 2) in vec2 aTexCoord;\n\n"
        "uniform mat4 uModel;\n"
        "uniform mat4 uView;\n"
        "uniform mat4 uProjection;\n\n"
        "out vec3 FragPos;\n"
        "out vec3 Normal;\n"
        "out vec2 TexCoord;\n\n"
        "void main() {\n"
        "    FragPos  = vec3(uModel * vec4(aPos, 1.0));\n"
        "    Normal   = mat3(transpose(inverse(uModel))) * aNormal;\n"
        "    TexCoord = aTexCoord;\n"
        "    gl_Position = uProjection * uView * vec4(FragPos, 1.0);\n"
        "}\n");

    writeFile(dir / "Assets" / "Shaders" / "default.frag",
        "#version 330 core\n"
        "in vec3 FragPos;\n"
        "in vec3 Normal;\n"
        "in vec2 TexCoord;\n\n"
        "out vec4 FragColor;\n\n"
        "uniform vec3 uLightPos;\n"
        "uniform vec3 uViewPos;\n\n"
        "void main() {\n"
        "    vec3 norm     = normalize(Normal);\n"
        "    vec3 lightDir = normalize(uLightPos - FragPos);\n"
        "    float diff    = max(dot(norm, lightDir), 0.0);\n"
        "    vec3 color    = vec3(0.8) * (0.2 + 0.8 * diff);\n"
        "    FragColor     = vec4(color, 1.0);\n"
        "}\n");

    writeFile(dir / "Scenes" / "MainScene.scene",
        "{\n"
        "  \"name\": \"MainScene\",\n"
        "  \"type\": \"3D\",\n"
        "  \"camera\": { \"position\": [0,2,5], \"target\": [0,0,0] },\n"
        "  \"entities\": []\n"
        "}\n");

    writeFile(dir / "Config" / "game.cfg",
        "# Game configuration\n"
        "window_width=1280\n"
        "window_height=720\n"
        "title=My 3D Game\n"
        "target_fps=60\n"
        "vsync=1\n");
}

static void scaffoldWebFrontend(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "public", ec);
    fs::create_directories(dir / "src" / "components", ec);
    fs::create_directories(dir / "src" / "styles", ec);
    fs::create_directories(dir / "src" / "assets", ec);

    writeFile(dir / "public" / "index.html",
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>MyuEngine Web App</title>\n"
        "  <link rel=\"stylesheet\" href=\"../src/styles/main.css\">\n"
        "</head>\n"
        "<body>\n"
        "  <div id=\"app\"></div>\n"
        "  <script src=\"../src/main.js\"></script>\n"
        "</body>\n"
        "</html>\n");

    writeFile(dir / "src" / "main.js",
        "// Auto-generated by MyuEngine\n"
        "document.getElementById('app').innerHTML = '<h1>Hello Frontend!</h1>';\n");

    writeFile(dir / "src" / "styles" / "main.css",
        "/* Auto-generated by MyuEngine */\n"
        "body { margin: 0; font-family: sans-serif; background: #1e1e2e; color: #cdd6f4; }\n"
        "#app { display: flex; justify-content: center; align-items: center; height: 100vh; }\n");
}

static void scaffoldWebBackend(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "src" / "routes", ec);
    fs::create_directories(dir / "src" / "models", ec);
    fs::create_directories(dir / "src" / "middleware", ec);
    fs::create_directories(dir / "config", ec);
    fs::create_directories(dir / "tests", ec);

    writeFile(dir / "src" / "main.cpp",
        "// Auto-generated by MyuEngine\n"
        "// Backend server entry point\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    std::cout << \"Backend server starting...\\n\";\n"
        "    // TODO: Initialize HTTP server with myudog::http\n"
        "    return 0;\n"
        "}\n");

    writeFile(dir / "config" / "server.cfg",
        "# Server configuration\n"
        "host=0.0.0.0\n"
        "port=8080\n"
        "workers=4\n"
        "db_connection=sqlite://data.db\n");

    writeFile(dir / "src" / "routes" / "api.cpp",
        "// Auto-generated by MyuEngine\n"
        "// Define your API routes here\n");
}

static void scaffoldWebFullStack(const fs::path& dir) {
    std::error_code ec;
    // Frontend
    fs::create_directories(dir / "frontend" / "public", ec);
    fs::create_directories(dir / "frontend" / "src" / "components", ec);
    fs::create_directories(dir / "frontend" / "src" / "styles", ec);
    // Backend
    fs::create_directories(dir / "backend" / "src" / "routes", ec);
    fs::create_directories(dir / "backend" / "src" / "models", ec);
    fs::create_directories(dir / "backend" / "config", ec);
    fs::create_directories(dir / "backend" / "tests", ec);
    // Shared
    fs::create_directories(dir / "shared", ec);

    writeFile(dir / "frontend" / "public" / "index.html",
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head><meta charset=\"UTF-8\"><title>Full-Stack App</title></head>\n"
        "<body><div id=\"app\"></div></body>\n"
        "</html>\n");

    writeFile(dir / "frontend" / "src" / "main.js",
        "// Auto-generated by MyuEngine\n"
        "fetch('/api/hello').then(r => r.json()).then(d => {\n"
        "    document.getElementById('app').innerHTML = '<h1>' + d.message + '</h1>';\n"
        "});\n");

    writeFile(dir / "backend" / "src" / "main.cpp",
        "// Auto-generated by MyuEngine\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    std::cout << \"Full-stack server starting...\\n\";\n"
        "    return 0;\n"
        "}\n");

    writeFile(dir / "backend" / "config" / "server.cfg",
        "# Server configuration\n"
        "host=0.0.0.0\n"
        "port=8080\n");
}

static void scaffoldDesktopApp(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "src", ec);
    fs::create_directories(dir / "resources" / "icons", ec);
    fs::create_directories(dir / "resources" / "fonts", ec);
    fs::create_directories(dir / "config", ec);

    writeFile(dir / "src" / "main.cpp",
        "// Auto-generated by MyuEngine\n"
        "// Desktop application entry point\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    std::cout << \"Desktop App launching...\\n\";\n"
        "    // TODO: Initialize SDL2 window + ImGui UI\n"
        "    return 0;\n"
        "}\n");

    writeFile(dir / "config" / "app.cfg",
        "# Desktop App configuration\n"
        "window_width=1024\n"
        "window_height=768\n"
        "title=My Desktop App\n"
        "resizable=1\n");
}

static void scaffoldLibrary(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "include", ec);
    fs::create_directories(dir / "src", ec);
    fs::create_directories(dir / "tests", ec);
    fs::create_directories(dir / "docs", ec);

    writeFile(dir / "include" / "mylib.hpp",
        "#pragma once\n"
        "// Auto-generated by MyuEngine\n\n"
        "namespace mylib {\n\n"
        "int add(int a, int b);\n\n"
        "} // namespace mylib\n");

    writeFile(dir / "src" / "mylib.cpp",
        "// Auto-generated by MyuEngine\n"
        "#include \"mylib.hpp\"\n\n"
        "namespace mylib {\n\n"
        "int add(int a, int b) { return a + b; }\n\n"
        "} // namespace mylib\n");

    writeFile(dir / "tests" / "test_main.cpp",
        "// Auto-generated by MyuEngine\n"
        "#include \"mylib.hpp\"\n"
        "#include <cassert>\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    assert(mylib::add(2, 3) == 5);\n"
        "    std::cout << \"All tests passed!\\n\";\n"
        "    return 0;\n"
        "}\n");

    writeFile(dir / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(mylib LANGUAGES CXX)\n\n"
        "set(CMAKE_CXX_STANDARD 20)\n\n"
        "add_library(mylib src/mylib.cpp)\n"
        "target_include_directories(mylib PUBLIC include)\n\n"
        "add_executable(mylib_test tests/test_main.cpp)\n"
        "target_link_libraries(mylib_test PRIVATE mylib)\n");
}

static void scaffoldEmpty(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "src", ec);
    fs::create_directories(dir / "docs", ec);

    writeFile(dir / "README.md",
        "# New Project\n\nCreated with MyuEngine.\n");
}

static void scaffoldBoardCardGame(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Assets" / "Sprites" / "Pieces", ec);
    fs::create_directories(dir / "Assets" / "Sprites" / "Cards", ec);
    fs::create_directories(dir / "Assets" / "Audio" / "BGM", ec);
    fs::create_directories(dir / "Assets" / "Audio" / "SFX", ec);
    fs::create_directories(dir / "Assets" / "Boards", ec);
    fs::create_directories(dir / "Config", ec);
    fs::create_directories(dir / "Scripts", ec);
    fs::create_directories(dir / "Scenes", ec);

    writeFile(dir / "Config" / "board.json",
        "{\n"
        "  \"rows\": 8, \"cols\": 8,\n"
        "  \"cellSize\": 1.0,\n"
        "  \"pieceTypes\": []\n"
        "}\n");

    writeFile(dir / "Config" / "cards.json",
        "{\n"
        "  \"templates\": [],\n"
        "  \"deckSize\": 30\n"
        "}\n");

    writeFile(dir / "Config" / "game.json",
        "{\n"
        "  \"turnTimeLimit\": 60,\n"
        "  \"winScore\": 0,\n"
        "  \"startGold\": 100,\n"
        "  \"drawPerTurn\": 1,\n"
        "  \"startHandSize\": 5\n"
        "}\n");

    writeFile(dir / "Scripts" / "main.cpp",
        "// Board + Card Strategy Game\n"
        "// Auto-generated by MyuEngine\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    std::cout << \"Board Card Game!\\n\";\n"
        "    return 0;\n"
        "}\n");

    writeFile(dir / "Scenes" / "GameScene.scene",
        "{\n"
        "  \"name\": \"GameScene\",\n"
        "  \"objects\": [\n"
        "    {\"name\": \"Background\", \"tag\": \"background\"},\n"
        "    {\"name\": \"Board\", \"tag\": \"board\"},\n"
        "    {\"name\": \"Pieces\", \"tag\": \"group\"},\n"
        "    {\"name\": \"Cards\", \"tag\": \"group\"},\n"
        "    {\"name\": \"UI Layer\", \"tag\": \"ui\"}\n"
        "  ]\n"
        "}\n");

    writeFile(dir / "README.md",
        "# Board + Card Strategy Game\n\n"
        "Created with MyuEngine.\n\n"
        "## Structure\n"
        "- `Config/` – Board, card library, and game settings\n"
        "- `Assets/` – Sprites, audio, board textures\n"
        "- `Scenes/` – Scene definitions\n"
        "- `Scripts/` – Custom game logic\n");
}

static void scaffoldTemplate(ProjectTemplate tmpl, const fs::path& dir) {
    switch (tmpl) {
        case ProjectTemplate::Game2D:        scaffoldGame2D(dir);        break;
        case ProjectTemplate::Game3D:        scaffoldGame3D(dir);        break;
        case ProjectTemplate::BoardCardGame: scaffoldBoardCardGame(dir); break;
        case ProjectTemplate::WebFrontend:   scaffoldWebFrontend(dir);   break;
        case ProjectTemplate::WebBackend:    scaffoldWebBackend(dir);    break;
        case ProjectTemplate::WebFullStack:  scaffoldWebFullStack(dir);  break;
        case ProjectTemplate::DesktopApp:    scaffoldDesktopApp(dir);    break;
        case ProjectTemplate::Library:       scaffoldLibrary(dir);       break;
        case ProjectTemplate::Empty:         scaffoldEmpty(dir);         break;
        default: break;
    }
}

// ============================================================================
// Project helpers
// ============================================================================
static bool isValidProjectName(const std::string& name, std::string& reason) {
    if (name.empty())     { reason = "Name is empty";    return false; }
    if (name.size() > 64) { reason = "Name is too long"; return false; }
    static const std::string kBad = "<>:\"/\\|?*";
    for (char ch : name) {
        if (static_cast<unsigned char>(ch) < 32) { reason = "Contains control chars"; return false; }
        if (kBad.find(ch) != std::string::npos)  { reason = "Contains invalid chars"; return false; }
    }
    reason.clear();
    return true;
}

static std::vector<fs::path> listProjects(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::exists(root, ec)) return out;
    for (auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory()) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

static std::string readProjectMeta(const fs::path& projectDir, const std::string& key) {
    fs::path metaPath = projectDir / "project.mye";
    std::ifstream f(metaPath);
    if (!f) return {};
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos && line.substr(0, eq) == key)
            return line.substr(eq + 1);
    }
    return {};
}

static bool createProject(const fs::path& root, const std::string& name,
                           ProjectTemplate tmpl, std::string& err)
{
    if (!isValidProjectName(name, err)) return false;
    fs::path dir = root / name;
    std::error_code ec;
    if (fs::exists(dir, ec))              { err = "Already exists"; return false; }
    if (!fs::create_directories(dir, ec)) { err = "mkdir failed";   return false; }

    // Write project meta
    std::ofstream meta(dir / "project.mye");
    if (!meta) { err = "Cannot write project.mye"; return false; }
    meta << "name=" << name << "\n"
         << "template=" << kTemplates[static_cast<int>(tmpl)].label << "\n"
         << "version=0.1.0\n"
         << "engine=MyuEngine\n"
         << "created_ms=" << myudog::utils::systemNowMs() << "\n";
    meta.close();

    // Scaffold template-specific structure
    scaffoldTemplate(tmpl, dir);
    return true;
}

static bool deleteProject(const fs::path& path, std::string& err) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) { err = ec.message(); return false; }
    return true;
}

// ============================================================================
// ImGui helpers
// ============================================================================
static void setupDarkTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 4.0f;
    s.FrameRounding    = 3.0f;
    s.GrabRounding     = 3.0f;
    s.ScrollbarRounding= 3.0f;
    s.WindowBorderSize = 1.0f;
    ImGui::StyleColorsDark();
    auto& c = s.Colors;
    c[ImGuiCol_WindowBg]          = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    c[ImGuiCol_TitleBg]           = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    c[ImGuiCol_Header]            = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.28f, 0.32f, 0.42f, 1.0f);
    c[ImGuiCol_Button]            = ImVec4(0.22f, 0.24f, 0.32f, 1.0f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.30f, 0.34f, 0.44f, 1.0f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
    c[ImGuiCol_Tab]               = ImVec4(0.14f, 0.14f, 0.20f, 1.0f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.24f, 0.28f, 0.40f, 1.0f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.20f, 0.24f, 0.36f, 1.0f);
}

static void drawLogPanel(LogBuffer& log) {
    ImGui::Begin("Log Console");

    if (ImGui::Button("Clear")) log.entries.clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &log.autoScroll);
    ImGui::Separator();

    ImGui::BeginChild("##logscroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& e : log.entries) {
        ImVec4 col;
        switch (e.level) {
            case LogEntry::kWarn:  col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f); break;
            case LogEntry::kError: col = ImVec4(1.0f, 0.3f,  0.3f, 1.0f); break;
            default:               col = ImVec4(0.8f, 0.8f,  0.8f, 1.0f); break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(e.text.c_str());
        ImGui::PopStyleColor();
    }
    if (log.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

// ============================================================================
// Create-project wizard state
// ============================================================================
struct CreateWizard {
    bool              open = false;
    char              name[64] = {};
    int               selectedTemplate = 0;
    std::string       errorMsg;

    void reset() {
        open = false;
        name[0] = '\0';
        selectedTemplate = 0;
        errorMsg.clear();
    }
};

static void drawCreateWizard(
    CreateWizard& wiz,
    const fs::path& root,
    std::vector<fs::path>& projects)
{
    if (!wiz.open) return;

    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_Appearing);
    if (!ImGui::Begin("Create New Project", &wiz.open)) {
        ImGui::End();
        return;
    }

    // --- Project name ---
    ImGui::Text("Project Name");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##wizname", wiz.name, sizeof(wiz.name));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Template selection ---
    ImGui::Text("Select Template");
    ImGui::Spacing();

    ImGui::BeginChild("##tmpllist", ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() * 3),
                      ImGuiChildFlags_Border);
    for (int i = 0; i < kTemplateCount; ++i) {
        auto& t = kTemplates[i];
        ImGui::PushID(i);
        bool sel = (wiz.selectedTemplate == i);

        // Selectable card
        char label[128];
        std::snprintf(label, sizeof(label), "%s  %s", t.icon, t.label);
        if (ImGui::Selectable(label, sel, 0, ImVec2(0, 40))) {
            wiz.selectedTemplate = i;
        }
        // Description tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", t.description);
        }
        // Inline description when selected
        if (sel) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220);
            ImGui::TextDisabled("%s", t.description);
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    // --- Error message ---
    if (!wiz.errorMsg.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
        ImGui::TextWrapped("%s", wiz.errorMsg.c_str());
        ImGui::PopStyleColor();
    }

    // --- Buttons ---
    ImGui::Spacing();
    float btnWidth = 120;
    float totalWidth = btnWidth * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f + ImGui::GetCursorPosX());

    if (ImGui::Button("Create", ImVec2(btnWidth, 0))) {
        wiz.errorMsg.clear();
        auto tmpl = static_cast<ProjectTemplate>(wiz.selectedTemplate);
        if (createProject(root, wiz.name, tmpl, wiz.errorMsg)) {
            gLog.info(std::string("Created project [") +
                      kTemplates[wiz.selectedTemplate].label + "]: " + wiz.name);
            projects = listProjects(root);
            wiz.reset();
        } else {
            gLog.error("Create failed: " + wiz.errorMsg);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(btnWidth, 0))) {
        wiz.reset();
    }

    ImGui::End();
}

// ============================================================================
// Projects panel
// ============================================================================
static void drawProjectsPanel(
    const fs::path& root,
    std::vector<fs::path>& projects,
    std::optional<fs::path>& selected,
    CreateWizard& wizard,
    bool& openRequested)
{
    openRequested = false;
    ImGui::Begin("Projects");

    // -- Header
    ImGui::TextDisabled("Root: %s", root.string().c_str());
    ImGui::Separator();

    // -- Toolbar
    if (ImGui::Button("+ New Project")) {
        wizard.reset();
        wizard.open = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) projects = listProjects(root);

    ImGui::Separator();

    // -- Project list
    ImGui::BeginChild("##projlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 5),
                      ImGuiChildFlags_Border);
    for (auto& p : projects) {
        bool sel = selected && *selected == p;
        std::string tmplTag = readProjectMeta(p, "template");
        std::string display = p.filename().string();
        if (!tmplTag.empty())
            display += "  (" + tmplTag + ")";
        if (ImGui::Selectable(display.c_str(), sel))
            selected = p;
    }
    ImGui::EndChild();

    // -- Details
    if (selected) {
        ImGui::Separator();
        ImGui::Text("Selected: %s", selected->filename().string().c_str());

        std::string tmpl = readProjectMeta(*selected, "template");
        std::string ver  = readProjectMeta(*selected, "version");
        if (!tmpl.empty()) ImGui::Text("Template: %s", tmpl.c_str());
        if (!ver.empty())  ImGui::Text("Version:  %s", ver.c_str());
        ImGui::TextWrapped("Path: %s", selected->string().c_str());

        if (ImGui::Button("Open")) {
            gLog.info("Open project: " + selected->filename().string());
            openRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            ImGui::OpenPopup("ConfirmDelete");
        }

        if (ImGui::BeginPopupModal("ConfirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete \"%s\"? This cannot be undone.", selected->filename().string().c_str());
            if (ImGui::Button("Yes, Delete")) {
                std::string err;
                if (deleteProject(*selected, err)) {
                    gLog.info("Deleted: " + selected->filename().string());
                    selected.reset();
                    projects = listProjects(root);
                } else {
                    gLog.error("Delete failed: " + err);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

// ============================================================================
// main
// ============================================================================
int main(int /*argc*/, char** /*argv*/) {
    // --- Console for log output ---
    allocDebugConsole();

    // --- MyuDog Logger setup ---
    Logger::instance().setLogLevel(myudog::utils::LogLevel::Debug);
    Logger::instance().setColorEnabled(true);

    // --- SDL init ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_LogSetOutputFunction(sdlLogCb, nullptr);

    // --- GL attributes ---
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // --- Window ---
    SDL_Window* window = SDL_CreateWindow(
        "MyuEngine v0.1.0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if (!window) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "GL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1); // vsync

    // --- glad2 loader ---
    int glVersion = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
    if (!glVersion) {
        std::fprintf(stderr, "gladLoadGL failed\n");
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    gLog.info(std::string("OpenGL ") +
              std::to_string(GLAD_VERSION_MAJOR(glVersion)) + "." +
              std::to_string(GLAD_VERSION_MINOR(glVersion)));

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = "myuengine_imgui.ini"; // persists layout

    setupDarkTheme();

    // Viewport: keep opaque backgrounds on platform windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiStyle& vstyle = ImGui::GetStyle();
        vstyle.WindowRounding = 0.0f;
        vstyle.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    // --- State ---
    fs::path dataRoot = getUserDataDir();
    fs::path projectsRoot = getProjectsRoot();
    auto projects = listProjects(projectsRoot);
    std::optional<fs::path> selectedProject;
    CreateWizard createWizard;

    // UI Designer state
    myu::ui::DesignerState uiDesigner;
    bool uiDesignerOpen = false;
    std::string openedProjectName;

    // Game Editor state
    myu::editor::GameEditorState gameEditor;
    bool gameEditorOpen = false;

    // Engine core systems
    myu::engine::ECSWorld ecsWorld;
    myu::engine::ResourceManager resources;
    myu::engine::EventBus eventBus;

    // Voxel editor
    myu::editor::VoxelEditorState voxelEditor;
    bool showVoxelEditor = true;
    bool showBlockbench = true;
    bool showResources = true;
    bool showEcs = true;
    bool showEvents = true;

    std::vector<myu::engine::Event> eventLog;
    eventBus.subscribe([&](const myu::engine::Event& e) {
        eventLog.push_back(e);
        if (eventLog.size() > 200)
            eventLog.erase(eventLog.begin());
    });

    enum class EditorMode { None, UIDesigner, GameEditor };
    EditorMode editorMode = EditorMode::None;
    bool editorDockNeedsInit = false;

    gLog.info("MyuEngine editor started");
    gLog.info("Projects root: " + projectsRoot.string());

    bool showAbout = false;
    bool running   = true;
    bool enableViewports = true;
    bool enableVsync = true;
    bool reduceFpsWhenUnfocused = true;
    bool pauseRenderWhenUnfocused = true;
    bool pauseRenderWhenMinimized = true;
    bool reduceHeavyWhenIdle = true;
    int  idleRenderMs = 250; // ms before throttling heavy panels
    int  fpsLimit = 60; // 0 = unlimited
    int  idleFpsLimit = 15; // FPS cap when unfocused
    int  platformFpsLimit = 60; // FPS cap for platform windows (viewports)
    bool pausePlatformWhenMinimized = true;
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 frameStart = 0;
    Uint64 lastInputCounter = 0;
    Uint64 lastPlatformRender = 0;
    bool lastViewports = enableViewports;
    bool lastVsync = enableVsync;
    auto lastScan  = myudog::utils::steadyNowMs();

    char dataRootBuf[512] = {};
    char simInstallBuf[512] = {};
    std::strncpy(dataRootBuf, dataRoot.string().c_str(), sizeof(dataRootBuf) - 1);

    // --- Main loop ---
    while (running) {
        // Events
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_WINDOWEVENT &&
                ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                ev.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        // Periodic re-scan
        auto now = myudog::utils::steadyNowMs();
        if (now - lastScan > 3000) {
            projects = listProjects(projectsRoot);
            lastScan = now;
        }

        if (lastInputCounter == 0)
            lastInputCounter = SDL_GetPerformanceCounter();

        SDL_Window* focusedWindow = SDL_GetKeyboardFocus();
        bool appFocused = (focusedWindow != nullptr);
        if (!appFocused && pauseRenderWhenUnfocused) {
            int sleepMs = (idleFpsLimit > 0) ? (int)(1000.0 / idleFpsLimit) : 50;
            SDL_Delay((Uint32)((sleepMs < 1) ? 1 : sleepMs));
            continue;
        }

        bool mainMinimized = (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
        if (mainMinimized && pauseRenderWhenMinimized) {
            int sleepMs = (idleFpsLimit > 0) ? (int)(1000.0 / idleFpsLimit) : 50;
            SDL_Delay((Uint32)((sleepMs < 1) ? 1 : sleepMs));
            continue;
        }

        // Apply viewport toggle changes
        if (enableViewports != lastViewports) {
            if (enableViewports)
                io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            else
                io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

            if (!enableViewports)
                ImGui::DestroyPlatformWindows();

            ImGuiStyle& vstyle = ImGui::GetStyle();
            vstyle.WindowRounding = enableViewports ? 0.0f : 4.0f;
            vstyle.Colors[ImGuiCol_WindowBg].w = 1.0f;
            lastViewports = enableViewports;
        }

        if (enableVsync != lastVsync) {
            SDL_GL_SetSwapInterval(enableVsync ? 1 : 0);
            lastVsync = enableVsync;
        }

        frameStart = SDL_GetPerformanceCounter();

        // --- Frame begin ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        bool allowHeavyRender = true;
        bool anyInput = (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ||
                         io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
                         ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered());
        if (anyInput)
            lastInputCounter = SDL_GetPerformanceCounter();
        if (reduceHeavyWhenIdle) {
            Uint64 nowCounter = SDL_GetPerformanceCounter();
            double idleMs = (double)(nowCounter - lastInputCounter) * 1000.0 / (double)perfFreq;
            if (idleRenderMs > 0 && idleMs > (double)idleRenderMs)
                allowHeavyRender = false;
        }

        // --- Full-window dockspace host ---
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDocking   | ImGuiWindowFlags_NoTitleBar   |
                ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoResize     |
                ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_MenuBar      |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("##DockHost", nullptr, flags);
            ImGui::PopStyleVar(3);
            ImGui::DockSpace(ImGui::GetID("MyuDock"), ImVec2(0, 0),
                             ImGuiDockNodeFlags_PassthruCentralNode);

            // Menu bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Rescan Projects"))
                        projects = listProjects(projectsRoot);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit", "Alt+F4"))
                        running = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Settings")) {
                    if (ImGui::Checkbox("Enable New Window (Viewports)", &enableViewports)) {
                        gLog.info(std::string("Viewports ") +
                                  (enableViewports ? "enabled" : "disabled"));
                    }
                    if (ImGui::Checkbox("VSync", &enableVsync)) {
                        gLog.info(std::string("VSync ") + (enableVsync ? "on" : "off"));
                    }
                    ImGui::Checkbox("Reduce FPS When Unfocused", &reduceFpsWhenUnfocused);
                    ImGui::Checkbox("Pause Render When Unfocused", &pauseRenderWhenUnfocused);
                    ImGui::Checkbox("Pause Render When Minimized", &pauseRenderWhenMinimized);
                    ImGui::Checkbox("Reduce Heavy Panels When Idle", &reduceHeavyWhenIdle);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt("FPS Limit", &fpsLimit, 1, 0, 240);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt("Idle FPS", &idleFpsLimit, 1, 0, 60);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt("Platform FPS", &platformFpsLimit, 1, 0, 240);
                    ImGui::Checkbox("Pause Platform When Minimized", &pausePlatformWhenMinimized);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt("Idle Delay (ms)", &idleRenderMs, 10, 0, 2000);
                    ImGui::TextDisabled("0 = unlimited");

                    ImGui::Separator();
                    ImGui::TextDisabled("Editor Performance");
                    ImGui::Checkbox("Viewport in Scene Tab", &gameEditor.perf.showViewportInSceneTab);
                    ImGui::Checkbox("Viewport in Board Tab", &gameEditor.perf.showViewportInBoardTab);
                    ImGui::Checkbox("Viewport in Preview Tab", &gameEditor.perf.showViewportInPreviewTab);
                    ImGui::Checkbox("Draw Board Grid", &gameEditor.perf.drawBoardGrid);
                    ImGui::Checkbox("Draw Board Labels", &gameEditor.perf.drawBoardLabels);
                    ImGui::Checkbox("Draw Board Pieces", &gameEditor.perf.drawBoardPieces);
                    ImGui::Checkbox("Draw Board Highlights", &gameEditor.perf.drawBoardHighlights);
                    ImGui::Checkbox("Draw Card Preview", &gameEditor.perf.drawCardPreview);
                    ImGui::Checkbox("Viewport Grid", &gameEditor.perf.drawViewportGrid);
                    ImGui::Checkbox("Viewport Pieces", &gameEditor.perf.drawViewportPieces);
                    ImGui::Checkbox("Viewport Shadows", &gameEditor.perf.drawViewportShadows);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragFloat("Viewport Scale", &gameEditor.perf.viewportScale, 1.0f, 10.0f, 120.0f, "%.0f");

                    ImGui::Separator();
                    ImGui::TextDisabled("UI Designer Performance");
                    ImGui::Checkbox("UI Canvas", &uiDesigner.perf.drawCanvas);
                    ImGui::Checkbox("UI Grid", &uiDesigner.perf.drawGrid);
                    ImGui::Checkbox("UI Elements", &uiDesigner.perf.drawElements);
                    ImGui::Checkbox("UI Selection", &uiDesigner.perf.drawSelection);
                    ImGui::Checkbox("UI Overlay", &uiDesigner.perf.drawOverlay);

                    ImGui::Separator();
                    ImGui::TextDisabled("Installer / Data");
                    ImGui::Text("Data Root: %s", dataRoot.string().c_str());
                    ImGui::InputText("Data Root Override", dataRootBuf, sizeof(dataRootBuf));
                    if (ImGui::Button("Apply Data Root")) {
                        std::string err;
                        fs::path newRoot = fs::path(dataRootBuf);
                        if (!newRoot.empty() && writePortableHomeFile(newRoot, err)) {
                            std::string moveErr;
                            moveAppData(dataRoot, newRoot, moveErr);
                            dataRoot = getUserDataDir();
                            projectsRoot = getProjectsRoot();
                            projects = listProjects(projectsRoot);
                            gLog.info("Data root updated: " + dataRoot.string());
                            if (!moveErr.empty()) gLog.warn("Move data warning: " + moveErr);
                        } else {
                            gLog.error("Apply data root failed: " + err);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Override")) {
                        std::string err;
                        if (clearPortableHomeFile(err)) {
                            dataRoot = getUserDataDir();
                            projectsRoot = getProjectsRoot();
                            projects = listProjects(projectsRoot);
                            gLog.info("Data root override cleared");
                        } else {
                            gLog.error("Clear override failed: " + err);
                        }
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Simulate Install");
                    ImGui::InputText("Install Dir", simInstallBuf, sizeof(simInstallBuf));
                    if (ImGui::Button("Simulate Install")) {
                        std::string err;
                        if (simulateInstall(fs::path(simInstallBuf), err))
                            gLog.info("Simulated install to: " + std::string(simInstallBuf));
                        else
                            gLog.error("Simulate install failed: " + err);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("About"))
                        showAbout = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            ImGui::End();
        }

        // --- Panels ---
        bool projectOpenRequested = false;
        drawProjectsPanel(projectsRoot, projects, selectedProject, createWizard, projectOpenRequested);
        drawCreateWizard(createWizard, projectsRoot, projects);

        // Handle Open button press – route to correct editor
        if (projectOpenRequested && selectedProject && editorMode == EditorMode::None) {
            openedProjectName = selectedProject->filename().string();
            std::string tmpl = readProjectMeta(*selectedProject, "template");

            if (tmpl == "Board Card Game") {
                // Open Game Editor
                editorMode = EditorMode::GameEditor;
                gameEditorOpen = true;
                editorDockNeedsInit = true;
                gameEditor = myu::editor::GameEditorState();
                gameEditor.initDefaultScene();
                gLog.info("Game Editor opened for: " + openedProjectName);
            } else {
                // Open UI Designer (for 2D Game, Desktop App, etc.)
                editorMode = EditorMode::UIDesigner;
                uiDesignerOpen = true;
                editorDockNeedsInit = true;
                uiDesigner = myu::ui::DesignerState();
                uiDesigner.savePath = *selectedProject / "ui_layout.json";

                auto existingHtml = *selectedProject / "ui_export.html";
                if (fs::exists(existingHtml)) {
                    myu::ui::loadDesignHTML(uiDesigner, existingHtml);
                    gLog.info("Loaded existing UI design from: " + existingHtml.string());
                }
                gLog.info("UI Designer opened for: " + openedProjectName);
            }
        }

        // Editor host window (opens as a new OS window via viewports)
        if (editorMode != EditorMode::None) {
            bool editorHostOpen = true;
            std::string editorTitle = "MyuEngine - " + openedProjectName + "###EditorHost";
            ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_MenuBar;
            ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Once);
            const ImGuiViewport* mainVp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(
                ImVec2(mainVp->Pos.x + mainVp->Size.x + 40,
                       mainVp->Pos.y + 40), ImGuiCond_Once);
            ImGui::Begin(editorTitle.c_str(), &editorHostOpen, hostFlags);

            // Editor menu bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (editorMode == EditorMode::UIDesigner) {
                        if (ImGui::MenuItem("Save UI Design", "Ctrl+S")) {
                            if (selectedProject) {
                                auto savePath = *selectedProject / "ui_layout.json";
                                myu::ui::saveDesign(uiDesigner, savePath);
                                gLog.info("Saved UI design: " + savePath.string());
                            }
                        }
                        if (ImGui::MenuItem("Export HTML")) {
                            if (selectedProject) {
                                auto htmlPath = *selectedProject / "ui_export.html";
                                std::string html = myu::ui::exportToHTML(uiDesigner.root,
                                    static_cast<int>(uiDesigner.canvasW),
                                    static_cast<int>(uiDesigner.canvasH));
                                std::ofstream f(htmlPath);
                                if (f) { f << html; gLog.info("Exported HTML: " + htmlPath.string()); }
                            }
                        }
                        ImGui::Separator();
                    }
                    if (ImGui::MenuItem("Close Project")) editorHostOpen = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Tools")) {
                    ImGui::MenuItem("Voxel Modeler", nullptr, &showVoxelEditor);
                    ImGui::MenuItem("Blockbench Import", nullptr, &showBlockbench);
                    ImGui::MenuItem("Resources", nullptr, &showResources);
                    ImGui::MenuItem("ECS", nullptr, &showEcs);
                    ImGui::MenuItem("Events", nullptr, &showEvents);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            // Editor dockspace
            ImGuiID editorDockId = ImGui::GetID("EditorDock");
            ImGui::DockSpace(editorDockId, ImVec2(0, 0));

            // First-time layout
            if (editorDockNeedsInit) {
                editorDockNeedsInit = false;
                ImGui::DockBuilderRemoveNode(editorDockId);
                ImGui::DockBuilderAddNode(editorDockId,
                    ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(editorDockId, ImVec2(1260, 680));

                if (editorMode == EditorMode::GameEditor) {
                    ImGuiID left, center, right, bottom;
                    ImGui::DockBuilderSplitNode(editorDockId,
                        ImGuiDir_Left,  0.20f, &left, &center);
                    ImGui::DockBuilderSplitNode(center,
                        ImGuiDir_Right, 0.25f, &right, &center);
                    ImGui::DockBuilderSplitNode(center,
                        ImGuiDir_Down,  0.30f, &bottom, &center);
                    ImGui::DockBuilderDockWindow("Scene",         left);
                    ImGui::DockBuilderDockWindow("Inspector",     right);
                    ImGui::DockBuilderDockWindow("Game Viewport", center);
                    ImGui::DockBuilderDockWindow("Board Editor",  center);
                    ImGui::DockBuilderDockWindow("Card Editor",   center);
                    ImGui::DockBuilderDockWindow("Game Systems",  bottom);
                    ImGui::DockBuilderDockWindow("Game Editor",   bottom);
                } else {
                    ImGuiID left, center, right;
                    ImGui::DockBuilderSplitNode(editorDockId,
                        ImGuiDir_Left,  0.18f, &left, &center);
                    ImGui::DockBuilderSplitNode(center,
                        ImGuiDir_Right, 0.28f, &right, &center);
                    ImGui::DockBuilderDockWindow("Toolbox",    left);
                    ImGui::DockBuilderDockWindow("Canvas",     center);
                    ImGui::DockBuilderDockWindow("Properties", right);
                    ImGui::DockBuilderDockWindow("HTML + CSS", right);
                }
                ImGui::DockBuilderFinish(editorDockId);
            }

            ImGui::End();

            // Draw the editor panels (they dock into EditorDock)
            if (editorMode == EditorMode::UIDesigner && uiDesignerOpen) {
                myu::ui::drawUIDesigner(uiDesigner, allowHeavyRender);
            }
            if (editorMode == EditorMode::GameEditor && gameEditorOpen) {
                myu::editor::drawGameEditor(gameEditor, allowHeavyRender);
            }

            // Voxel modeler
            if (showVoxelEditor)
                myu::editor::drawVoxelEditor(voxelEditor, allowHeavyRender);

            // Blockbench import panel
            if (showBlockbench) {
                ImGui::Begin("Blockbench Import", &showBlockbench);
                static char bbPath[512] = {};
                ImGui::InputText(".bbmodel Path", bbPath, sizeof(bbPath));
                static myu::tools::BlockbenchModelInfo bbInfo;
                if (ImGui::Button("Load")) {
                    if (!myu::tools::loadBlockbenchModelInfo(bbPath, bbInfo))
                        gLog.error("Failed to load Blockbench model");
                    else
                        gLog.info("Loaded Blockbench model: " + bbInfo.name);
                }
                ImGui::Separator();
                ImGui::Text("Name: %s", bbInfo.name.c_str());
                ImGui::Text("Elements: %d", bbInfo.elementCount);
                ImGui::Text("Textures: %d", bbInfo.textureCount);
                ImGui::Text("Resolution: %dx%d", bbInfo.resolutionX, bbInfo.resolutionY);
                ImGui::End();
            }

            // Resources panel
            if (showResources) {
                ImGui::Begin("Resources", &showResources);
                static int rtype = 0;
                static char rname[64] = {};
                static char rpath[256] = {};
                const char* rtypes[] = {"Texture","Model","Material","Audio","Font"};
                ImGui::Combo("Type", &rtype, rtypes, 5);
                ImGui::InputText("Name", rname, sizeof(rname));
                ImGui::InputText("Path", rpath, sizeof(rpath));
                if (ImGui::Button("Add Resource")) {
                    resources.add(static_cast<myu::engine::ResourceType>(rtype), rname, rpath);
                }
                ImGui::Separator();
                for (const auto& e : resources.entries()) {
                    ImGui::BulletText("%s (%s)", e.name.c_str(), e.path.c_str());
                }
                ImGui::End();
            }

            // ECS panel
            if (showEcs) {
                ImGui::Begin("ECS", &showEcs);
                static char ename[64] = "Entity";
                if (ImGui::InputText("New Entity", ename, sizeof(ename), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    ecsWorld.createEntity(ename);
                }
                if (ImGui::Button("Create Entity")) ecsWorld.createEntity(ename);
                ImGui::Separator();
                for (auto e : ecsWorld.entities()) {
                    auto* n = ecsWorld.name(e);
                    auto* t = ecsWorld.tag(e);
                    ImGui::BulletText("%u: %s [%s]", e,
                        n ? n->value.c_str() : "",
                        (t && !t->value.empty()) ? t->value.c_str() : "-");
                }
                ImGui::End();
            }

            // Events panel
            if (showEvents) {
                ImGui::Begin("Events", &showEvents);
                static char ename[64] = "game.tick";
                static char epayload[256] = "{}";
                ImGui::InputText("Name", ename, sizeof(ename));
                ImGui::InputTextMultiline("Payload", epayload, sizeof(epayload), ImVec2(-1, 60));
                if (ImGui::Button("Emit")) eventBus.emit(ename, epayload);
                ImGui::SameLine();
                if (ImGui::Button("Dispatch")) eventBus.dispatch();
                ImGui::SameLine();
                if (ImGui::Button("Clear")) eventLog.clear();
                ImGui::Separator();
                for (const auto& e : eventLog) {
                    ImGui::BulletText("%s: %s", e.name.c_str(), e.payload.c_str());
                }
                ImGui::End();
            }

            // Handle host window close (X button or Close menu)
            if (!editorHostOpen) {
                editorMode = EditorMode::None;
                uiDesignerOpen = false;
                gameEditorOpen = false;
                openedProjectName.clear();
            }
        }

        drawLogPanel(gLog);

        // --- About popup ---
        if (showAbout) {
            ImGui::OpenPopup("About MyuEngine");
            showAbout = false;
        }
        if (ImGui::BeginPopupModal("About MyuEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("MyuEngine v0.1.0");
            ImGui::Text("SDL2 + OpenGL 3.3 + Dear ImGui");
            ImGui::Text("Built with MyuDoglibs-Cpp");
            ImGui::Separator();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // --- Render ---
        ImGui::Render();
        int w, h;
        SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Multi-viewport: render platform windows (new OS windows)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            Uint64 nowCounter = SDL_GetPerformanceCounter();
            bool renderPlatform = true;
            if (platformFpsLimit > 0) {
                double interval = (double)perfFreq / (double)platformFpsLimit;
                if (lastPlatformRender != 0 &&
                    (double)(nowCounter - lastPlatformRender) < interval) {
                    renderPlatform = false;
                } else {
                    lastPlatformRender = nowCounter;
                }
            }

            if (pausePlatformWhenMinimized) {
                bool anyVisible = false;
                ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
                for (ImGuiViewport* vp : pio.Viewports) {
                    if ((vp->Flags & ImGuiViewportFlags_IsMinimized) == 0) {
                        anyVisible = true;
                        break;
                    }
                }
                if (!anyVisible)
                    renderPlatform = false;
            }

            if (renderPlatform) {
                SDL_Window* backup_window  = SDL_GL_GetCurrentWindow();
                SDL_GLContext backup_context = SDL_GL_GetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                SDL_GL_MakeCurrent(backup_window, backup_context);
            }
        }

        SDL_GL_SwapWindow(window);

        // Frame pacing (simple FPS cap)
        int effectiveFps = fpsLimit;
        if (reduceFpsWhenUnfocused) {
            if (!appFocused && idleFpsLimit > 0)
                effectiveFps = idleFpsLimit;
        }
        if (effectiveFps > 0) {
            Uint64 frameEnd = SDL_GetPerformanceCounter();
            double frameMs = (double)(frameEnd - frameStart) * 1000.0 / (double)perfFreq;
            double targetMs = 1000.0 / (double)effectiveFps;
            if (frameMs < targetMs) {
                SDL_Delay((Uint32)(targetMs - frameMs));
            }
        }
    }

    // --- Cleanup ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();

    gLog.info("MyuEngine editor shut down");
    return 0;
}
