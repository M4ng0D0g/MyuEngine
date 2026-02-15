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
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
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
// UI localization – loaded from .lang files
// ============================================================================
enum class UiLang { ZhTW, EnUS };
static UiLang gUiLang = UiLang::ZhTW;

// Map: English key → translated string
static std::unordered_map<std::string, std::string> gLangMap;

// Resolve the lang/ directory relative to the executable
static fs::path getLangDir() {
    // Try a few common locations:
    //   1) <exe_dir>/../../lang        (running from build/bin/)
    //   2) <exe_dir>/../lang           (installed layout)
    //   3) <exe_dir>/lang
    //   4) ./lang
    auto tryDir = [](const fs::path& p) -> fs::path {
        std::error_code ec;
        if (fs::is_directory(p, ec)) return p;
        return {};
    };

    // SDL provides the executable base path
    char* basePath = SDL_GetBasePath();
    fs::path base = basePath ? fs::path(basePath) : fs::current_path();
    if (basePath) SDL_free(basePath);

    for (auto& candidate : {
            base / ".." / ".." / "lang",
            base / ".." / "lang",
            base / "lang",
            fs::path("lang")
         }) {
        auto d = tryDir(candidate);
        if (!d.empty()) return fs::canonical(d);
    }
    return fs::path("lang"); // fallback
}

static bool loadLangFile(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return false;
    gLangMap.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue; // skip comments
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        gLangMap[key] = value;
    }
    return true;
}

static void loadLanguage(UiLang lang) {
    gUiLang = lang;
    gLangMap.clear();
    if (lang == UiLang::EnUS) return; // English: just use the key itself

    const char* filename = "zh_TW.lang";
    fs::path langDir = getLangDir();
    fs::path langFile = langDir / filename;

    if (!loadLangFile(langFile)) {
        // Log to stdout since gLog may not exist yet
        std::fprintf(stderr, "[i18n] Failed to load language file: %s\n",
                     langFile.string().c_str());
    }
}

// Translation lookup – returns the English key if no entry found
static const char* tr(const char* en) {
    if (gUiLang == UiLang::EnUS) return en;
    auto it = gLangMap.find(en);
    if (it != gLangMap.end()) return it->second.c_str();
    return en;
}

static std::string makeWindowTitle(const char* label, const char* id) {
    std::string title = label;
    title += "###";
    title += id;
    return title;
}

// ============================================================================
// Project language
// ============================================================================
enum class ProjectLang { Cpp, Web, Java, Python, kCount };
static constexpr int kLangCount = static_cast<int>(ProjectLang::kCount);
static const char* kLangLabels[] = { "C++ (Native)", "Web (HTML/JS)", "Java", "Python" };
static const char* kLangIds[]    = { "cpp",          "web",           "java", "python" };

// ============================================================================
// Build runner – async compile / run with output capture
// ============================================================================
struct BuildRunner {
    std::atomic<bool> running{false};
    std::atomic<bool> wantStop{false};
    std::thread       thread;
    std::mutex        mtx;
    std::string       pendingOutput;
    int               exitCode = 0;
    bool              finished = false;

    void start(const std::string& cmd, const std::string& workDir) {
        if (running) return;
        running   = true;
        wantStop  = false;
        finished  = false;
        exitCode  = 0;
        {
            std::lock_guard<std::mutex> lk(mtx);
            pendingOutput.clear();
        }
        thread = std::thread([this, cmd, workDir]() {
            runImpl(cmd, workDir);
        });
        thread.detach();
    }

    void stop() { wantStop = true; }

    // Drain captured output into LogBuffer (call from main thread)
    void drain(LogBuffer& log) {
        std::lock_guard<std::mutex> lk(mtx);
        if (!pendingOutput.empty()) {
            // Split by lines
            std::istringstream iss(pendingOutput);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.find("error") != std::string::npos || line.find("Error") != std::string::npos)
                    log.error(line);
                else if (line.find("warning") != std::string::npos || line.find("Warning") != std::string::npos)
                    log.warn(line);
                else
                    log.info(line);
            }
            pendingOutput.clear();
        }
    }

private:
    void runImpl(const std::string& cmd, const std::string& workDir) {
        std::string fullCmd = "cd " + workDir + " && " + cmd + " 2>&1";
        FILE* fp = popen(fullCmd.c_str(), "r");
        if (!fp) {
            std::lock_guard<std::mutex> lk(mtx);
            pendingOutput += "[BuildRunner] Failed to start process\n";
            running = false;
            finished = true;
            exitCode = -1;
            return;
        }
        char buf[256];
        while (fgets(buf, sizeof(buf), fp) && !wantStop) {
            std::lock_guard<std::mutex> lk(mtx);
            pendingOutput += buf;
        }
        int rc = pclose(fp);
#ifdef _WIN32
        exitCode = rc;
#else
        exitCode = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
        running  = false;
        finished = true;
    }
};

static BuildRunner gBuild;

// ============================================================================
// Flow chart data model
// ============================================================================
enum class FlowNodeType { Start, End, Process, Decision, Class, Function, IO, Loop };

struct FlowNode {
    int             id = 0;
    FlowNodeType    type = FlowNodeType::Process;
    char            label[64]   = "Node";
    char            content[256] = "";       // code / description
    char            condition[128] = "";     // for Decision
    char            returnType[32] = "void"; // for Function
    char            params[128] = "";        // for Function
    char            members[256] = "";       // for Class (one per line)
    char            methods[256] = "";       // for Class
    char            parentClass[64] = "";    // for Class inheritance
    ImVec2          pos = {0, 0};
    ImVec2          size = {160, 80};
    bool            selected = false;
};

struct FlowConnection {
    int fromNode = -1;
    int toNode   = -1;
    const char* label = nullptr; // "Yes", "No", "" , etc.
};

struct FlowChartState {
    std::vector<FlowNode>       nodes;
    std::vector<FlowConnection> connections;
    int  nextId = 1;
    int  selectedNode = -1;
    int  connectFrom  = -1;       // node id being connected from
    bool showCodeWindow = false;
    std::string generatedCode;
    ImVec2 scrolling = {0, 0};

    int addNode(FlowNodeType type, ImVec2 pos) {
        FlowNode n;
        n.id   = nextId++;
        n.type = type;
        n.pos  = pos;
        switch (type) {
            case FlowNodeType::Start:    std::strncpy(n.label, "Start", 64); n.size = {100, 50}; break;
            case FlowNodeType::End:      std::strncpy(n.label, "End",   64); n.size = {100, 50}; break;
            case FlowNodeType::Process:  std::strncpy(n.label, "Process", 64); break;
            case FlowNodeType::Decision: std::strncpy(n.label, "Decision", 64); std::strncpy(n.condition, "x > 0", 128); break;
            case FlowNodeType::Class:    std::strncpy(n.label, "MyClass", 64); n.size = {180, 120}; break;
            case FlowNodeType::Function: std::strncpy(n.label, "myFunc", 64); break;
            case FlowNodeType::IO:       std::strncpy(n.label, "I/O", 64); break;
            case FlowNodeType::Loop:     std::strncpy(n.label, "Loop", 64); std::strncpy(n.condition, "i < n", 128); break;
        }
        nodes.push_back(n);
        return n.id;
    }

    void removeNode(int id) {
        connections.erase(
            std::remove_if(connections.begin(), connections.end(),
                [id](const FlowConnection& c) { return c.fromNode == id || c.toNode == id; }),
            connections.end());
        nodes.erase(
            std::remove_if(nodes.begin(), nodes.end(),
                [id](const FlowNode& n) { return n.id == id; }),
            nodes.end());
        if (selectedNode == id) selectedNode = -1;
    }

    FlowNode* findNode(int id) {
        for (auto& n : nodes) if (n.id == id) return &n;
        return nullptr;
    }

    // Save flow chart to a simple text file
    bool save(const fs::path& path) const {
        std::ofstream f(path);
        if (!f) return false;
        f << "FLOWCHART 1\n";
        f << "nodes " << nodes.size() << "\n";
        for (const auto& n : nodes) {
            f << "N " << n.id << " " << static_cast<int>(n.type)
              << " " << n.pos.x << " " << n.pos.y
              << " " << n.size.x << " " << n.size.y << "\n";
            f << "L " << n.label << "\n";
            f << "C " << n.content << "\n";
            f << "D " << n.condition << "\n";
            f << "R " << n.returnType << "\n";
            f << "P " << n.params << "\n";
            f << "M " << n.members << "\n";
            f << "T " << n.methods << "\n";
            f << "I " << n.parentClass << "\n";
        }
        f << "connections " << connections.size() << "\n";
        for (const auto& c : connections) {
            f << "E " << c.fromNode << " " << c.toNode << "\n";
        }
        f << "nextId " << nextId << "\n";
        return true;
    }

    // Load flow chart from file
    bool load(const fs::path& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        if (!std::getline(f, line) || line.find("FLOWCHART") == std::string::npos)
            return false;

        nodes.clear();
        connections.clear();
        selectedNode = -1;
        connectFrom = -1;

        int nodeCount = 0;
        if (std::getline(f, line))
            std::sscanf(line.c_str(), "nodes %d", &nodeCount);

        for (int i = 0; i < nodeCount; ++i) {
            FlowNode n;
            int typeInt = 0;
            if (std::getline(f, line))
                std::sscanf(line.c_str(), "N %d %d %f %f %f %f",
                    &n.id, &typeInt, &n.pos.x, &n.pos.y, &n.size.x, &n.size.y);
            n.type = static_cast<FlowNodeType>(typeInt);
            auto readField = [&](char* buf, int sz) {
                if (std::getline(f, line) && line.size() > 2)
                    std::strncpy(buf, line.c_str() + 2, sz - 1);
                buf[sz - 1] = '\0';
            };
            readField(n.label, sizeof(n.label));
            readField(n.content, sizeof(n.content));
            readField(n.condition, sizeof(n.condition));
            readField(n.returnType, sizeof(n.returnType));
            readField(n.params, sizeof(n.params));
            readField(n.members, sizeof(n.members));
            readField(n.methods, sizeof(n.methods));
            readField(n.parentClass, sizeof(n.parentClass));
            nodes.push_back(n);
        }

        int connCount = 0;
        if (std::getline(f, line))
            std::sscanf(line.c_str(), "connections %d", &connCount);
        for (int i = 0; i < connCount; ++i) {
            FlowConnection c;
            if (std::getline(f, line))
                std::sscanf(line.c_str(), "E %d %d", &c.fromNode, &c.toNode);
            connections.push_back(c);
        }

        if (std::getline(f, line))
            std::sscanf(line.c_str(), "nextId %d", &nextId);

        return true;
    }
};

// ============================================================================
// Flow chart → code generation
// ============================================================================
static std::string generateFlowCode(const FlowChartState& fc, ProjectLang lang) {
    std::ostringstream out;
    bool isCpp  = (lang == ProjectLang::Cpp);
    bool isJava = (lang == ProjectLang::Java);
    bool isPy   = (lang == ProjectLang::Python);
    bool isWeb  = (lang == ProjectLang::Web);

    if (isCpp) {
        out << "// Auto-generated from Flow Chart\n#include <iostream>\n\n";
    } else if (isJava) {
        out << "// Auto-generated from Flow Chart\n\n";
    } else if (isPy) {
        out << "# Auto-generated from Flow Chart\n\n";
    } else {
        out << "// Auto-generated from Flow Chart\n\n";
    }

    // Emit classes first
    for (const auto& n : fc.nodes) {
        if (n.type != FlowNodeType::Class) continue;
        if (isCpp) {
            out << "class " << n.label;
            if (n.parentClass[0]) out << " : public " << n.parentClass;
            out << " {\npublic:\n";
            // Members
            std::istringstream ms(n.members);
            std::string line;
            while (std::getline(ms, line)) { if (!line.empty()) out << "    " << line << ";\n"; }
            out << "\n";
            // Methods
            std::istringstream mt(n.methods);
            while (std::getline(mt, line)) { if (!line.empty()) out << "    " << line << " {}\n"; }
            out << "};\n\n";
        } else if (isJava) {
            out << "class " << n.label;
            if (n.parentClass[0]) out << " extends " << n.parentClass;
            out << " {\n";
            std::istringstream ms(n.members);
            std::string line;
            while (std::getline(ms, line)) { if (!line.empty()) out << "    " << line << ";\n"; }
            std::istringstream mt(n.methods);
            while (std::getline(mt, line)) { if (!line.empty()) out << "    " << line << " {}\n"; }
            out << "}\n\n";
        } else if (isPy) {
            out << "class " << n.label;
            if (n.parentClass[0]) out << "(" << n.parentClass << ")";
            out << ":\n";
            std::istringstream ms(n.members);
            std::string line;
            bool hasBody = false;
            out << "    def __init__(self):\n";
            while (std::getline(ms, line)) {
                if (!line.empty()) { out << "        self." << line << "\n"; hasBody = true; }
            }
            if (!hasBody) out << "        pass\n";
            std::istringstream mt(n.methods);
            while (std::getline(mt, line)) { if (!line.empty()) out << "\n    def " << line << "(self):\n        pass\n"; }
            out << "\n";
        } else { // JS
            out << "class " << n.label;
            if (n.parentClass[0]) out << " extends " << n.parentClass;
            out << " {\n    constructor() {\n";
            if (n.parentClass[0]) out << "        super();\n";
            std::istringstream ms(n.members);
            std::string line;
            while (std::getline(ms, line)) { if (!line.empty()) out << "        this." << line << ";\n"; }
            out << "    }\n";
            std::istringstream mt(n.methods);
            while (std::getline(mt, line)) { if (!line.empty()) out << "    " << line << "() {}\n"; }
            out << "}\n\n";
        }
    }

    // Emit functions
    for (const auto& n : fc.nodes) {
        if (n.type != FlowNodeType::Function) continue;
        if (isCpp) {
            out << n.returnType << " " << n.label << "(" << n.params << ") {\n";
            if (n.content[0]) out << "    " << n.content << "\n";
            out << "}\n\n";
        } else if (isJava) {
            out << "static " << n.returnType << " " << n.label << "(" << n.params << ") {\n";
            if (n.content[0]) out << "    " << n.content << "\n";
            out << "}\n\n";
        } else if (isPy) {
            out << "def " << n.label << "(" << n.params << "):\n";
            if (n.content[0]) out << "    " << n.content << "\n";
            else out << "    pass\n";
            out << "\n";
        } else {
            out << "function " << n.label << "(" << n.params << ") {\n";
            if (n.content[0]) out << "    " << n.content << "\n";
            out << "}\n\n";
        }
    }

    // Emit main-body flow: Process, Decision, Loop, IO  (sequential order top to bottom)
    bool hasMain = false;
    for (const auto& n : fc.nodes) {
        if (n.type == FlowNodeType::Start) { hasMain = true; break; }
    }
    if (hasMain) {
        if (isCpp) out << "int main() {\n";
        else if (isJava) out << "public static void main(String[] args) {\n";
        else if (isPy) out << "if __name__ == '__main__':\n";
        else out << "(function main() {\n";

        std::string indent = (isPy) ? "    " : "    ";

        // Walk the nodes in order (by vertical position)
        std::vector<const FlowNode*> sorted;
        for (const auto& n : fc.nodes) {
            if (n.type == FlowNodeType::Class || n.type == FlowNodeType::Function) continue;
            sorted.push_back(&n);
        }
        std::sort(sorted.begin(), sorted.end(),
            [](const FlowNode* a, const FlowNode* b) { return a->pos.y < b->pos.y; });

        for (const auto* n : sorted) {
            switch (n->type) {
            case FlowNodeType::Process:
                if (n->content[0])
                    out << indent << n->content << (isPy ? "\n" : ";\n");
                else
                    out << indent << "// " << n->label << "\n";
                break;
            case FlowNodeType::Decision:
                out << indent << (isPy ? "if " : "if (") << n->condition << (isPy ? ":\n" : ") {\n");
                out << indent << indent << "// " << tr("True Branch") << "\n";
                if (!isPy) out << indent << "} else {\n";
                else       out << indent << "else:\n";
                out << indent << indent << "// " << tr("False Branch") << "\n";
                if (!isPy) out << indent << "}\n";
                break;
            case FlowNodeType::Loop:
                if (isCpp || isJava)
                    out << indent << "while (" << n->condition << ") {\n";
                else if (isPy)
                    out << indent << "while " << n->condition << ":\n";
                else
                    out << indent << "while (" << n->condition << ") {\n";
                out << indent << indent << "// " << n->label << "\n";
                if (!isPy) out << indent << "}\n";
                break;
            case FlowNodeType::IO:
                if (isCpp)
                    out << indent << "std::cout << " << n->label << " << std::endl;\n";
                else if (isJava)
                    out << indent << "System.out.println(" << n->label << ");\n";
                else if (isPy)
                    out << indent << "print(" << n->label << ")\n";
                else
                    out << indent << "console.log(" << n->label << ");\n";
                break;
            default: break;
            }
        }

        if (isCpp) out << "    return 0;\n}\n";
        else if (isJava) out << "}\n";
        else if (isPy) { /* pass */ }
        else out << "})();\n";
    }

    return out.str();
}

// ============================================================================
// Draw flow chart designer
// ============================================================================
static void drawFlowChart(FlowChartState& fc, ProjectLang lang) {
    // --- Toolbar ---
    if (ImGui::Button(tr("Add Node"))) ImGui::OpenPopup("##AddNodePopup");
    if (ImGui::BeginPopup("##AddNodePopup")) {
        ImVec2 center = { fc.scrolling.x + 300, fc.scrolling.y + 200 };
        struct { const char* label; FlowNodeType type; } items[] = {
            { tr("Start"),     FlowNodeType::Start },
            { tr("End"),       FlowNodeType::End },
            { tr("Process"),   FlowNodeType::Process },
            { tr("Decision"),  FlowNodeType::Decision },
            { tr("Function"),  FlowNodeType::Function },
            { tr("Class"),     FlowNodeType::Class },
            { tr("Input/Output"), FlowNodeType::IO },
            { tr("Loop"),      FlowNodeType::Loop },
        };
        for (auto& it : items) {
            if (ImGui::MenuItem(it.label))
                fc.addNode(it.type, center);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Generate Code"))) {
        fc.generatedCode = generateFlowCode(fc, lang);
        fc.showCodeWindow = true;
    }
    if (fc.connectFrom >= 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "[%s: %d → ???]",
            tr("Connect"), fc.connectFrom);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Cancel"))) fc.connectFrom = -1;
    }

    ImGui::Separator();

    // --- Canvas area ---
    ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    ImVec2 canvasSz = ImGui::GetContentRegionAvail();
    if (canvasSz.x < 50) canvasSz.x = 50;
    if (canvasSz.y < 50) canvasSz.y = 50;
    ImVec2 canvasP1 = { canvasP0.x + canvasSz.x, canvasP0.y + canvasSz.y };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasP0, canvasP1, IM_COL32(30, 30, 36, 255));

    // Grid
    const float gridStep = 40.0f;
    for (float x = fmodf(fc.scrolling.x, gridStep); x < canvasSz.x; x += gridStep)
        dl->AddLine({canvasP0.x + x, canvasP0.y}, {canvasP0.x + x, canvasP1.y}, IM_COL32(50, 50, 60, 100));
    for (float y = fmodf(fc.scrolling.y, gridStep); y < canvasSz.y; y += gridStep)
        dl->AddLine({canvasP0.x, canvasP0.y + y}, {canvasP1.x, canvasP0.y + y}, IM_COL32(50, 50, 60, 100));

    ImGui::InvisibleButton("##fccanvas", canvasSz,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool canvasHovered = ImGui::IsItemHovered();

    // Pan with right mouse
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        fc.scrolling.x += ImGui::GetIO().MouseDelta.x;
        fc.scrolling.y += ImGui::GetIO().MouseDelta.y;
    }

    ImVec2 offset = { canvasP0.x + fc.scrolling.x, canvasP0.y + fc.scrolling.y };

    dl->PushClipRect(canvasP0, canvasP1, true);

    // Draw connections
    for (const auto& c : fc.connections) {
        const FlowNode* from = nullptr;
        const FlowNode* to = nullptr;
        for (const auto& n : fc.nodes) {
            if (n.id == c.fromNode) from = &n;
            if (n.id == c.toNode)   to   = &n;
        }
        if (from && to) {
            ImVec2 p1 = { offset.x + from->pos.x + from->size.x * 0.5f,
                          offset.y + from->pos.y + from->size.y };
            ImVec2 p2 = { offset.x + to->pos.x + to->size.x * 0.5f,
                          offset.y + to->pos.y };
            // Bezier curve
            ImVec2 cp1 = { p1.x, p1.y + 40 };
            ImVec2 cp2 = { p2.x, p2.y - 40 };
            dl->AddBezierCubic(p1, cp1, cp2, p2, IM_COL32(120, 200, 255, 200), 2.0f);
            // Arrow head
            ImVec2 dir = { p2.x - cp2.x, p2.y - cp2.y };
            float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
            if (len > 0) { dir.x /= len; dir.y /= len; }
            ImVec2 perp = { -dir.y, dir.x };
            float arrowSz = 8.0f;
            dl->AddTriangleFilled(
                p2,
                { p2.x - dir.x * arrowSz + perp.x * arrowSz * 0.5f,
                  p2.y - dir.y * arrowSz + perp.y * arrowSz * 0.5f },
                { p2.x - dir.x * arrowSz - perp.x * arrowSz * 0.5f,
                  p2.y - dir.y * arrowSz - perp.y * arrowSz * 0.5f },
                IM_COL32(120, 200, 255, 220));
            // Label
            if (c.label && c.label[0]) {
                ImVec2 mid = { (p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f };
                dl->AddText(mid, IM_COL32(200, 200, 100, 255), c.label);
            }
        }
    }

    // Draw nodes
    for (auto& n : fc.nodes) {
        ImVec2 nodeP0 = { offset.x + n.pos.x, offset.y + n.pos.y };
        ImVec2 nodeP1 = { nodeP0.x + n.size.x, nodeP0.y + n.size.y };

        ImU32 bgCol, borderCol;
        switch (n.type) {
            case FlowNodeType::Start:    bgCol = IM_COL32(40, 130, 60, 220);  break;
            case FlowNodeType::End:      bgCol = IM_COL32(150, 40, 40, 220);  break;
            case FlowNodeType::Process:  bgCol = IM_COL32(50, 80, 140, 220);  break;
            case FlowNodeType::Decision: bgCol = IM_COL32(160, 120, 30, 220); break;
            case FlowNodeType::Class:    bgCol = IM_COL32(100, 50, 150, 220); break;
            case FlowNodeType::Function: bgCol = IM_COL32(40, 120, 130, 220); break;
            case FlowNodeType::IO:       bgCol = IM_COL32(130, 80, 50, 220);  break;
            case FlowNodeType::Loop:     bgCol = IM_COL32(80, 80, 160, 220);  break;
            default:                     bgCol = IM_COL32(60, 60, 70, 220);   break;
        }
        borderCol = (fc.selectedNode == n.id) ? IM_COL32(255, 220, 60, 255) : IM_COL32(140, 140, 160, 180);

        // Shape varies by type
        if (n.type == FlowNodeType::Decision) {
            // Diamond shape
            ImVec2 center = { (nodeP0.x + nodeP1.x) * 0.5f, (nodeP0.y + nodeP1.y) * 0.5f };
            float hw = n.size.x * 0.5f, hh = n.size.y * 0.5f;
            ImVec2 pts[4] = {
                { center.x, center.y - hh },
                { center.x + hw, center.y },
                { center.x, center.y + hh },
                { center.x - hw, center.y },
            };
            dl->AddConvexPolyFilled(pts, 4, bgCol);
            dl->AddPolyline(pts, 4, borderCol, ImDrawFlags_Closed, 2.0f);
        } else if (n.type == FlowNodeType::Start || n.type == FlowNodeType::End) {
            // Rounded rectangle (pill)
            dl->AddRectFilled(nodeP0, nodeP1, bgCol, n.size.y * 0.5f);
            dl->AddRect(nodeP0, nodeP1, borderCol, n.size.y * 0.5f, 0, 2.0f);
        } else if (n.type == FlowNodeType::IO) {
            // Parallelogram
            float skew = 15.0f;
            ImVec2 pts[4] = {
                { nodeP0.x + skew, nodeP0.y },
                { nodeP1.x,        nodeP0.y },
                { nodeP1.x - skew, nodeP1.y },
                { nodeP0.x,        nodeP1.y },
            };
            dl->AddConvexPolyFilled(pts, 4, bgCol);
            dl->AddPolyline(pts, 4, borderCol, ImDrawFlags_Closed, 2.0f);
        } else {
            dl->AddRectFilled(nodeP0, nodeP1, bgCol, 6.0f);
            dl->AddRect(nodeP0, nodeP1, borderCol, 6.0f, 0, 2.0f);
        }

        // Node label
        ImVec2 textSz = ImGui::CalcTextSize(n.label);
        ImVec2 textPos = { (nodeP0.x + nodeP1.x - textSz.x) * 0.5f,
                           (nodeP0.y + nodeP1.y - textSz.y) * 0.5f };
        dl->AddText(textPos, IM_COL32(255, 255, 255, 255), n.label);

        // Node interaction
        ImGui::SetCursorScreenPos(nodeP0);
        ImGui::InvisibleButton(("##node" + std::to_string(n.id)).c_str(), n.size);
        bool nodeHovered = ImGui::IsItemHovered();
        bool nodeActive  = ImGui::IsItemActive();

        if (nodeHovered && ImGui::IsMouseClicked(0)) {
            if (fc.connectFrom >= 0 && fc.connectFrom != n.id) {
                // Finish connection
                fc.connections.push_back({fc.connectFrom, n.id, ""});
                fc.connectFrom = -1;
            } else {
                fc.selectedNode = n.id;
            }
        }
        // Drag node
        if (nodeActive && ImGui::IsMouseDragging(0)) {
            n.pos.x += ImGui::GetIO().MouseDelta.x;
            n.pos.y += ImGui::GetIO().MouseDelta.y;
        }

        // Right-click context menu
        if (nodeHovered && ImGui::IsMouseClicked(1)) {
            fc.selectedNode = n.id;
            ImGui::OpenPopup(("##nodeCtx" + std::to_string(n.id)).c_str());
        }
        if (ImGui::BeginPopup(("##nodeCtx" + std::to_string(n.id)).c_str())) {
            if (ImGui::MenuItem(tr("Connect")))      fc.connectFrom = n.id;
            if (ImGui::MenuItem(tr("Delete Node")))   fc.removeNode(n.id);
            ImGui::EndPopup();
        }
    }

    dl->PopClipRect();

    // --- Generated code window ---
    if (fc.showCodeWindow) {
        std::string codeTitle = makeWindowTitle(tr("Generated Code"), "GenCode");
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Once);
        if (ImGui::Begin(codeTitle.c_str(), &fc.showCodeWindow)) {
            if (ImGui::Button(tr("Copy"))) {
                ImGui::SetClipboardText(fc.generatedCode.c_str());
            }
            ImGui::Separator();
            ImGui::InputTextMultiline("##gencode", &fc.generatedCode[0],
                fc.generatedCode.size() + 1, ImVec2(-1, -1),
                ImGuiInputTextFlags_ReadOnly);
        }
        ImGui::End();
    }
}

// ============================================================================
// Draw flow chart properties panel (selected node)
// ============================================================================
static void drawFlowProperties(FlowChartState& fc) {
    if (fc.selectedNode < 0) {
        ImGui::TextDisabled("(%s)", tr("No selection"));
        return;
    }
    FlowNode* n = fc.findNode(fc.selectedNode);
    if (!n) { fc.selectedNode = -1; return; }

    ImGui::Text("%s #%d", tr("Node Properties"), n->id);
    ImGui::Separator();

    ImGui::InputText(tr("Label"), n->label, sizeof(n->label));

    const char* typeNames[] = {"Start", "End", "Process", "Decision", "Class", "Function", "I/O", "Loop"};
    int typeIdx = static_cast<int>(n->type);
    ImGui::Text("%s: %s", tr("Type"), typeNames[typeIdx]);

    if (n->type == FlowNodeType::Process || n->type == FlowNodeType::IO) {
        ImGui::InputTextMultiline(tr("Content"), n->content, sizeof(n->content), ImVec2(-1, 60));
    }
    if (n->type == FlowNodeType::Decision || n->type == FlowNodeType::Loop) {
        ImGui::InputText(tr("Condition"), n->condition, sizeof(n->condition));
    }
    if (n->type == FlowNodeType::Function) {
        ImGui::InputText(tr("Return Type"), n->returnType, sizeof(n->returnType));
        ImGui::InputText(tr("Parameters"), n->params, sizeof(n->params));
        ImGui::InputTextMultiline(tr("Content"), n->content, sizeof(n->content), ImVec2(-1, 60));
    }
    if (n->type == FlowNodeType::Class) {
        ImGui::InputText(tr("Parent Class"), n->parentClass, sizeof(n->parentClass));
        ImGui::InputTextMultiline(tr("Members"), n->members, sizeof(n->members), ImVec2(-1, 60));
        ImGui::InputTextMultiline(tr("Methods"), n->methods, sizeof(n->methods), ImVec2(-1, 60));
    }

    ImGui::Separator();
    if (ImGui::Button(tr("Delete Node"))) {
        fc.removeNode(fc.selectedNode);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Connect"))) {
        fc.connectFrom = fc.selectedNode;
    }

    // Show connections
    ImGui::Separator();
    ImGui::Text("%s:", tr("Connections"));
    for (int i = 0; i < (int)fc.connections.size(); ++i) {
        auto& c = fc.connections[i];
        if (c.fromNode == fc.selectedNode || c.toNode == fc.selectedNode) {
            ImGui::BulletText("%d -> %d", c.fromNode, c.toNode);
            ImGui::SameLine();
            ImGui::PushID(i);
            if (ImGui::SmallButton("x")) {
                fc.connections.erase(fc.connections.begin() + i);
                --i;
            }
            ImGui::PopID();
        }
    }
}
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

static void scaffoldTemplate(ProjectTemplate tmpl, const fs::path& dir) {
    switch (tmpl) {
        case ProjectTemplate::Game2D:        scaffoldGame2D(dir);        break;
        case ProjectTemplate::Game3D:        scaffoldGame3D(dir);        break;
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
                           ProjectTemplate tmpl, ProjectLang lang, std::string& err)
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
         << "language=" << kLangIds[static_cast<int>(lang)] << "\n"
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
    std::string title = makeWindowTitle(tr("Log Console"), "LogConsole");
    ImGui::Begin(title.c_str());

    if (ImGui::Button(tr("Clear"))) log.entries.clear();
    ImGui::SameLine();
    ImGui::Checkbox(tr("Auto-scroll"), &log.autoScroll);
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

static void drawHelpPanel(bool* open) {
    std::string title = makeWindowTitle(tr("Usage Guide"), "Help");
    if (!ImGui::Begin(title.c_str(), open)) {
        ImGui::End();
        return;
    }

    // --- Quick Start ---
    ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "%s", tr("Quick Start"));
    ImGui::Separator();
    ImGui::BulletText("%s", tr("help.quickstart.1"));
    ImGui::BulletText("%s", tr("help.quickstart.2"));
    ImGui::BulletText("%s", tr("help.quickstart.3"));
    ImGui::BulletText("%s", tr("help.quickstart.4"));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "%s", tr("UI Designer"));
    ImGui::Separator();
    ImGui::BulletText("%s", tr("help.uid.1"));
    ImGui::BulletText("%s", tr("help.uid.2"));
    ImGui::BulletText("%s", tr("help.uid.3"));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "Game Editor");
    ImGui::Separator();
    ImGui::BulletText("%s", tr("help.game.1"));
    ImGui::BulletText("%s", tr("help.game.2"));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "%s", tr("Flow Chart"));
    ImGui::Separator();
    ImGui::BulletText("%s", tr("help.flow.1"));
    ImGui::BulletText("%s", tr("help.flow.2"));
    ImGui::BulletText("%s", tr("help.flow.3"));
    ImGui::BulletText("%s", tr("help.flow.4"));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "%s", tr("Build & Run"));
    ImGui::Separator();
    ImGui::BulletText("%s", tr("help.build.1"));
    ImGui::BulletText("%s", tr("help.build.2"));
    ImGui::BulletText("%s", tr("help.build.3"));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "%s", tr("Tool Panels"));
    ImGui::Separator();
    ImGui::BulletText("%s", tr("help.tools.1"));
    ImGui::BulletText("%s", tr("help.tools.2"));
    ImGui::BulletText("%s", tr("help.tools.3"));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "%s", tr("Data & Maintenance"));
    ImGui::Separator();
    ImGui::BulletText("%s", tr("help.data.1"));
    ImGui::BulletText("%s", tr("help.data.2"));
    ImGui::BulletText("%s", tr("help.data.3"));

    ImGui::End();
}

// ============================================================================
// Create-project wizard state
// ============================================================================
struct CreateWizard {
    bool              open = false;
    char              name[64] = {};
    int               selectedTemplate = 0;
    int               selectedLang = 0;  // ProjectLang index
    std::string       errorMsg;

    void reset() {
        open = false;
        name[0] = '\0';
        selectedTemplate = 0;
        selectedLang = 0;
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
    std::string title = makeWindowTitle(tr("Create New Project"), "CreateProject");
    if (!ImGui::Begin(title.c_str(), &wiz.open)) {
        ImGui::End();
        return;
    }

    // --- Project name ---
    ImGui::Text("%s", tr("Project Name"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##wizname", wiz.name, sizeof(wiz.name));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Template selection ---
    ImGui::Text("%s", tr("Select Template"));
    ImGui::Spacing();

    ImGui::BeginChild("##tmpllist", ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() * 3),
                      ImGuiChildFlags_Border);
    for (int i = 0; i < kTemplateCount; ++i) {
        auto& t = kTemplates[i];
        ImGui::PushID(i);
        bool sel = (wiz.selectedTemplate == i);

        // Selectable card
        char label[128];
        std::snprintf(label, sizeof(label), "%s  %s", t.icon, tr(t.label));
        if (ImGui::Selectable(label, sel, 0, ImVec2(0, 40))) {
            wiz.selectedTemplate = i;
        }
        // Description tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(t.description));
        }
        // Inline description when selected
        if (sel) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220);
            ImGui::TextDisabled("%s", tr(t.description));
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    // --- Language selection ---
    ImGui::Spacing();
    ImGui::Text("%s", tr("Project Language"));
    {
        const char* langItems[kLangCount];
        for (int i = 0; i < kLangCount; ++i) langItems[i] = tr(kLangLabels[i]);
        ImGui::SetNextItemWidth(200);
        ImGui::Combo("##projlang", &wiz.selectedLang, langItems, kLangCount);
    }

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

    if (ImGui::Button(tr("Create"), ImVec2(btnWidth, 0))) {
        wiz.errorMsg.clear();
        auto tmpl = static_cast<ProjectTemplate>(wiz.selectedTemplate);
        auto lang = static_cast<ProjectLang>(wiz.selectedLang);
        if (createProject(root, wiz.name, tmpl, lang, wiz.errorMsg)) {
            gLog.info(std::string("Created project [") +
                      kTemplates[wiz.selectedTemplate].label + "]: " + wiz.name);
            projects = listProjects(root);
            wiz.reset();
        } else {
            gLog.error("Create failed: " + wiz.errorMsg);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(btnWidth, 0))) {
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
    std::string title = makeWindowTitle(tr("Projects"), "Projects");
    ImGui::Begin(title.c_str());

    // -- Header
    ImGui::TextDisabled("%s: %s", tr("Root"), root.string().c_str());
    ImGui::Separator();

    // -- Toolbar
    if (ImGui::Button(tr("+ New Project"))) {
        wizard.reset();
        wizard.open = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Refresh"))) projects = listProjects(root);

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
        ImGui::Text("%s: %s", tr("Selected"), selected->filename().string().c_str());

        std::string tmpl = readProjectMeta(*selected, "template");
        std::string ver  = readProjectMeta(*selected, "version");
        if (!tmpl.empty()) ImGui::Text("%s: %s", tr("Template"), tmpl.c_str());
        if (!ver.empty())  ImGui::Text("%s:  %s", tr("Version"), ver.c_str());
        ImGui::TextWrapped("%s: %s", tr("Path"), selected->string().c_str());

        if (ImGui::Button(tr("Open"))) {
            gLog.info("Open project: " + selected->filename().string());
            openRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Delete"))) {
            ImGui::OpenPopup("###ConfirmDelete");
        }

        std::string confirmTitle = makeWindowTitle(tr("Delete Project"), "ConfirmDelete");
        if (ImGui::BeginPopupModal(confirmTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("\xe5\x88\xaa\xe9\x99\xa4 \"%s\"\xef\xbc\x9f\xe6\xad\xa4\xe5\x8b\x95\xe4\xbd\x9c\xe7\x84\xa1\xe6\xb3\x95\xe5\x9b\x9e\xe5\xbe\xa9\xe3\x80\x82", selected->filename().string().c_str());
            if (ImGui::Button(tr("Yes, Delete"))) {
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
            if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
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

    // Flow chart state
    FlowChartState flowChart;
    bool showFlowChart = false;
    ProjectLang currentProjectLang = ProjectLang::Cpp;

    enum class EditorMode { None, UIDesigner, GameEditor };
    EditorMode editorMode = EditorMode::None;
    bool editorDockNeedsInit = false;

    // Load default language from .lang file
    loadLanguage(gUiLang);

    gLog.info("MyuEngine editor started");
    gLog.info("Projects root: " + projectsRoot.string());

    bool showAbout = false;
    bool showHelp = true;
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

    bool mainDockNeedsInit = false;
    if (io.IniFilename && !fs::exists(io.IniFilename))
        mainDockNeedsInit = true;

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
            ImGuiID mainDockId = ImGui::GetID("MyuDock");
            ImGui::DockSpace(mainDockId, ImVec2(0, 0),
                             ImGuiDockNodeFlags_PassthruCentralNode);

            if (mainDockNeedsInit) {
                mainDockNeedsInit = false;
                ImGui::DockBuilderRemoveNode(mainDockId);
                ImGui::DockBuilderAddNode(mainDockId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(mainDockId, vp->WorkSize);

                ImGuiID left, center, bottom;
                ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.24f, &left, &center);
                ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, &bottom, &center);
                ImGui::DockBuilderDockWindow(makeWindowTitle(tr("Projects"), "Projects").c_str(), left);
                ImGui::DockBuilderDockWindow(makeWindowTitle(tr("Log Console"), "LogConsole").c_str(), bottom);
                ImGui::DockBuilderDockWindow(makeWindowTitle(tr("Usage Guide"), "Help").c_str(), center);
                ImGui::DockBuilderFinish(mainDockId);
            }

            // Menu bar
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu(tr("File"))) {
                    if (ImGui::MenuItem(tr("Rescan Projects")))
                        projects = listProjects(projectsRoot);
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("Quit"), "Alt+F4"))
                        running = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("Settings"))) {
                    if (ImGui::Checkbox(tr("Enable New Window (Viewports)"), &enableViewports)) {
                        gLog.info(std::string("Viewports ") +
                                  (enableViewports ? "enabled" : "disabled"));
                    }
                    if (ImGui::Checkbox(tr("VSync"), &enableVsync)) {
                        gLog.info(std::string("VSync ") + (enableVsync ? "on" : "off"));
                    }
                    ImGui::Checkbox(tr("Reduce FPS When Unfocused"), &reduceFpsWhenUnfocused);
                    ImGui::Checkbox(tr("Pause Render When Unfocused"), &pauseRenderWhenUnfocused);
                    ImGui::Checkbox(tr("Pause Render When Minimized"), &pauseRenderWhenMinimized);
                    ImGui::Checkbox(tr("Reduce Heavy Panels When Idle"), &reduceHeavyWhenIdle);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt(tr("FPS Limit"), &fpsLimit, 1, 0, 240);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt(tr("Idle FPS"), &idleFpsLimit, 1, 0, 60);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt(tr("Platform FPS"), &platformFpsLimit, 1, 0, 240);
                    ImGui::Checkbox(tr("Pause Platform When Minimized"), &pausePlatformWhenMinimized);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragInt(tr("Idle Delay (ms)"), &idleRenderMs, 10, 0, 2000);
                    ImGui::TextDisabled("%s", tr("0 = unlimited"));

                    ImGui::Separator();
                    ImGui::TextDisabled("%s", tr("Language"));
                    int langIndex = (gUiLang == UiLang::ZhTW) ? 0 : 1;
                    const char* langs[] = {"\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87", "English"};
                    if (ImGui::Combo("##lang", &langIndex, langs, 2))
                        loadLanguage((langIndex == 0) ? UiLang::ZhTW : UiLang::EnUS);

                    if (ImGui::MenuItem(tr("Reset Layout")))
                        mainDockNeedsInit = true;

                    ImGui::Separator();
                    ImGui::TextDisabled("%s", tr("Editor Performance"));
                    ImGui::Checkbox(tr("Viewport in Scene Tab"), &gameEditor.perf.showViewportInSceneTab);
                    ImGui::Checkbox(tr("Viewport in Board Tab"), &gameEditor.perf.showViewportInBoardTab);
                    ImGui::Checkbox(tr("Viewport in Preview Tab"), &gameEditor.perf.showViewportInPreviewTab);
                    ImGui::Checkbox(tr("Draw Board Grid"), &gameEditor.perf.drawBoardGrid);
                    ImGui::Checkbox(tr("Draw Board Labels"), &gameEditor.perf.drawBoardLabels);
                    ImGui::Checkbox(tr("Draw Board Pieces"), &gameEditor.perf.drawBoardPieces);
                    ImGui::Checkbox(tr("Draw Board Highlights"), &gameEditor.perf.drawBoardHighlights);
                    ImGui::Checkbox(tr("Draw Card Preview"), &gameEditor.perf.drawCardPreview);
                    ImGui::Checkbox(tr("Viewport Grid"), &gameEditor.perf.drawViewportGrid);
                    ImGui::Checkbox(tr("Viewport Pieces"), &gameEditor.perf.drawViewportPieces);
                    ImGui::Checkbox(tr("Viewport Shadows"), &gameEditor.perf.drawViewportShadows);
                    ImGui::SetNextItemWidth(120);
                    ImGui::DragFloat(tr("Viewport Scale"), &gameEditor.perf.viewportScale, 1.0f, 10.0f, 120.0f, "%.0f");

                    ImGui::Separator();
                    ImGui::TextDisabled("%s", tr("UI Designer Performance"));
                    ImGui::Checkbox(tr("UI Canvas"), &uiDesigner.perf.drawCanvas);
                    ImGui::Checkbox(tr("UI Grid"), &uiDesigner.perf.drawGrid);
                    ImGui::Checkbox(tr("UI Elements"), &uiDesigner.perf.drawElements);
                    ImGui::Checkbox(tr("UI Selection"), &uiDesigner.perf.drawSelection);
                    ImGui::Checkbox(tr("UI Overlay"), &uiDesigner.perf.drawOverlay);

                    ImGui::Separator();
                    ImGui::TextDisabled("%s", tr("Installer / Data"));
                    ImGui::Text("%s: %s", tr("Data Root"), dataRoot.string().c_str());
                    ImGui::InputText(tr("Data Root Override"), dataRootBuf, sizeof(dataRootBuf));
                    if (ImGui::Button(tr("Apply Data Root"))) {
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
                    if (ImGui::Button(tr("Clear Override"))) {
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
                    ImGui::TextDisabled("%s", tr("Simulate Install"));
                    ImGui::InputText(tr("Install Dir"), simInstallBuf, sizeof(simInstallBuf));
                    if (ImGui::Button(tr("Simulate Install"))) {
                        std::string err;
                        if (simulateInstall(fs::path(simInstallBuf), err))
                            gLog.info("Simulated install to: " + std::string(simInstallBuf));
                        else
                            gLog.error("Simulate install failed: " + err);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("Help"))) {
                    ImGui::MenuItem(tr("Usage Guide"), nullptr, &showHelp);
                    if (ImGui::MenuItem(tr("About")))
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
            std::string langStr = readProjectMeta(*selectedProject, "language");
            // Set project language
            currentProjectLang = ProjectLang::Cpp;
            for (int i = 0; i < kLangCount; ++i) {
                if (langStr == kLangIds[i]) { currentProjectLang = static_cast<ProjectLang>(i); break; }
            }

            bool isGameTemplate = (tmpl == "2D Game" || tmpl == "3D Game");
            if (isGameTemplate) {
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

            // Auto-load existing flow chart
            flowChart = FlowChartState();
            auto fcPath = *selectedProject / "flowchart.myuflow";
            if (fs::exists(fcPath)) {
                flowChart.load(fcPath);
                gLog.info("Loaded flow chart: " + fcPath.string());
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
                if (ImGui::BeginMenu(tr("File"))) {
                    if (editorMode == EditorMode::UIDesigner) {
                        if (ImGui::MenuItem(tr("Save UI Design"), "Ctrl+S")) {
                            if (selectedProject) {
                                auto savePath = *selectedProject / "ui_layout.json";
                                myu::ui::saveDesign(uiDesigner, savePath);
                                gLog.info("Saved UI design: " + savePath.string());
                            }
                        }
                        if (ImGui::MenuItem(tr("Export HTML"))) {
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
                    // Save / Load Flow Chart
                    if (ImGui::MenuItem(tr("Save Flow Chart"))) {
                        if (selectedProject) {
                            auto fcPath = *selectedProject / "flowchart.myuflow";
                            if (flowChart.save(fcPath))
                                gLog.info("Saved flow chart: " + fcPath.string());
                            else
                                gLog.error("Failed to save flow chart");
                        }
                    }
                    if (ImGui::MenuItem(tr("Load Flow Chart"))) {
                        if (selectedProject) {
                            auto fcPath = *selectedProject / "flowchart.myuflow";
                            if (flowChart.load(fcPath)) {
                                showFlowChart = true;
                                gLog.info("Loaded flow chart: " + fcPath.string());
                            } else {
                                gLog.warn("No flow chart found: " + fcPath.string());
                            }
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("Close Project"))) editorHostOpen = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("Build"))) {
                    if (!gBuild.running) {
                        if (ImGui::MenuItem(tr("Build"), "Ctrl+B")) {
                            if (selectedProject) {
                                std::string projDir = selectedProject->string();
                                std::string lang = readProjectMeta(*selectedProject, "language");
                                std::string cmd;
                                if (lang == "cpp") cmd = "cmake -B build && cmake --build build 2>&1";
                                else if (lang == "java") cmd = "javac -d build $(find . -name '*.java') 2>&1";
                                else if (lang == "python") cmd = "python -m py_compile $(find . -name '*.py') 2>&1";
                                else if (lang == "web") cmd = "echo 'Web project — no build step required'";
                                else cmd = "cmake -B build && cmake --build build 2>&1";
                                gLog.info(std::string(tr("Building...")) + " [" + cmd + "]");
                                gBuild.start(cmd, projDir);
                            }
                        }
                        if (ImGui::MenuItem(tr("Run"), "Ctrl+R")) {
                            if (selectedProject) {
                                std::string projDir = selectedProject->string();
                                std::string lang = readProjectMeta(*selectedProject, "language");
                                std::string cmd;
                                if (lang == "cpp") cmd = "./build/main 2>&1 || ./build/$(ls build/ | head -1) 2>&1";
                                else if (lang == "java") cmd = "java -cp build Main 2>&1";
                                else if (lang == "python") {
                                    // Find a main.py in src or root
                                    cmd = "python3 src/main.py 2>&1 || python3 main.py 2>&1";
                                }
                                else if (lang == "web") cmd = "echo 'Open public/index.html in browser'";
                                else cmd = "./build/main 2>&1";
                                gLog.info(std::string(tr("Running...")) + " [" + cmd + "]");
                                gBuild.start(cmd, projDir);
                            }
                        }
                        if (ImGui::MenuItem(tr("Build & Run"), "Ctrl+Shift+B")) {
                            if (selectedProject) {
                                std::string projDir = selectedProject->string();
                                std::string lang = readProjectMeta(*selectedProject, "language");
                                std::string cmd;
                                if (lang == "cpp") cmd = "cmake -B build && cmake --build build && ./build/main 2>&1";
                                else if (lang == "java") cmd = "javac -d build $(find . -name '*.java') && java -cp build Main 2>&1";
                                else if (lang == "python") cmd = "python3 src/main.py 2>&1 || python3 main.py 2>&1";
                                else if (lang == "web") cmd = "echo 'Open public/index.html in browser'";
                                else cmd = "cmake -B build && cmake --build build && ./build/main 2>&1";
                                gLog.info(std::string(tr("Build & Run")) + " [" + cmd + "]");
                                gBuild.start(cmd, projDir);
                            }
                        }
                    } else {
                        ImGui::MenuItem(tr("Building..."), nullptr, false, false);
                        if (ImGui::MenuItem(tr("Stop"))) gBuild.stop();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(tr("Tools"))) {
                    ImGui::MenuItem(tr("Flow Chart"), nullptr, &showFlowChart);
                    ImGui::Separator();
                    ImGui::MenuItem(tr("Voxel Modeler"), nullptr, &showVoxelEditor);
                    ImGui::MenuItem(tr("Blockbench Import"), nullptr, &showBlockbench);
                    ImGui::MenuItem(tr("Resources"), nullptr, &showResources);
                    ImGui::MenuItem("ECS", nullptr, &showEcs);
                    ImGui::MenuItem(tr("Events"), nullptr, &showEvents);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            // Build status bar
            if (gBuild.running) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.3f, 1));
                ImGui::Text("  [%s]", tr("Building..."));
                ImGui::PopStyleColor();
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
            if (showVoxelEditor) {
                std::string voxTitle = makeWindowTitle(tr("Voxel Modeler"), "VoxelModeler");
                myu::editor::drawVoxelEditor(voxelEditor, voxTitle.c_str(), allowHeavyRender);
            }

            // Blockbench import panel
            if (showBlockbench) {
                std::string bbTitle = makeWindowTitle(tr("Blockbench Import"), "BlockbenchImport");
                ImGui::Begin(bbTitle.c_str(), &showBlockbench);
                static char bbPath[512] = {};
                ImGui::InputText(".bbmodel Path", bbPath, sizeof(bbPath));
                static myu::tools::BlockbenchModelInfo bbInfo;
                if (ImGui::Button(tr("Load"))) {
                    if (!myu::tools::loadBlockbenchModelInfo(bbPath, bbInfo))
                        gLog.error("Failed to load Blockbench model");
                    else
                        gLog.info("Loaded Blockbench model: " + bbInfo.name);
                }
                ImGui::Separator();
                ImGui::Text("%s: %s", tr("Name"), bbInfo.name.c_str());
                ImGui::Text("%s: %d", tr("Elements"), bbInfo.elementCount);
                ImGui::Text("%s: %d", tr("Textures"), bbInfo.textureCount);
                ImGui::Text("%s: %dx%d", tr("Resolution"), bbInfo.resolutionX, bbInfo.resolutionY);
                ImGui::End();
            }

            // Resources panel
            if (showResources) {
                std::string resTitle = makeWindowTitle(tr("Resources"), "Resources");
                ImGui::Begin(resTitle.c_str(), &showResources);
                static int rtype = 0;
                static char rname[64] = {};
                static char rpath[256] = {};
                const char* rtypes[] = {tr("Texture"),tr("Model"),tr("Material"),tr("Audio"),tr("Font")};
                ImGui::Combo(tr("Type"), &rtype, rtypes, 5);
                ImGui::InputText(tr("Name"), rname, sizeof(rname));
                ImGui::InputText(tr("Path"), rpath, sizeof(rpath));
                if (ImGui::Button(tr("Add Resource"))) {
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
                if (ImGui::InputText(tr("New Entity"), ename, sizeof(ename), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    ecsWorld.createEntity(ename);
                }
                if (ImGui::Button(tr("Create Entity"))) ecsWorld.createEntity(ename);
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
                std::string evtTitle = makeWindowTitle(tr("Events"), "Events");
                ImGui::Begin(evtTitle.c_str(), &showEvents);
                static char ename[64] = "game.tick";
                static char epayload[256] = "{}";
                ImGui::InputText(tr("Name"), ename, sizeof(ename));
                ImGui::InputTextMultiline(tr("Payload"), epayload, sizeof(epayload), ImVec2(-1, 60));
                if (ImGui::Button(tr("Emit"))) eventBus.emit(ename, epayload);
                ImGui::SameLine();
                if (ImGui::Button(tr("Dispatch"))) eventBus.dispatch();
                ImGui::SameLine();
                if (ImGui::Button(tr("Clear"))) eventLog.clear();
                ImGui::Separator();
                for (const auto& e : eventLog) {
                    ImGui::BulletText("%s: %s", e.name.c_str(), e.payload.c_str());
                }
                ImGui::End();
            }

            // Flow chart panel (canvas + properties side by side)
            if (showFlowChart) {
                std::string fcTitle = makeWindowTitle(tr("Flow Chart"), "FlowChart");
                ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Once);
                if (ImGui::Begin(fcTitle.c_str(), &showFlowChart)) {
                    // Left: canvas, Right: properties
                    float propsWidth = 280;
                    ImGui::BeginChild("##fcCanvas", ImVec2(-propsWidth, 0));
                    drawFlowChart(flowChart, currentProjectLang);
                    ImGui::EndChild();
                    ImGui::SameLine();
                    ImGui::BeginChild("##fcProps", ImVec2(0, 0), ImGuiChildFlags_Border);
                    drawFlowProperties(flowChart);
                    ImGui::EndChild();
                }
                ImGui::End();
            }

            // Drain build runner output
            gBuild.drain(gLog);
            if (gBuild.finished) {
                gBuild.finished = false;
                std::string msg = std::string(tr("Process finished with code")) +
                                  " " + std::to_string(gBuild.exitCode);
                if (gBuild.exitCode == 0)
                    gLog.info(msg);
                else
                    gLog.error(msg);
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

        // --- Help panel ---
        if (showHelp) drawHelpPanel(&showHelp);

        // --- About popup ---
        if (showAbout) {
            ImGui::OpenPopup("###AboutPopup");
            showAbout = false;
        }
        {
            std::string aboutTitle = makeWindowTitle((std::string(tr("About")) + " MyuEngine").c_str(), "AboutPopup");
            if (ImGui::BeginPopupModal(aboutTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("MyuEngine v0.1.0");
            ImGui::Text("SDL2 + OpenGL 3.3 + Dear ImGui");
            ImGui::Text("Built with MyuDoglibs-Cpp");
            ImGui::Separator();
            if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            }
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
