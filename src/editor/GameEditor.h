#pragma once
// =============================================================================
// GameEditor.h – All-in-one ImGui editor panels for game development
//   Scene hierarchy, Inspector, Code editor, Board creation,
//   Game systems config, 2.5D viewport
// =============================================================================

#include "../engine/Core.h"
#include "../engine/Camera2D5.h"
#include "../engine/Math3D.h"
#include "../engine/Resources.h"
#include "../engine/GltfLoader.h"
#include "../game/BoardGame.h"
#include "../game/CardGame.h"
#include "../game/GameSystems.h"
#include "../tools/BlockbenchImport.h"

#include <imgui.h>
#include <glad/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace myu::editor {

// ─── Performance Settings ──────────────────────────────────────────────────

struct PerfSettings {
    bool  showViewportInSceneTab  = true;
    bool  showViewportInBoardTab  = true;
    bool  showViewportInPreviewTab= true;
    bool  drawBoardGrid           = true;
    bool  drawBoardLabels         = true;
    bool  drawBoardPieces         = true;
    bool  drawBoardHighlights     = true;
    bool  drawCardPreview         = true;
    bool  drawViewportGrid        = true;
    bool  drawViewportPieces      = true;
    bool  drawViewportShadows     = true;
    float viewportScale           = 40.0f; // pixels per world unit
};

// ─── Game Flow (FSM + Sequence) ───────────────────────────────────────────

struct FlowState {
    std::string name;
    std::string onEnter;
    std::string onExit;
    float nodeX = 40.0f;
    float nodeY = 40.0f;
};

struct FlowTransition {
    int fromState = -1;
    int toState = -1;
    std::string eventName;
    std::string condition;
};

struct FlowSequenceStep {
    std::string name;
    float duration = 1.0f;
    std::string onStart;
    std::string onUpdate;
    std::string onEnd;
    float nodeX = 40.0f;
    float nodeY = 40.0f;
};

struct FlowEventTrigger {
    std::string eventName;
    std::string keyName; // SDL key name
};

struct FlowVar {
    enum class Type { Number, Bool, String };
    std::string name;
    Type type = Type::Number;
    float numValue = 0.0f;
    bool boolValue = false;
    std::string strValue;
};

// ─── 3D Camera / Viewport ─────────────────────────────────────────────────

struct Camera3DState {
    myu::engine::Vec3 position = {0, 2.5f, 6.0f};
    myu::engine::Vec3 pivot    = {0, 0, 0};
    float yaw   = -90.0f; // degrees
    float pitch = -20.0f; // degrees
    float distance = 8.0f;
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane  = 200.0f;
    float moveSpeed = 6.0f;
    float mouseSens = 0.2f;
    bool invertY = false;
    bool axisMoveMode = false; // pan along world axes

    enum class Mode { Orbit, Fly } mode = Mode::Orbit;
};

struct Keybinds3D {
    ImGuiKey forward = ImGuiKey_W;
    ImGuiKey back    = ImGuiKey_S;
    ImGuiKey left    = ImGuiKey_A;
    ImGuiKey right   = ImGuiKey_D;
    ImGuiKey up      = ImGuiKey_E;
    ImGuiKey down    = ImGuiKey_Q;
    ImGuiKey sprint  = ImGuiKey_LeftShift;
    ImGuiKey toggleView = ImGuiKey_V;
};

enum class PlayerView3D { FirstPerson, ThirdPerson };

struct Viewport3DResources {
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint depth = 0;
    int width = 0;
    int height = 0;

    GLuint program = 0;
    GLuint vaoCube = 0;
    GLuint vboCube = 0;
    GLuint vaoGrid = 0;
    GLuint vboGrid = 0;
    int gridVertexCount = 0;
    GLuint vaoAxis = 0;
    GLuint vboAxis = 0;

    bool initialized = false;
};

struct ModelMeshGPU {
    GLuint vao = 0;
    GLuint vbo = 0;
    int vertexCount = 0;
};

struct ModelCacheEntry {
    ModelMeshGPU gpu;
    std::string sourcePath;
    std::string error;
    bool loaded = false;
};

struct Gizmo3DState {
    bool active = false;
    int axis = 0; // 0=free,1=x,2=y,3=z
    myu::engine::Vec3 startPos = {0,0,0};
    ImVec2 startMouse = {0,0};
};

// ─── Editor State ───────────────────────────────────────────────────────────

struct GameEditorState {
    // Engine
    myu::engine::Scene      scene;
    myu::engine::Camera2D5  camera;
    myu::engine::GameObject* selectedObject = nullptr;

    // 3D viewport
    Camera3DState camera3d;
    Viewport3DResources viewport3d;
    Gizmo3DState gizmo3d;
    bool show3DGrid = true;
    bool show3DAxis = true;
    bool show3DGizmo = true;
    bool lockOrbit = false;

    bool playerControlEnabled = false;
    PlayerView3D playerView = PlayerView3D::ThirdPerson;
    float playerSpeed = 4.0f;
    float playerSprintMul = 2.0f;
    float playerHeight = 1.6f;
    float thirdPersonDistance = 4.0f;
    float thirdPersonHeight = 1.0f;
    Keybinds3D keybinds;

    // Resource access (injected by main.cpp)
    myu::engine::ResourceManager* resources = nullptr;

    // Model cache
    std::unordered_map<std::string, ModelCacheEntry> modelCache;

    // Game systems (kept for Systems tab)
    myu::game::Board        board;
    myu::game::CardLibrary  cardLibrary;
    myu::game::PlayerCards  player1Cards, player2Cards;
    myu::game::TurnManager  turnManager;
    myu::game::GameTimer    timer;
    myu::game::ScoreSystem  score;
    myu::game::Shop         shop;
    myu::game::AudioConfig  audio;

    // Editor tabs
    enum class Tab { Scene, Code, Objects, Systems, Flow, Preview, ThreeD };
    Tab currentTab = Tab::Scene;

    // Board editor state
    myu::game::CellType paintCellType = myu::game::CellType::Normal;
    int  selectedPieceType = -1;
    myu::game::PieceOwner placeOwner = myu::game::PieceOwner::Player1;
    bool boardInited = false;

    // Card editor state
    int  selectedCardTmpl = -1;
    char cardNameBuf[64]  = {};
    char cardDescBuf[256] = {};
    char cardArtBuf[256]  = {};

    // Inspector buf
    char objNameBuf[64] = {};
    char objTagBuf[64]  = {};

    // Piece type editor
    char ptNameBuf[64]    = {};
    char ptSpriteBuf[256] = {};

    // Shop editor
    char shopNameBuf[64]  = {};
    char shopDescBuf[256] = {};

    // Audio
    char bgmBuf[256]      = {};
    char sfxNameBuf[64]   = {};
    char sfxPathBuf[256]  = {};

    // Preview
    bool previewPlaying = false;
    int  previewSelPiece = -1;

    PerfSettings perf;

    // --- Code Editor State ---
    std::string codeBuffer;         // Current file content
    std::string codeFilePath;       // Path to the currently edited file
    bool codeDirty = false;
    std::vector<std::string> projectFiles; // List of source files

    // --- Project path (set when project is opened) ---
    std::filesystem::path projectDir;

    // --- Add Object state ---
    char newBoardName[64] = "GameBoard";
    int  newBoardRows = 8;
    int  newBoardCols = 8;

    // --- 3D Object state ---
    char new3DName[64] = "Cube";
    int  new3DShape = 0; // 0=Cube,1=Plane
    float new3DSize = 1.0f;
    char bbmodelPath[256] = "";

    // --- Flow editor state ---
    std::vector<FlowState> flowStates;
    std::vector<FlowTransition> flowTransitions;
    std::vector<FlowSequenceStep> flowSteps;
    std::vector<FlowEventTrigger> flowTriggers;
    std::vector<FlowVar> flowVars;
    int selectedFlowState = -1;
    int selectedFlowStep = -1;
    char flowNameBuf[64] = "Flow";
    bool showFlowGraph = true;

    GameEditorState() {
        // No board initialization — blank project
    }

    void initDefaultScene() {
        scene = myu::engine::Scene();
        if (board.cells.empty()) {
            board.init(8, 8);
            board.clearPieces();
            boardInited = true;
        }
        // Blank scene — just a background
        auto* bg = scene.createObject("Background", "background");
        bg->position = {0, 0, -1};
        bg->width = 20; bg->height = 20;
        bg->tint = {0.08f, 0.12f, 0.22f, 1.0f};
    }

    // --- Code editor helpers ---
    void scanProjectFiles() {
        projectFiles.clear();
        if (projectDir.empty()) return;
        std::error_code ec;
        auto srcDir = projectDir / "src";
        if (std::filesystem::is_directory(srcDir, ec)) {
            for (auto& entry : std::filesystem::recursive_directory_iterator(srcDir, ec)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" ||
                        ext == ".py"  || ext == ".java" || ext == ".js" ||
                        ext == ".html" || ext == ".css" || ext == ".json")
                        projectFiles.push_back(
                            std::filesystem::relative(entry.path(), projectDir).string());
                }
            }
        }
        // Also check Scripts/ for legacy layout
        auto scriptsDir = projectDir / "Scripts";
        if (std::filesystem::is_directory(scriptsDir, ec)) {
            for (auto& entry : std::filesystem::recursive_directory_iterator(scriptsDir, ec)) {
                if (entry.is_regular_file())
                    projectFiles.push_back(
                        std::filesystem::relative(entry.path(), projectDir).string());
            }
        }
        // Also scan root-level CMakeLists.txt
        if (std::filesystem::exists(projectDir / "CMakeLists.txt", ec))
            projectFiles.push_back("CMakeLists.txt");
        std::sort(projectFiles.begin(), projectFiles.end());
    }

    void openFile(const std::string& relPath) {
        auto fullPath = projectDir / relPath;
        std::ifstream f(fullPath);
        if (!f) return;
        std::ostringstream ss;
        ss << f.rdbuf();
        codeBuffer = ss.str();
        codeFilePath = fullPath.string();
        codeDirty = false;
    }

    void saveCurrentFile() {
        if (codeFilePath.empty()) return;
        std::ofstream f(codeFilePath);
        if (f) {
            f << codeBuffer;
            codeDirty = false;
        }
    }

    // --- Add Board object: inject code into main.cpp ---
    void addBoardToProject(const char* varName, int rows, int cols) {
        auto mainPath = projectDir / "src" / "main.cpp";
        std::ifstream f(mainPath);
        if (!f) return;
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string src = ss.str();
        f.close();

        // Insert board declaration after "void init() override {"
        std::string initMarker = "void init() override {";
        auto pos = src.find(initMarker);
        if (pos == std::string::npos) return;
        pos += initMarker.size();

        std::string boardDecl =
            "\n        // Board: " + std::string(varName) + " (" +
            std::to_string(rows) + "x" + std::to_string(cols) + ")\n"
            "        " + std::string(varName) + ".resize(" +
            std::to_string(rows) + ", " + std::to_string(cols) + ", 0);\n";
        src.insert(pos, boardDecl);

        // Insert member declaration after "class MyGame : public GameApp {"
        std::string classMarker = "class MyGame : public GameApp {";
        auto cpos = src.find(classMarker);
        if (cpos != std::string::npos) {
            cpos += classMarker.size();
            std::string memberDecl =
                "\n    Board<int> " + std::string(varName) + ";\n";
            src.insert(cpos, memberDecl);
        }

        std::ofstream out(mainPath);
        if (out) out << src;

        // Reload if this file is open in the editor
        if (codeFilePath == mainPath.string()) {
            codeBuffer = src;
            codeDirty = false;
        }
    }
};

// ─── Helpers ────────────────────────────────────────────────────────────────

inline ImU32 colF(float r, float g, float b, float a = 1.0f) {
    return IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),(int)(a*255));
}

// ─── 3D Rendering Helpers ─────────────────────────────────────────────────

inline GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[3D] Shader compile error: %s\n", log);
    }
    return s;
}

inline GLuint createProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[3D] Program link error: %s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

inline void init3DResources(Viewport3DResources& vr, int width, int height) {
    if (!vr.program) {
        const char* vs =
            "#version 330 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "layout(location=1) in vec3 aNormal;\n"
            "uniform mat4 uModel;\n"
            "uniform mat4 uView;\n"
            "uniform mat4 uProjection;\n"
            "out vec3 vNormal;\n"
            "out vec3 vPos;\n"
            "void main(){\n"
            "  vec4 wp = uModel * vec4(aPos,1.0);\n"
            "  vPos = wp.xyz;\n"
            "  vNormal = mat3(uModel) * aNormal;\n"
            "  gl_Position = uProjection * uView * wp;\n"
            "}\n";
        const char* fs =
            "#version 330 core\n"
            "in vec3 vNormal;\n"
            "in vec3 vPos;\n"
            "uniform vec3 uColor;\n"
            "uniform vec3 uLightPos;\n"
            "uniform vec3 uViewPos;\n"
            "uniform int uUseLighting;\n"
            "out vec4 FragColor;\n"
            "void main(){\n"
            "  vec3 color = uColor;\n"
            "  if (uUseLighting == 1) {\n"
            "    vec3 norm = normalize(vNormal);\n"
            "    vec3 lightDir = normalize(uLightPos - vPos);\n"
            "    float diff = max(dot(norm, lightDir), 0.0);\n"
            "    float ambient = 0.25;\n"
            "    color = color * (ambient + diff * 0.75);\n"
            "  }\n"
            "  FragColor = vec4(color, 1.0);\n"
            "}\n";
        vr.program = createProgram(vs, fs);
    }

    if (!vr.vaoCube) {
        float cube[] = {
            // positions          // normals
            -0.5f,-0.5f,-0.5f,  0,0,-1,
             0.5f,-0.5f,-0.5f,  0,0,-1,
             0.5f, 0.5f,-0.5f,  0,0,-1,
             0.5f, 0.5f,-0.5f,  0,0,-1,
            -0.5f, 0.5f,-0.5f,  0,0,-1,
            -0.5f,-0.5f,-0.5f,  0,0,-1,

            -0.5f,-0.5f, 0.5f,  0,0,1,
             0.5f,-0.5f, 0.5f,  0,0,1,
             0.5f, 0.5f, 0.5f,  0,0,1,
             0.5f, 0.5f, 0.5f,  0,0,1,
            -0.5f, 0.5f, 0.5f,  0,0,1,
            -0.5f,-0.5f, 0.5f,  0,0,1,

            -0.5f, 0.5f, 0.5f, -1,0,0,
            -0.5f, 0.5f,-0.5f, -1,0,0,
            -0.5f,-0.5f,-0.5f, -1,0,0,
            -0.5f,-0.5f,-0.5f, -1,0,0,
            -0.5f,-0.5f, 0.5f, -1,0,0,
            -0.5f, 0.5f, 0.5f, -1,0,0,

             0.5f, 0.5f, 0.5f, 1,0,0,
             0.5f, 0.5f,-0.5f, 1,0,0,
             0.5f,-0.5f,-0.5f, 1,0,0,
             0.5f,-0.5f,-0.5f, 1,0,0,
             0.5f,-0.5f, 0.5f, 1,0,0,
             0.5f, 0.5f, 0.5f, 1,0,0,

            -0.5f,-0.5f,-0.5f, 0,-1,0,
             0.5f,-0.5f,-0.5f, 0,-1,0,
             0.5f,-0.5f, 0.5f, 0,-1,0,
             0.5f,-0.5f, 0.5f, 0,-1,0,
            -0.5f,-0.5f, 0.5f, 0,-1,0,
            -0.5f,-0.5f,-0.5f, 0,-1,0,

            -0.5f, 0.5f,-0.5f, 0,1,0,
             0.5f, 0.5f,-0.5f, 0,1,0,
             0.5f, 0.5f, 0.5f, 0,1,0,
             0.5f, 0.5f, 0.5f, 0,1,0,
            -0.5f, 0.5f, 0.5f, 0,1,0,
            -0.5f, 0.5f,-0.5f, 0,1,0
        };
        glGenVertexArrays(1, &vr.vaoCube);
        glGenBuffers(1, &vr.vboCube);
        glBindVertexArray(vr.vaoCube);
        glBindBuffer(GL_ARRAY_BUFFER, vr.vboCube);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glBindVertexArray(0);
    }

    if (!vr.vaoGrid) {
        const int half = 10;
        const float step = 1.0f;
        std::vector<float> verts;
        for (int i = -half; i <= half; ++i) {
            float v = i * step;
            // line parallel to X
            verts.push_back(-half * step); verts.push_back(0); verts.push_back(v);
            verts.push_back(0); verts.push_back(1); verts.push_back(0);
            verts.push_back(half * step); verts.push_back(0); verts.push_back(v);
            verts.push_back(0); verts.push_back(1); verts.push_back(0);
            // line parallel to Z
            verts.push_back(v); verts.push_back(0); verts.push_back(-half * step);
            verts.push_back(0); verts.push_back(1); verts.push_back(0);
            verts.push_back(v); verts.push_back(0); verts.push_back(half * step);
            verts.push_back(0); verts.push_back(1); verts.push_back(0);
        }
        vr.gridVertexCount = static_cast<int>(verts.size() / 6);
        glGenVertexArrays(1, &vr.vaoGrid);
        glGenBuffers(1, &vr.vboGrid);
        glBindVertexArray(vr.vaoGrid);
        glBindBuffer(GL_ARRAY_BUFFER, vr.vboGrid);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glBindVertexArray(0);
    }

    if (!vr.vaoAxis) {
        float axis[] = {
            0,0,0, 1,0,0,  1,0,0, 1,0,0,
            0,0,0, 0,1,0,  0,1,0, 0,1,0,
            0,0,0, 0,0,1,  0,0,1, 0,0,1
        };
        glGenVertexArrays(1, &vr.vaoAxis);
        glGenBuffers(1, &vr.vboAxis);
        glBindVertexArray(vr.vaoAxis);
        glBindBuffer(GL_ARRAY_BUFFER, vr.vboAxis);
        glBufferData(GL_ARRAY_BUFFER, sizeof(axis), axis, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glBindVertexArray(0);
    }

    if (vr.width != width || vr.height != height || !vr.fbo) {
        vr.width = width;
        vr.height = height;
        if (!vr.color) glGenTextures(1, &vr.color);
        glBindTexture(GL_TEXTURE_2D, vr.color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if (!vr.depth) glGenRenderbuffers(1, &vr.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, vr.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

        if (!vr.fbo) glGenFramebuffers(1, &vr.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, vr.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vr.color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, vr.depth);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    vr.initialized = true;
}

inline std::filesystem::path resolveResourcePath(const GameEditorState& st, const std::string& relOrName) {
    if (relOrName.empty()) return {};
    if (std::filesystem::path(relOrName).is_absolute())
        return relOrName;
    if (!st.projectDir.empty())
        return st.projectDir / relOrName;
    return relOrName;
}

inline bool loadModelToGPU(GameEditorState& st, const std::string& modelName, const std::string& path,
                           std::string& err) {
    myu::engine::MeshData mesh;
    if (!myu::engine::loadGltfMesh(path, mesh, err)) return false;

    ModelCacheEntry entry;
    entry.sourcePath = path;
    entry.loaded = true;

    glGenVertexArrays(1, &entry.gpu.vao);
    glGenBuffers(1, &entry.gpu.vbo);
    glBindVertexArray(entry.gpu.vao);
    glBindBuffer(GL_ARRAY_BUFFER, entry.gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(float), mesh.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    entry.gpu.vertexCount = mesh.vertexCount;
    st.modelCache[modelName] = entry;
    return true;
}

inline ModelCacheEntry* getModelEntry(GameEditorState& st, const std::string& modelName) {
    auto it = st.modelCache.find(modelName);
    if (it == st.modelCache.end()) return nullptr;
    return &it->second;
}

inline myu::engine::Vec3 forwardFromYawPitch(float yaw, float pitch) {
    float cy = std::cos(myu::engine::degToRad(yaw));
    float sy = std::sin(myu::engine::degToRad(yaw));
    float cp = std::cos(myu::engine::degToRad(pitch));
    float sp = std::sin(myu::engine::degToRad(pitch));
    return {cy * cp, sp, sy * cp};
}

inline myu::engine::Vec3 rightFromForward(const myu::engine::Vec3& f) {
    return myu::engine::normalize(myu::engine::cross(f, {0, 1, 0}));
}

inline myu::engine::Vec3 upFromForward(const myu::engine::Vec3& f) {
    return myu::engine::normalize(myu::engine::cross(rightFromForward(f), f));
}

inline std::string sanitizeField(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '|' || c == '\n' || c == '\r' || c == '\t') c = '_';
    }
    return out;
}

inline std::string sanitizeIdentifier(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "Flow";
    if (!out.empty() && (out[0] >= '0' && out[0] <= '9')) out = "Flow_" + out;
    return out;
}

inline std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '|') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

inline float toFloat(const std::string& s, float def = 0.0f) {
    try { return std::stof(s); } catch (...) { return def; }
}

inline int toInt(const std::string& s, int def = 0) {
    try { return std::stoi(s); } catch (...) { return def; }
}

inline void clear3DObjects(myu::engine::Scene& scene) {
    std::vector<uint32_t> ids;
    scene.forEachObject([&](myu::engine::GameObject& obj) {
        if (obj.tag == "3d" || obj.tag == "model" || obj.tag == "bbmodel")
            ids.push_back(obj.id);
    });
    for (auto id : ids) scene.removeObject(id);
}

inline bool save3DScene(GameEditorState& st, const std::filesystem::path& path) {
    std::ofstream f(path);
    if (!f) return false;

    const auto& cam = st.camera3d;
    f << "SCENE3D|1\n";
    f << "CAM|" << (cam.mode == Camera3DState::Mode::Orbit ? 0 : 1) << "|"
      << cam.position.x << "|" << cam.position.y << "|" << cam.position.z << "|"
      << cam.pivot.x << "|" << cam.pivot.y << "|" << cam.pivot.z << "|"
      << cam.yaw << "|" << cam.pitch << "|" << cam.distance << "|"
      << cam.fov << "|" << cam.nearPlane << "|" << cam.farPlane << "|"
      << cam.axisMoveMode << "|" << cam.invertY << "\n";

    st.scene.forEachObject([&](myu::engine::GameObject& obj) {
        if (obj.tag != "3d" && obj.tag != "model" && obj.tag != "bbmodel") return;
        f << "OBJ|" << sanitizeField(obj.name) << "|" << sanitizeField(obj.tag) << "|"
          << obj.position.x << "|" << obj.position.y << "|" << obj.position.z << "|"
          << obj.rotation.x << "|" << obj.rotation.y << "|" << obj.rotation.z << "|"
          << obj.scale.x << "|" << obj.scale.y << "|" << obj.scale.z << "|"
          << obj.tint.r << "|" << obj.tint.g << "|" << obj.tint.b << "|" << obj.tint.a << "|"
          << sanitizeField(obj.modelPath) << "|" << sanitizeField(obj.materialName) << "\n";
    });

    return true;
}

inline bool load3DScene(GameEditorState& st, const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return false;

    clear3DObjects(st.scene);
    st.selectedObject = nullptr;

    std::string line;
    while (std::getline(f, line)) {
        auto fields = splitFields(line);
        if (fields.empty()) continue;
        if (fields[0] == "CAM" && fields.size() >= 16) {
            st.camera3d.mode = (toInt(fields[1]) == 0)
                ? Camera3DState::Mode::Orbit
                : Camera3DState::Mode::Fly;
            st.camera3d.position = {toFloat(fields[2]), toFloat(fields[3]), toFloat(fields[4])};
            st.camera3d.pivot = {toFloat(fields[5]), toFloat(fields[6]), toFloat(fields[7])};
            st.camera3d.yaw = toFloat(fields[8]);
            st.camera3d.pitch = toFloat(fields[9]);
            st.camera3d.distance = toFloat(fields[10], st.camera3d.distance);
            st.camera3d.fov = toFloat(fields[11], st.camera3d.fov);
            st.camera3d.nearPlane = toFloat(fields[12], st.camera3d.nearPlane);
            st.camera3d.farPlane = toFloat(fields[13], st.camera3d.farPlane);
            st.camera3d.axisMoveMode = (toInt(fields[14]) != 0);
            st.camera3d.invertY = (toInt(fields[15]) != 0);
        } else if (fields[0] == "OBJ" && fields.size() >= 18) {
            auto* obj = st.scene.createObject(fields[1], fields[2]);
            obj->position = {toFloat(fields[3]), toFloat(fields[4]), toFloat(fields[5])};
            obj->rotation = {toFloat(fields[6]), toFloat(fields[7]), toFloat(fields[8])};
            obj->scale = {toFloat(fields[9], 1.0f), toFloat(fields[10], 1.0f), toFloat(fields[11], 1.0f)};
            obj->tint = {toFloat(fields[12], 1.0f), toFloat(fields[13], 1.0f),
                         toFloat(fields[14], 1.0f), toFloat(fields[15], 1.0f)};
            obj->modelPath = fields[16];
            obj->materialName = fields[17];
        }
    }
    return true;
}

inline bool saveFlow(const GameEditorState& st, const std::filesystem::path& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "FLOW|1|" << sanitizeField(st.flowNameBuf) << "\n";
        for (auto& s : st.flowStates) {
                f << "STATE|" << sanitizeField(s.name) << "|" << sanitizeField(s.onEnter)
                    << "|" << sanitizeField(s.onExit) << "|" << s.nodeX << "|" << s.nodeY << "\n";
    }
    for (auto& t : st.flowTransitions) {
        f << "TRANS|" << t.fromState << "|" << t.toState << "|"
          << sanitizeField(t.eventName) << "|" << sanitizeField(t.condition) << "\n";
    }
    for (auto& s : st.flowSteps) {
        f << "STEP|" << sanitizeField(s.name) << "|" << s.duration << "|"
          << sanitizeField(s.onStart) << "|" << sanitizeField(s.onUpdate)
          << "|" << sanitizeField(s.onEnd) << "|" << s.nodeX << "|" << s.nodeY << "\n";
    }
    for (auto& t : st.flowTriggers) {
        f << "TRIGGER|" << sanitizeField(t.eventName) << "|" << sanitizeField(t.keyName) << "\n";
    }
    for (auto& v : st.flowVars) {
        f << "VAR|" << sanitizeField(v.name) << "|" << static_cast<int>(v.type) << "|";
        if (v.type == FlowVar::Type::Number) f << v.numValue;
        else if (v.type == FlowVar::Type::Bool) f << (v.boolValue ? 1 : 0);
        else f << sanitizeField(v.strValue);
        f << "\n";
    }
    return true;
}

inline bool loadFlow(GameEditorState& st, const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return false;
    st.flowStates.clear();
    st.flowTransitions.clear();
    st.flowSteps.clear();
    st.flowTriggers.clear();
    st.flowVars.clear();
    st.selectedFlowState = -1;
    st.selectedFlowStep = -1;

    std::string line;
    while (std::getline(f, line)) {
        auto fields = splitFields(line);
        if (fields.empty()) continue;
        if (fields[0] == "FLOW" && fields.size() >= 3) {
            std::strncpy(st.flowNameBuf, fields[2].c_str(), sizeof(st.flowNameBuf)-1);
            st.flowNameBuf[sizeof(st.flowNameBuf)-1] = '\0';
        } else if (fields[0] == "STATE" && fields.size() >= 4) {
            FlowState s;
            s.name = fields[1];
            s.onEnter = fields[2];
            s.onExit = fields[3];
            if (fields.size() >= 6) {
                s.nodeX = toFloat(fields[4], s.nodeX);
                s.nodeY = toFloat(fields[5], s.nodeY);
            }
            st.flowStates.push_back(s);
        } else if (fields[0] == "TRANS" && fields.size() >= 5) {
            FlowTransition t;
            t.fromState = toInt(fields[1], -1);
            t.toState = toInt(fields[2], -1);
            t.eventName = fields[3];
            t.condition = fields[4];
            st.flowTransitions.push_back(t);
        } else if (fields[0] == "STEP" && fields.size() >= 6) {
            FlowSequenceStep s;
            s.name = fields[1];
            s.duration = toFloat(fields[2], 1.0f);
            s.onStart = fields[3];
            s.onUpdate = fields[4];
            s.onEnd = fields[5];
            if (fields.size() >= 8) {
                s.nodeX = toFloat(fields[6], s.nodeX);
                s.nodeY = toFloat(fields[7], s.nodeY);
            }
            st.flowSteps.push_back(s);
        } else if (fields[0] == "TRIGGER" && fields.size() >= 3) {
            FlowEventTrigger t;
            t.eventName = fields[1];
            t.keyName = fields[2];
            st.flowTriggers.push_back(t);
        } else if (fields[0] == "VAR" && fields.size() >= 4) {
            FlowVar v;
            v.name = fields[1];
            v.type = static_cast<FlowVar::Type>(toInt(fields[2], 0));
            if (v.type == FlowVar::Type::Number) v.numValue = toFloat(fields[3], 0.0f);
            else if (v.type == FlowVar::Type::Bool) v.boolValue = (toInt(fields[3], 0) != 0);
            else v.strValue = fields[3];
            st.flowVars.push_back(v);
        }
    }
    return true;
}

inline bool exportFlowRuntimeCpp(const GameEditorState& st, const std::filesystem::path& outDir,
                                 std::string* err = nullptr) {
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        if (err) *err = "Failed to create output directory";
        return false;
    }

    std::string flowName = sanitizeIdentifier(st.flowNameBuf);

    std::filesystem::path hpath = outDir / "flow_runtime.h";
    std::filesystem::path cpath = outDir / "flow_runtime.cpp";
    std::filesystem::path tpath = outDir / "flow_triggers.h";
    std::filesystem::path tcpath = outDir / "flow_triggers.cpp";

    std::ostringstream h;
    h << "#pragma once\n";
    h << "#include <string>\n";
    h << "#include <vector>\n";
    h << "#include <functional>\n";
    h << "#include <unordered_map>\n";
    h << "\n";
    h << "struct FlowEvent { std::string name; };\n";
    h << "\n";
    h << "class " << flowName << "Runtime {\n";
    h << "public:\n";
    h << "    struct State {\n";
    h << "        std::string name;\n";
    h << "        std::function<void()> onEnter;\n";
    h << "        std::function<void()> onExit;\n";
    h << "    };\n";
    h << "    struct Transition {\n";
    h << "        int from = -1;\n";
    h << "        int to = -1;\n";
    h << "        std::string eventName;\n";
    h << "        std::string condition;\n";
    h << "    };\n";
    h << "    struct Step {\n";
    h << "        std::string name;\n";
    h << "        float duration = 1.0f;\n";
    h << "        std::function<void()> onStart;\n";
    h << "        std::function<void(float)> onUpdate;\n";
    h << "        std::function<void()> onEnd;\n";
    h << "    };\n";
    h << "\n";
    h << "    void update(float dt);\n";
    h << "    void emit(const FlowEvent& e);\n";
    h << "    void start();\n";
    h << "    bool loadFromFile(const char* path);\n";
    h << "    void registerAction(const std::string& name, const std::function<void()>& fn);\n";
    h << "    void registerUpdateAction(const std::string& name, const std::function<void(float)>& fn);\n";
    h << "    void registerCondition(const std::string& name, const std::function<bool()>& fn);\n";
    h << "    void setVar(const std::string& name, float v);\n";
    h << "    void setVarBool(const std::string& name, bool v);\n";
    h << "    void setVarString(const std::string& name, const std::string& v);\n";
    h << "    float getVar(const std::string& name, float def = 0.0f) const;\n";
    h << "    bool getVarBool(const std::string& name, bool def = false) const;\n";
    h << "    std::string getVarString(const std::string& name, const std::string& def = \"\") const;\n";
    h << "\n";
    h << "    std::vector<State> states;\n";
    h << "    std::vector<Transition> transitions;\n";
    h << "    std::vector<Step> steps;\n";
    h << "    int currentState = 0;\n";
    h << "    int currentStep = 0;\n";
    h << "    float stepTimer = 0.0f;\n";
    h << "    std::unordered_map<std::string, std::function<void()>> actions;\n";
    h << "    std::unordered_map<std::string, std::function<void(float)>> updateActions;\n";
    h << "    std::unordered_map<std::string, std::function<bool()>> conditions;\n";
    h << "    std::unordered_map<std::string, float> vars;\n";
    h << "    std::unordered_map<std::string, bool> varsBool;\n";
    h << "    std::unordered_map<std::string, std::string> varsStr;\n";
    h << "    void callAction(const std::string& name);\n";
    h << "    void callUpdateAction(const std::string& name, float dt);\n";
    h << "    bool checkCondition(const std::string& name);\n";
    h << "    bool evalExpression(const std::string& expr);\n";
    h << "};\n";

    h << "void buildFlow(" << flowName << "Runtime& rt);\n";
    h << "void registerFlowActions(" << flowName << "Runtime& rt);\n";
    h << "void initializeFlowVars(" << flowName << "Runtime& rt);\n";

    std::ostringstream c;
    c << "#include \"flow_runtime.h\"\n";
    c << "#include <fstream>\n";
    c << "#include <vector>\n";
    c << "#include <string>\n";
    c << "#include <cctype>\n";
    c << "#include <cstdlib>\n";
    c << "\n";
    c << "void " << flowName << "Runtime::registerAction(const std::string& name, const std::function<void()>& fn) {\n";
    c << "    actions[name] = fn;\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::registerUpdateAction(const std::string& name, const std::function<void(float)>& fn) {\n";
    c << "    updateActions[name] = fn;\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::registerCondition(const std::string& name, const std::function<bool()>& fn) {\n";
    c << "    conditions[name] = fn;\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::setVar(const std::string& name, float v) {\n";
    c << "    vars[name] = v;\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::setVarBool(const std::string& name, bool v) {\n";
    c << "    varsBool[name] = v;\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::setVarString(const std::string& name, const std::string& v) {\n";
    c << "    varsStr[name] = v;\n";
    c << "}\n";
    c << "\n";
    c << "float " << flowName << "Runtime::getVar(const std::string& name, float def) const {\n";
    c << "    auto it = vars.find(name);\n";
    c << "    return it != vars.end() ? it->second : def;\n";
    c << "}\n";
    c << "\n";
    c << "bool " << flowName << "Runtime::getVarBool(const std::string& name, bool def) const {\n";
    c << "    auto it = varsBool.find(name);\n";
    c << "    return it != varsBool.end() ? it->second : def;\n";
    c << "}\n";
    c << "\n";
    c << "std::string " << flowName << "Runtime::getVarString(const std::string& name, const std::string& def) const {\n";
    c << "    auto it = varsStr.find(name);\n";
    c << "    return it != varsStr.end() ? it->second : def;\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::callAction(const std::string& name) {\n";
    c << "    auto it = actions.find(name);\n";
    c << "    if (it != actions.end()) it->second();\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::callUpdateAction(const std::string& name, float dt) {\n";
    c << "    auto it = updateActions.find(name);\n";
    c << "    if (it != updateActions.end()) it->second(dt);\n";
    c << "}\n";
    c << "\n";
    c << "bool " << flowName << "Runtime::checkCondition(const std::string& name) {\n";
    c << "    if (name.empty()) return true;\n";
    c << "    if (name == \"true\") return true;\n";
    c << "    if (name == \"false\") return false;\n";
    c << "    bool hasOps = false;\n";
    c << "    for (char c : name) {\n";
    c << "        if (c == '>' || c == '<' || c == '=' || c == '!' || c == '&' || c == '|' || c == '(' || c == ')' || c == '\"') { hasOps = true; break; }\n";
    c << "    }\n";
    c << "    if (hasOps) return evalExpression(name);\n";
    c << "    auto it = conditions.find(name);\n";
    c << "    if (it != conditions.end()) return it->second();\n";
    c << "    auto itb = varsBool.find(name);\n";
    c << "    if (itb != varsBool.end()) return itb->second;\n";
    c << "    auto itf = vars.find(name);\n";
    c << "    if (itf != vars.end()) return itf->second != 0.0f;\n";
    c << "    return false;\n";
    c << "}\n";
    c << "\n";
    c << "struct ExprToken { enum Type { End, Ident, Number, String, Op, LParen, RParen } type; std::string text; double number = 0.0; };\n";
    c << "static ExprToken nextToken(const std::string& s, size_t& i) {\n";
    c << "    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;\n";
    c << "    if (i >= s.size()) return {ExprToken::End, \"\"};\n";
    c << "    char c = s[i];\n";
    c << "    if (std::isalpha((unsigned char)c) || c == '_') {\n";
    c << "        size_t start = i++;\n";
    c << "        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;\n";
    c << "        return {ExprToken::Ident, s.substr(start, i - start)};\n";
    c << "    }\n";
    c << "    if (std::isdigit((unsigned char)c) || c == '.') {\n";
    c << "        size_t start = i++;\n";
    c << "        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) ++i;\n";
    c << "        ExprToken t{ExprToken::Number, s.substr(start, i - start)};\n";
    c << "        t.number = std::atof(t.text.c_str());\n";
    c << "        return t;\n";
    c << "    }\n";
    c << "    if (c == '\"') {\n";
    c << "        size_t start = ++i;\n";
    c << "        while (i < s.size() && s[i] != '\"') ++i;\n";
    c << "        std::string val = s.substr(start, i - start);\n";
    c << "        if (i < s.size()) ++i;\n";
    c << "        return {ExprToken::String, val};\n";
    c << "    }\n";
    c << "    if (c == '(') { ++i; return {ExprToken::LParen, \"(\"}; }\n";
    c << "    if (c == ')') { ++i; return {ExprToken::RParen, \")\"}; }\n";
    c << "    if (i + 1 < s.size()) {\n";
    c << "        std::string two = s.substr(i, 2);\n";
    c << "        if (two == \"&&\" || two == \"||\" || two == \"==\" || two == \"!=\" || two == \">=\" || two == \"<=\") {\n";
    c << "            i += 2; return {ExprToken::Op, two};\n";
    c << "        }\n";
    c << "    }\n";
    c << "    std::string one(1, c); ++i; return {ExprToken::Op, one};\n";
    c << "}\n";
    c << "struct ExprValue { enum Kind { Number, String, Bool } kind; double num; std::string str; bool b; };\n";
    c << "static bool toBool(const ExprValue& v) {\n";
    c << "    if (v.kind == ExprValue::Bool) return v.b;\n";
    c << "    if (v.kind == ExprValue::String) return !v.str.empty();\n";
    c << "    return v.num != 0.0;\n";
    c << "}\n";
    c << "struct ExprParser {\n";
    c << "    " << flowName << "Runtime* rt; std::string s; size_t i = 0; ExprToken cur;\n";
    c << "    ExprParser(" << flowName << "Runtime* r, const std::string& src) : rt(r), s(src) { cur = nextToken(s, i); }\n";
    c << "    void next() { cur = nextToken(s, i); }\n";
    c << "    ExprValue parseExpression() { return parseOr(); }\n";
    c << "    ExprValue parseOr() { ExprValue v = parseAnd(); while (cur.type == ExprToken::Op && cur.text == \"||\") { next(); ExprValue r = parseAnd(); v = {ExprValue::Bool, 0, \"\", toBool(v) || toBool(r)}; } return v; }\n";
    c << "    ExprValue parseAnd() { ExprValue v = parseEquality(); while (cur.type == ExprToken::Op && cur.text == \"&&\") { next(); ExprValue r = parseEquality(); v = {ExprValue::Bool, 0, \"\", toBool(v) && toBool(r)}; } return v; }\n";
    c << "    ExprValue parseEquality() { ExprValue v = parseRelational(); while (cur.type == ExprToken::Op && (cur.text == \"==\" || cur.text == \"!=\")) { std::string op = cur.text; next(); ExprValue r = parseRelational(); bool res = false; if (v.kind == ExprValue::String || r.kind == ExprValue::String) { std::string a = (v.kind == ExprValue::String) ? v.str : std::to_string(v.num); std::string b = (r.kind == ExprValue::String) ? r.str : std::to_string(r.num); res = (a == b); } else { res = (v.num == r.num); } if (op == \"!=\") res = !res; v = {ExprValue::Bool, 0, \"\", res}; } return v; }\n";
    c << "    ExprValue parseRelational() { ExprValue v = parseUnary(); while (cur.type == ExprToken::Op && (cur.text == \">\" || cur.text == \"<\" || cur.text == \">=\" || cur.text == \"<=\")) { std::string op = cur.text; next(); ExprValue r = parseUnary(); bool res = false; double a = v.num; double b = r.num; if (op == \">\") res = a > b; else if (op == \"<\") res = a < b; else if (op == \">=\") res = a >= b; else if (op == \"<=\") res = a <= b; v = {ExprValue::Bool, 0, \"\", res}; } return v; }\n";
    c << "    ExprValue parseUnary() { if (cur.type == ExprToken::Op && cur.text == \"!\") { next(); ExprValue v = parseUnary(); return {ExprValue::Bool, 0, \"\", !toBool(v)}; } return parsePrimary(); }\n";
    c << "    ExprValue parsePrimary() { if (cur.type == ExprToken::Number) { double n = cur.number; next(); return {ExprValue::Number, n, \"\", false}; } if (cur.type == ExprToken::String) { std::string s = cur.text; next(); return {ExprValue::String, 0, s, false}; } if (cur.type == ExprToken::Ident) { std::string id = cur.text; next(); if (rt->varsStr.count(id)) return {ExprValue::String, 0, rt->varsStr[id], false}; if (rt->varsBool.count(id)) return {ExprValue::Bool, 0, \"\", rt->varsBool[id]}; double v = rt->getVar(id, 0.0f); return {ExprValue::Number, v, \"\", false}; } if (cur.type == ExprToken::LParen) { next(); ExprValue v = parseExpression(); if (cur.type == ExprToken::RParen) next(); return v; } return {ExprValue::Number, 0, \"\", false}; }\n";
    c << "};\n";
    c << "bool " << flowName << "Runtime::evalExpression(const std::string& expr) {\n";
    c << "    ExprParser p(this, expr);\n";
    c << "    ExprValue v = p.parseExpression();\n";
    c << "    return toBool(v);\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::start() {\n";
    c << "    if (!states.empty()) { currentState = 0; if (states[0].onEnter) states[0].onEnter(); }\n";
    c << "    if (!steps.empty()) { currentStep = 0; stepTimer = 0.0f; if (steps[0].onStart) steps[0].onStart(); }\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::emit(const FlowEvent& e) {\n";
    c << "    for (auto& t : transitions) {\n";
    c << "        if (t.from == currentState && t.eventName == e.name && checkCondition(t.condition)) {\n";
    c << "            if (states[currentState].onExit) states[currentState].onExit();\n";
    c << "            currentState = t.to;\n";
    c << "            if (states[currentState].onEnter) states[currentState].onEnter();\n";
    c << "            break;\n";
    c << "        }\n";
    c << "    }\n";
    c << "}\n";
    c << "\n";
    c << "static std::vector<std::string> splitFields(const std::string& line) {\n";
    c << "    std::vector<std::string> out; std::string cur;\n";
    c << "    for (char c : line) { if (c == '|') { out.push_back(cur); cur.clear(); } else { cur.push_back(c); } }\n";
    c << "    out.push_back(cur); return out;\n";
    c << "}\n";
    c << "static float toFloat(const std::string& s, float def=0.0f){ try { return std::stof(s); } catch (...) { return def; } }\n";
    c << "static int toInt(const std::string& s, int def=0){ try { return std::stoi(s); } catch (...) { return def; } }\n";
    c << "\n";
    c << "bool " << flowName << "Runtime::loadFromFile(const char* path) {\n";
    c << "    std::ifstream f(path); if (!f) return false;\n";
    c << "    states.clear(); transitions.clear(); steps.clear();\n";
    c << "    std::string line;\n";
    c << "    while (std::getline(f, line)) {\n";
    c << "        auto fields = splitFields(line); if (fields.empty()) continue;\n";
    c << "        if (fields[0] == \"STATE\" && fields.size() >= 4) {\n";
    c << "            std::string onEnter = fields[2]; std::string onExit = fields[3];\n";
    c << "            State s; s.name = fields[1];\n";
    c << "            if (!onEnter.empty()) s.onEnter = [this, onEnter]() { callAction(onEnter); };\n";
    c << "            if (!onExit.empty()) s.onExit = [this, onExit]() { callAction(onExit); };\n";
    c << "            states.push_back(s);\n";
    c << "        } else if (fields[0] == \"TRANS\" && fields.size() >= 5) {\n";
    c << "            Transition t; t.from = toInt(fields[1], -1); t.to = toInt(fields[2], -1);\n";
    c << "            t.eventName = fields[3]; t.condition = fields[4]; transitions.push_back(t);\n";
    c << "        } else if (fields[0] == \"STEP\" && fields.size() >= 6) {\n";
    c << "            Step s; s.name = fields[1]; s.duration = toFloat(fields[2], 1.0f);\n";
    c << "            std::string onStart = fields[3]; std::string onUpdate = fields[4]; std::string onEnd = fields[5];\n";
    c << "            if (!onStart.empty()) s.onStart = [this, onStart]() { callAction(onStart); };\n";
    c << "            if (!onUpdate.empty()) s.onUpdate = [this, onUpdate](float dt) { callUpdateAction(onUpdate, dt); };\n";
    c << "            if (!onEnd.empty()) s.onEnd = [this, onEnd]() { callAction(onEnd); };\n";
    c << "            steps.push_back(s);\n";
    c << "        } else if (fields[0] == \"VAR\" && fields.size() >= 4) {\n";
    c << "            int type = toInt(fields[2], 0);\n";
    c << "            if (type == 0) setVar(fields[1], toFloat(fields[3], 0.0f));\n";
    c << "            else if (type == 1) setVarBool(fields[1], toInt(fields[3], 0) != 0);\n";
    c << "            else setVarString(fields[1], fields[3]);\n";
    c << "        }\n";
    c << "    }\n";
    c << "    start();\n";
    c << "    return true;\n";
    c << "}\n";
    c << "\n";
    c << "void " << flowName << "Runtime::update(float dt) {\n";
    c << "    if (steps.empty()) return;\n";
    c << "    stepTimer += dt;\n";
    c << "    auto& s = steps[currentStep];\n";
    c << "    if (s.onUpdate) s.onUpdate(dt);\n";
    c << "    if (stepTimer >= s.duration) {\n";
    c << "        if (s.onEnd) s.onEnd();\n";
    c << "        currentStep = (currentStep + 1) % (int)steps.size();\n";
    c << "        stepTimer = 0.0f;\n";
    c << "        if (steps[currentStep].onStart) steps[currentStep].onStart();\n";
    c << "    }\n";
    c << "}\n";
    c << "\n";
    c << "void buildFlow(" << flowName << "Runtime& rt) {\n";
    c << "    rt.states.clear();\n";
    c << "    rt.transitions.clear();\n";
    c << "    rt.steps.clear();\n";

    for (auto& s : st.flowStates) {
        c << "    rt.states.push_back({\"" << sanitizeField(s.name) << "\", ";
        if (!s.onEnter.empty()) c << "[&]() { rt.callAction(\"" << sanitizeField(s.onEnter) << "\"); }, ";
        else c << "nullptr, ";
        if (!s.onExit.empty()) c << "[&]() { rt.callAction(\"" << sanitizeField(s.onExit) << "\"); }";
        else c << "nullptr";
        c << "});\n";
    }
    for (auto& t : st.flowTransitions) {
        c << "    rt.transitions.push_back({" << t.fromState << ", " << t.toState
          << ", \"" << sanitizeField(t.eventName) << "\", \"" << sanitizeField(t.condition) << "\"});\n";
    }
    for (auto& s : st.flowSteps) {
        c << "    rt.steps.push_back({\"" << sanitizeField(s.name) << "\", " << s.duration << ", ";
        if (!s.onStart.empty()) c << "[&]() { rt.callAction(\"" << sanitizeField(s.onStart) << "\"); }, ";
        else c << "nullptr, ";
        if (!s.onUpdate.empty()) c << "[&](float dt) { rt.callUpdateAction(\"" << sanitizeField(s.onUpdate) << "\", dt); }, ";
        else c << "nullptr, ";
        if (!s.onEnd.empty()) c << "[&]() { rt.callAction(\"" << sanitizeField(s.onEnd) << "\"); }";
        else c << "nullptr";
        c << "});\n";
    }
    c << "}\n";
    c << "\n";
    c << "void registerFlowActions(" << flowName << "Runtime& rt) {\n";
    c << "    // TODO: register actions, e.g. rt.registerAction(\"OnStart\", [](){ /* ... */ });\n";
    c << "    // TODO: register update actions, e.g. rt.registerUpdateAction(\"OnUpdate\", [](float dt){ /* ... */ });\n";
    c << "    // TODO: register conditions, e.g. rt.registerCondition(\"CanMove\", [](){ return true; });\n";
    c << "}\n";
    c << "\n";
    c << "void initializeFlowVars(" << flowName << "Runtime& rt) {\n";
    for (auto& v : st.flowVars) {
        std::string name = sanitizeField(v.name);
        if (v.type == FlowVar::Type::Number)
            c << "    rt.setVar(\"" << name << "\", " << v.numValue << ");\n";
        else if (v.type == FlowVar::Type::Bool)
            c << "    rt.setVarBool(\"" << name << "\", " << (v.boolValue ? "true" : "false") << ");\n";
        else
            c << "    rt.setVarString(\"" << name << "\", \"" << sanitizeField(v.strValue) << "\");\n";
    }
    c << "}\n";

    std::ofstream hf(hpath);
    if (!hf) { if (err) *err = "Failed to write flow_runtime.h"; return false; }
    hf << h.str();

    std::ofstream cf(cpath);
    if (!cf) { if (err) *err = "Failed to write flow_runtime.cpp"; return false; }
    cf << c.str();

    std::ostringstream th;
    th << "#pragma once\n";
    th << "#include <SDL.h>\n";
    th << "const char* flowEventFromKey(int keycode);\n";

    std::ostringstream tc;
    tc << "#include \"flow_triggers.h\"\n";
    tc << "#include <string>\n";
    tc << "struct TriggerPair { const char* keyName; const char* eventName; };\n";
    tc << "static const TriggerPair kTriggers[] = {\n";
    for (auto& t : st.flowTriggers) {
        tc << "    { \"" << sanitizeField(t.keyName) << "\", \"" << sanitizeField(t.eventName) << "\" },\n";
    }
    tc << "};\n";
    tc << "const char* flowEventFromKey(int keycode) {\n";
    tc << "    for (auto& t : kTriggers) {\n";
    tc << "        SDL_Keycode kc = SDL_GetKeyFromName(t.keyName);\n";
    tc << "        if (kc == keycode) return t.eventName;\n";
    tc << "    }\n";
    tc << "    return nullptr;\n";
    tc << "}\n";

    std::ofstream tf(tpath);
    if (!tf) { if (err) *err = "Failed to write flow_triggers.h"; return false; }
    tf << th.str();

    std::ofstream tcf(tcpath);
    if (!tcf) { if (err) *err = "Failed to write flow_triggers.cpp"; return false; }
    tcf << tc.str();

    return true;
}

inline bool injectFlowRuntimeIntoProject(GameEditorState& st, std::string* err = nullptr) {
    if (st.projectDir.empty()) {
        if (err) *err = "Project dir not set";
        return false;
    }
    std::filesystem::path mainPath = st.projectDir / "src" / "main.cpp";
    if (!std::filesystem::exists(mainPath)) {
        mainPath = st.projectDir / "Scripts" / "main.cpp";
    }
    std::ifstream f(mainPath);
    if (!f) {
        if (err) *err = "main.cpp not found";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();

    bool hasRuntimeInclude = (src.find("flow_runtime.h") != std::string::npos);
    bool hasTriggerInclude = (src.find("flow_triggers.h") != std::string::npos);

    // Insert includes after last include
    if (!hasRuntimeInclude || !hasTriggerInclude) {
        size_t pos = src.rfind("#include");
        if (pos != std::string::npos) {
            size_t lineEnd = src.find('\n', pos);
            if (lineEnd != std::string::npos) {
                std::string inc;
                if (!hasRuntimeInclude) inc += "#include \"flow_runtime.h\"\n";
                if (!hasTriggerInclude) inc += "#include \"flow_triggers.h\"\n";
                src.insert(lineEnd + 1, inc);
            }
        } else {
            std::string inc;
            if (!hasRuntimeInclude) inc += "#include \"flow_runtime.h\"\n";
            if (!hasTriggerInclude) inc += "#include \"flow_triggers.h\"\n";
            src.insert(0, inc);
        }
    }

    std::string flowName = sanitizeIdentifier(st.flowNameBuf);
    // Insert FlowRuntime member
    std::string classMarker = "class MyGame : public GameApp {";
    size_t cpos = src.find(classMarker);
    if (cpos != std::string::npos) {
        std::string member = "\n    " + flowName + "Runtime flow;\n";
        if (src.find(flowName + "Runtime flow;") == std::string::npos) {
            cpos += classMarker.size();
            src.insert(cpos, member);
        }
    }

    // init hook
    std::string initMarker = "void init() override {";
    size_t ipos = src.find(initMarker);
    if (ipos != std::string::npos) {
        if (src.find("registerFlowActions(flow);") == std::string::npos) {
            ipos += initMarker.size();
            src.insert(ipos,
                "\n        registerFlowActions(flow);\n"
                "        buildFlow(flow);\n"
                "        initializeFlowVars(flow);\n"
                "        if (!flow.loadFromFile(\"flow.mye\")) {\n"
                "            flow.start();\n"
                "        }\n");
        } else if (src.find("initializeFlowVars(flow);") == std::string::npos) {
            size_t bpos = src.find("buildFlow(flow);");
            if (bpos != std::string::npos) {
                size_t bend = src.find('\n', bpos);
                if (bend != std::string::npos)
                    src.insert(bend + 1, "        initializeFlowVars(flow);\n");
            }
        }
    }

    // update hook
    std::string updateMarker = "void update(float dt) override {";
    size_t upos = src.find(updateMarker);
    if (upos != std::string::npos) {
        if (src.find("flow.update(dt);") == std::string::npos) {
            upos += updateMarker.size();
            src.insert(upos, "\n        flow.update(dt);\n");
        }
    }

    // onEvent hook
    std::string eventMarker = "void onEvent(const SDL_Event& ev) override {";
    size_t epos = src.find(eventMarker);
    if (epos != std::string::npos) {
        if (src.find("flow.emit({name});") == std::string::npos) {
            epos += eventMarker.size();
            src.insert(epos,
                "\n        if (ev.type == SDL_KEYDOWN) {\n"
                "            if (ev.key.keysym.sym == SDLK_F5) {\n"
                "                initializeFlowVars(flow);\n"
                "                flow.loadFromFile(\"flow.mye\");\n"
                "            }\n"
                "            flow.setVar(\"key\", (float)ev.key.keysym.sym);\n"
                "            flow.setVarString(\"keyName\", SDL_GetKeyName(ev.key.keysym.sym));\n"
                "            if (const char* name = flowEventFromKey(ev.key.keysym.sym)) {\n"
                "                flow.emit({name});\n"
                "            }\n"
                "        }\n");
        } else {
            size_t f5pos = src.find("if (ev.key.keysym.sym == SDLK_F5)");
            if (f5pos != std::string::npos) {
                size_t loadPos = src.find("flow.loadFromFile(\"flow.mye\");", f5pos);
                size_t initPos = src.find("initializeFlowVars(flow);", f5pos);
                if (loadPos != std::string::npos && (initPos == std::string::npos || initPos > loadPos)) {
                    src.insert(loadPos, "                initializeFlowVars(flow);\n");
                }
            }
        }
    }

    std::ofstream out(mainPath);
    if (!out) {
        if (err) *err = "Failed to write main.cpp";
        return false;
    }
    out << src;
    return true;
}

inline myu::engine::Mat4 makeViewMatrix(Camera3DState& cam) {
    if (cam.mode == Camera3DState::Mode::Orbit) {
        myu::engine::Vec3 f = forwardFromYawPitch(cam.yaw, cam.pitch);
        cam.position = {cam.pivot.x - f.x * cam.distance,
                        cam.pivot.y - f.y * cam.distance,
                        cam.pivot.z - f.z * cam.distance};
        return myu::engine::lookAt(cam.position, cam.pivot, {0, 1, 0});
    }
    myu::engine::Vec3 f = forwardFromYawPitch(cam.yaw, cam.pitch);
    return myu::engine::lookAt(cam.position, {cam.position.x + f.x,
        cam.position.y + f.y, cam.position.z + f.z}, {0, 1, 0});
}

inline myu::engine::Mat4 makeProjectionMatrix(const Camera3DState& cam, float aspect) {
    return myu::engine::perspective(cam.fov, aspect, cam.nearPlane, cam.farPlane);
}

inline const ImGuiKey kKeyOptions[] = {
    ImGuiKey_W, ImGuiKey_A, ImGuiKey_S, ImGuiKey_D,
    ImGuiKey_Q, ImGuiKey_E, ImGuiKey_R, ImGuiKey_F,
    ImGuiKey_Space, ImGuiKey_LeftShift, ImGuiKey_LeftCtrl,
    ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
    ImGuiKey_V, ImGuiKey_C, ImGuiKey_Z, ImGuiKey_X
};

inline bool drawKeybindCombo(const char* label, ImGuiKey& key) {
    const char* current = ImGui::GetKeyName(key);
    if (!current) current = "(None)";
    bool changed = false;
    if (ImGui::BeginCombo(label, current)) {
        for (ImGuiKey k : kKeyOptions) {
            const char* name = ImGui::GetKeyName(k);
            if (!name) continue;
            bool selected = (k == key);
            if (ImGui::Selectable(name, selected)) {
                key = k;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

inline myu::engine::Vec3 projectToScreen(const myu::engine::Vec3& p,
                                         const myu::engine::Mat4& view,
                                         const myu::engine::Mat4& proj,
                                         const ImVec2& viewportPos,
                                         const ImVec2& viewportSize) {
    myu::engine::Vec4 clip = myu::engine::multiply(proj, myu::engine::multiply(view,
        myu::engine::Vec4{p.x, p.y, p.z, 1.0f}));
    if (clip.w == 0) return {-999, -999, 0};
    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    float sx = viewportPos.x + (ndcX * 0.5f + 0.5f) * viewportSize.x;
    float sy = viewportPos.y + (1.0f - (ndcY * 0.5f + 0.5f)) * viewportSize.y;
    return {sx, sy, clip.w};
}

inline void updateCameraInput(Camera3DState& cam, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    if (!hovered) return;

    // Zoom
    if (io.MouseWheel != 0) {
        if (cam.mode == Camera3DState::Mode::Orbit) {
            cam.distance *= (io.MouseWheel > 0) ? 0.9f : 1.1f;
            cam.distance = std::clamp(cam.distance, 1.0f, 100.0f);
        } else {
            cam.fov += (io.MouseWheel > 0) ? -2.0f : 2.0f;
            cam.fov = std::clamp(cam.fov, 25.0f, 90.0f);
        }
    }

    // Mouse drag
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        float dx = io.MouseDelta.x * cam.mouseSens;
        float dy = io.MouseDelta.y * cam.mouseSens * (cam.invertY ? -1.0f : 1.0f);
        if (cam.mode == Camera3DState::Mode::Orbit) {
            if (cam.axisMoveMode) {
                // Pan pivot along world axes
                myu::engine::Vec3 f = forwardFromYawPitch(cam.yaw, cam.pitch);
                myu::engine::Vec3 right = rightFromForward(f);
                myu::engine::Vec3 up = {0, 1, 0};
                cam.pivot.x -= (right.x * dx * 0.02f + up.x * dy * 0.02f);
                cam.pivot.y -= (right.y * dx * 0.02f + up.y * dy * 0.02f);
                cam.pivot.z -= (right.z * dx * 0.02f + up.z * dy * 0.02f);
            } else {
                cam.yaw += dx;
                cam.pitch = std::clamp(cam.pitch - dy, -89.0f, 89.0f);
            }
        } else {
            cam.yaw += dx;
            cam.pitch = std::clamp(cam.pitch - dy, -89.0f, 89.0f);
        }
    }

    // Fly mode movement
    if (cam.mode == Camera3DState::Mode::Fly) {
        float speed = cam.moveSpeed * (io.KeyShift ? 2.5f : 1.0f) * io.DeltaTime;
        myu::engine::Vec3 f = forwardFromYawPitch(cam.yaw, cam.pitch);
        myu::engine::Vec3 right = rightFromForward(f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            cam.position = cam.position + f * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            cam.position = cam.position - f * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            cam.position = cam.position - right * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            cam.position = cam.position + right * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) cam.position.y -= speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) cam.position.y += speed;
    }
}

inline void updatePlayerControl(GameEditorState& st, bool hovered) {
    if (!st.playerControlEnabled || !st.selectedObject) return;
    if (!hovered) return;

    ImGuiIO& io = ImGui::GetIO();

    // Toggle view
    if (ImGui::IsKeyPressed(st.keybinds.toggleView)) {
        st.playerView = (st.playerView == PlayerView3D::FirstPerson)
            ? PlayerView3D::ThirdPerson
            : PlayerView3D::FirstPerson;
    }

    // Mouse look
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        float dx = io.MouseDelta.x * st.camera3d.mouseSens;
        float dy = io.MouseDelta.y * st.camera3d.mouseSens * (st.camera3d.invertY ? -1.0f : 1.0f);
        st.camera3d.yaw += dx;
        st.camera3d.pitch = std::clamp(st.camera3d.pitch - dy, -89.0f, 89.0f);
    }

    // Movement
    myu::engine::Vec3 f = forwardFromYawPitch(st.camera3d.yaw, 0.0f);
    myu::engine::Vec3 right = rightFromForward(f);
    myu::engine::Vec3 move = {0, 0, 0};
    if (ImGui::IsKeyDown(st.keybinds.forward)) move = move + f;
    if (ImGui::IsKeyDown(st.keybinds.back))    move = move - f;
    if (ImGui::IsKeyDown(st.keybinds.left))    move = move - right;
    if (ImGui::IsKeyDown(st.keybinds.right))   move = move + right;
    if (ImGui::IsKeyDown(st.keybinds.up))      move.y += 1.0f;
    if (ImGui::IsKeyDown(st.keybinds.down))    move.y -= 1.0f;

    float speed = st.playerSpeed * (ImGui::IsKeyDown(st.keybinds.sprint) ? st.playerSprintMul : 1.0f);
    if (move.x != 0 || move.y != 0 || move.z != 0) {
        move = myu::engine::normalize(move);
        move = move * (speed * io.DeltaTime);
        st.selectedObject->position = st.selectedObject->position + move;
    }

    // Camera follow
    if (st.playerView == PlayerView3D::FirstPerson) {
        st.camera3d.mode = Camera3DState::Mode::Fly;
        st.camera3d.position = st.selectedObject->position + myu::engine::Vec3{0, st.playerHeight, 0};
    } else {
        st.camera3d.mode = Camera3DState::Mode::Orbit;
        st.camera3d.pivot = st.selectedObject->position + myu::engine::Vec3{0, st.thirdPersonHeight, 0};
        st.camera3d.distance = st.thirdPersonDistance;
    }
}

// ─── Scene Hierarchy Panel ──────────────────────────────────────────────────

inline void drawSceneTree(GameEditorState& st, myu::engine::GameObject& obj) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (obj.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (&obj == st.selectedObject) flags |= ImGuiTreeNodeFlags_Selected;

    char label[128];
    std::snprintf(label, sizeof(label), "%s  [%s]",
                  obj.name.c_str(), obj.tag.empty() ? "-" : obj.tag.c_str());

    bool open = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(obj.id)), flags, "%s", label);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        st.selectedObject = &obj;
        std::strncpy(st.objNameBuf, obj.name.c_str(), sizeof(st.objNameBuf)-1);
        std::strncpy(st.objTagBuf,  obj.tag.c_str(),  sizeof(st.objTagBuf)-1);
    }

    if (open) {
        for (auto& c : obj.children) drawSceneTree(st, *c);
        ImGui::TreePop();
    }
}

inline void drawScenePanel(GameEditorState& st) {
    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("+ Object")) {
        st.scene.createObject("New Object");
    }
    ImGui::SameLine();
    if (st.selectedObject && ImGui::Button("Delete")) {
        st.scene.removeObject(st.selectedObject->id);
        st.selectedObject = nullptr;
    }
    ImGui::Separator();

    for (auto& r : st.scene.roots) drawSceneTree(st, *r);
    ImGui::End();
}

// ─── Inspector Panel ────────────────────────────────────────────────────────

inline void drawComponentEditor(myu::engine::Component& comp) {
    ImGui::PushID(comp.typeName.c_str());
    bool hdr = ImGui::CollapsingHeader(comp.typeName.c_str(),
                                       ImGuiTreeNodeFlags_DefaultOpen);
    if (hdr) {
        ImGui::Checkbox("Enabled", &comp.enabled);
        for (auto& prop : comp.properties) {
            ImGui::PushID(prop.name.c_str());
            auto ptype = myu::engine::propTypeOf(prop.value);
            switch (ptype) {
            case myu::engine::PropType::Bool: {
                bool v = std::get<bool>(prop.value);
                if (ImGui::Checkbox(prop.display.c_str(), &v)) prop.value = v;
                break;
            }
            case myu::engine::PropType::Int: {
                int v = std::get<int>(prop.value);
                if (prop.rangeMin != prop.rangeMax)
                    ImGui::SliderInt(prop.display.c_str(), &v,
                        (int)prop.rangeMin, (int)prop.rangeMax);
                else
                    ImGui::DragInt(prop.display.c_str(), &v);
                prop.value = v;
                break;
            }
            case myu::engine::PropType::Float: {
                float v = std::get<float>(prop.value);
                if (prop.rangeMin != prop.rangeMax)
                    ImGui::SliderFloat(prop.display.c_str(), &v,
                        prop.rangeMin, prop.rangeMax);
                else
                    ImGui::DragFloat(prop.display.c_str(), &v, 0.1f);
                prop.value = v;
                break;
            }
            case myu::engine::PropType::String: {
                auto& v = std::get<std::string>(prop.value);
                if (!prop.enumOptions.empty()) {
                    int cur = 0;
                    for (int i = 0; i < (int)prop.enumOptions.size(); ++i)
                        if (prop.enumOptions[i] == v) cur = i;
                    if (ImGui::BeginCombo(prop.display.c_str(), v.c_str())) {
                        for (int i = 0; i < (int)prop.enumOptions.size(); ++i)
                            if (ImGui::Selectable(prop.enumOptions[i].c_str(), i==cur))
                                prop.value = prop.enumOptions[i];
                        ImGui::EndCombo();
                    }
                } else {
                    char buf[256]; std::strncpy(buf, v.c_str(), sizeof(buf)-1);
                    buf[sizeof(buf)-1] = '\0';
                    if (ImGui::InputText(prop.display.c_str(), buf, sizeof(buf)))
                        prop.value = std::string(buf);
                }
                break;
            }
            case myu::engine::PropType::Vec3: {
                auto& v = std::get<myu::engine::Vec3>(prop.value);
                ImGui::DragFloat3(prop.display.c_str(), &v.x, 0.1f);
                break;
            }
            case myu::engine::PropType::Color: {
                auto& v = std::get<myu::engine::Color>(prop.value);
                ImGui::ColorEdit4(prop.display.c_str(), &v.r);
                break;
            }
            }
            if (!prop.tooltip.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", prop.tooltip.c_str());
            ImGui::PopID();
        }
    }
    ImGui::PopID();
}

inline void drawInspector(GameEditorState& st) {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    if (!st.selectedObject) {
        ImGui::TextDisabled("Select an object in the Scene panel");
        ImGui::End(); return;
    }
    auto& obj = *st.selectedObject;

    // Identity
    if (ImGui::CollapsingHeader("Identity", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::InputText("Name", st.objNameBuf, sizeof(st.objNameBuf)))
            obj.name = st.objNameBuf;
        if (ImGui::InputText("Tag",  st.objTagBuf,  sizeof(st.objTagBuf)))
            obj.tag = st.objTagBuf;
        ImGui::Text("ID: %u  Layer: %d", obj.id, obj.layer);
        ImGui::DragInt("Layer", &obj.layer);
        ImGui::Checkbox("Active", &obj.active);
        ImGui::SameLine();
        ImGui::Checkbox("Visible", &obj.visible);
    }

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &obj.position.x, 0.1f);
        ImGui::DragFloat3("Rotation", &obj.rotation.x, 0.5f);
        ImGui::DragFloat3("Scale",    &obj.scale.x,    0.01f);
    }

    // Sprite shortcut
    if (ImGui::CollapsingHeader("Appearance")) {
        char sp[256]; std::strncpy(sp, obj.spritePath.c_str(), sizeof(sp)-1); sp[255]=0;
        if (ImGui::InputText("Sprite Path", sp, sizeof(sp))) obj.spritePath = sp;
        ImGui::ColorEdit4("Tint", &obj.tint.r);
        ImGui::DragFloat("Width",  &obj.width,  0.1f, 0.01f, 100.0f);
        ImGui::DragFloat("Height", &obj.height, 0.1f, 0.01f, 100.0f);
    }

    // 3D Resource assignment
    if (obj.tag == "3d" || obj.tag == "model" || obj.tag == "bbmodel") {
        if (ImGui::CollapsingHeader("3D Rendering")) {
            char mpBuf[256]; std::strncpy(mpBuf, obj.modelPath.c_str(), sizeof(mpBuf)-1); mpBuf[255]=0;
            char matBuf[128]; std::strncpy(matBuf, obj.materialName.c_str(), sizeof(matBuf)-1); matBuf[127]=0;
            if (ImGui::InputText("Model Path", mpBuf, sizeof(mpBuf))) obj.modelPath = mpBuf;
            if (ImGui::InputText("Material", matBuf, sizeof(matBuf))) obj.materialName = matBuf;
            if (st.resources) {
                std::vector<const char*> modelNames;
                std::vector<std::string> modelNameBuf;
                std::vector<const char*> matNames;
                std::vector<std::string> matNameBuf;
                for (auto& e : st.resources->entries()) {
                    if (e.type == myu::engine::ResourceType::Model) {
                        modelNameBuf.push_back(e.name);
                    }
                    if (e.type == myu::engine::ResourceType::Material) {
                        matNameBuf.push_back(e.name);
                    }
                }
                for (auto& s : modelNameBuf) modelNames.push_back(s.c_str());
                for (auto& s : matNameBuf) matNames.push_back(s.c_str());

                if (!modelNames.empty()) {
                    int cur = 0;
                    for (int i = 0; i < (int)modelNameBuf.size(); ++i)
                        if (modelNameBuf[i] == obj.modelPath) cur = i;
                    if (ImGui::Combo("Model Resource", &cur, modelNames.data(), (int)modelNames.size())) {
                        obj.modelPath = modelNameBuf[cur];
                        obj.tag = "model";
                    }
                } else {
                    ImGui::TextDisabled("No model resources available.");
                }

                if (!matNames.empty()) {
                    int cur = 0;
                    for (int i = 0; i < (int)matNameBuf.size(); ++i)
                        if (matNameBuf[i] == obj.materialName) cur = i;
                    if (ImGui::Combo("Material Resource", &cur, matNames.data(), (int)matNames.size()))
                        obj.materialName = matNameBuf[cur];
                }
            }
            ImGui::TextDisabled("Current model: %s", obj.modelPath.empty() ? "(none)" : obj.modelPath.c_str());
            if (!obj.modelPath.empty() && ImGui::Button("Reload Model")) {
                if (st.resources) {
                    std::string err;
                    std::string path = obj.modelPath;
                    auto res = st.resources->findByName(myu::engine::ResourceType::Model, obj.modelPath);
                    if (auto* e = st.resources->get(res)) path = e->path;
                    std::filesystem::path fullPath = resolveResourcePath(st, path);
                    loadModelToGPU(st, obj.modelPath, fullPath.string(), err);
                }
            }
        }
    }

    // Components
    ImGui::Separator();
    for (auto& comp : obj.components) drawComponentEditor(comp);

    // Add component button
    if (ImGui::Button("+ Add Component")) ImGui::OpenPopup("AddComp");
    if (ImGui::BeginPopup("AddComp")) {
        const char* types[] = {"Sprite","BoxCollider","Animator","AudioSource","Label","Custom"};
        for (auto* t : types) {
            if (ImGui::MenuItem(t)) {
                if (std::string(t) == "Sprite")
                    obj.addComponent(myu::engine::components::sprite());
                else if (std::string(t) == "BoxCollider")
                    obj.addComponent(myu::engine::components::boxCollider());
                else if (std::string(t) == "Animator")
                    obj.addComponent(myu::engine::components::animator());
                else if (std::string(t) == "AudioSource")
                    obj.addComponent(myu::engine::components::audioSource());
                else if (std::string(t) == "Label")
                    obj.addComponent(myu::engine::components::label());
                else
                    obj.requireComponent(t);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

// ─── Board Editor Panel ────────────────────────────────────────────────────

inline void drawBoardEditor(GameEditorState& st, bool allowHeavy = true) {
    if (!ImGui::Begin("Board Editor")) {
        ImGui::End();
        return;
    }
    auto& b = st.board;
    bool drawGrid = st.perf.drawBoardGrid && allowHeavy;
    bool drawLabels = st.perf.drawBoardLabels && allowHeavy;
    bool drawPieces = st.perf.drawBoardPieces && allowHeavy;
    bool drawHighlights = st.perf.drawBoardHighlights && allowHeavy;

    // ── Controls ──
    ImGui::Text("Grid: %d x %d", b.rows, b.cols);
    ImGui::SameLine();
    static int newR = 8, newC = 8;
    ImGui::SetNextItemWidth(50); ImGui::DragInt("##nr", &newR, 1, 2, 20);
    ImGui::SameLine(); ImGui::Text("x");
    ImGui::SameLine(); ImGui::SetNextItemWidth(50); ImGui::DragInt("##nc", &newC, 1, 2, 20);
    ImGui::SameLine();
    if (ImGui::Button("Resize")) { b.init(newR, newC); b.clearPieces(); }

    // Cell paint type
    ImGui::Text("Paint:");
    ImGui::SameLine();
    const char* ctNames[] = {"Normal","Blocked","Special","Start","Goal"};
    int ct = static_cast<int>(st.paintCellType);
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##ct", &ct, ctNames, 5))
        st.paintCellType = static_cast<myu::game::CellType>(ct);

    // Piece type management
    ImGui::Separator();
    ImGui::Text("Piece Types (%d)", (int)b.pieceTypes.size());
    ImGui::SameLine();
    if (ImGui::Button("+ Type")) {
        b.addPieceType("Piece " + std::to_string(b.pieceTypes.size()));
    }

    if (!b.pieceTypes.empty()) {
        ImGui::SetNextItemWidth(120);
        if (ImGui::BeginCombo("##ptsel",
                st.selectedPieceType >= 0 && st.selectedPieceType < (int)b.pieceTypes.size()
                ? b.pieceTypes[st.selectedPieceType].name.c_str() : "Select...")) {
            for (int i = 0; i < (int)b.pieceTypes.size(); ++i) {
                if (ImGui::Selectable(b.pieceTypes[i].name.c_str(), i == st.selectedPieceType))
                    st.selectedPieceType = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        int own = static_cast<int>(st.placeOwner);
        ImGui::SetNextItemWidth(90);
        const char* ownNames[] = {"Player 1","Player 2","Neutral"};
        if (ImGui::Combo("##own", &own, ownNames, 3))
            st.placeOwner = static_cast<myu::game::PieceOwner>(own);
    }

    // Selected piece type properties
    if (st.selectedPieceType >= 0 && st.selectedPieceType < (int)b.pieceTypes.size()) {
        auto& pt = b.pieceTypes[st.selectedPieceType];
        ImGui::PushID("ptedit");
        std::strncpy(st.ptNameBuf, pt.name.c_str(), sizeof(st.ptNameBuf)-1);
        if (ImGui::InputText("Name", st.ptNameBuf, sizeof(st.ptNameBuf)))
            pt.name = st.ptNameBuf;
        ImGui::DragInt("Move Range",  &pt.moveRange, 1, 1, 10);
        ImGui::Checkbox("Diagonal", &pt.canDiagonal);
        ImGui::SameLine(); ImGui::Checkbox("Jump", &pt.canJump);
        ImGui::DragInt("ATK", &pt.attack, 1, 0, 99);
        ImGui::SameLine(); ImGui::DragInt("DEF", &pt.defense, 1, 0, 99);
        ImGui::SameLine(); ImGui::DragInt("HP",  &pt.hp, 1, 1, 99);
        ImGui::ColorEdit3("Color", &pt.colorR);
        ImGui::PopID();
    }

    // ── Board Grid ──
    ImGui::Separator();
    if (!drawGrid) {
        ImGui::TextDisabled("Board grid rendering disabled (Settings or Idle)");
        ImGui::End();
        return;
    }
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float cellSz = std::min(avail.x / (b.cols + 1), (avail.y - 20) / (b.rows + 1));
    cellSz = std::max(cellSz, 16.0f);
    cellSz = std::min(cellSz, 60.0f);

    ImVec2 gridOrigin = ImGui::GetCursorScreenPos();
    ImVec2 gridSize(cellSz * b.cols, cellSz * b.rows);
    ImGui::InvisibleButton("##boardgrid", gridSize);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGuiIO& io = ImGui::GetIO();

    for (int r = 0; r < b.rows; ++r)
        for (int c = 0; c < b.cols; ++c) {
            auto* cell = b.getCell(r, c);
            if (!cell) continue;

            ImVec2 p0(gridOrigin.x + c * cellSz, gridOrigin.y + r * cellSz);
            ImVec2 p1(p0.x + cellSz, p0.y + cellSz);

            // Cell background
            ImU32 bg = colF(cell->colorR, cell->colorG, cell->colorB, cell->colorA);
            if (cell->type == myu::game::CellType::Blocked)
                bg = IM_COL32(40, 40, 40, 255);
            else if (cell->type == myu::game::CellType::Special)
                bg = IM_COL32(80, 40, 120, 255);
            else if (cell->type == myu::game::CellType::Start)
                bg = IM_COL32(40, 100, 60, 255);
            else if (cell->type == myu::game::CellType::Goal)
                bg = IM_COL32(120, 100, 30, 255);

            dl->AddRectFilled(p0, p1, bg);
            dl->AddRect(p0, p1, IM_COL32(60, 60, 70, 255));

            // Highlight
            if (drawHighlights && cell->highlighted)
                dl->AddRectFilled(p0, p1, IM_COL32(0, 200, 100, 60));

            // Piece
            auto* piece = b.getPieceAt(r, c);
            if (drawPieces && piece) {
                auto* pt = b.getPieceType(piece->typeId);
                float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
                float pr = cellSz * 0.35f;
                ImU32 pcol = pt ? colF(pt->colorR, pt->colorG, pt->colorB)
                               : IM_COL32(200, 200, 200, 255);
                if (piece->owner == myu::game::PieceOwner::Player2)
                    pcol = IM_COL32(255, 80, 80, 255);
                dl->AddCircleFilled(ImVec2(cx, cy), pr, pcol);
                dl->AddCircle(ImVec2(cx, cy), pr, IM_COL32(255,255,255,120), 0, 2.0f);
                if (pt) {
                    const char* letter = pt->name.c_str();
                    char ch[2] = {letter[0], 0};
                    dl->AddText(ImVec2(cx - 4, cy - 6), IM_COL32(255,255,255,220), ch);
                }
            }

            // Mouse interaction
            if (ImGui::IsItemHovered()) {
                ImVec2 mousePos = io.MousePos;
                if (mousePos.x >= p0.x && mousePos.x < p1.x &&
                    mousePos.y >= p0.y && mousePos.y < p1.y) {
                    // Tooltip
                    ImGui::SetTooltip("(%d, %d) %s%s", r, c,
                        myu::game::cellTypeName(cell->type),
                        piece ? (" - " + b.pieceTypes[piece->typeId].name).c_str() : "");

                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        // Left click: place piece if type selected, otherwise paint cell
                        if (st.selectedPieceType >= 0 && cell->isEmpty()) {
                            b.placePiece(st.selectedPieceType, st.placeOwner, r, c);
                        } else {
                            cell->type = st.paintCellType;
                        }
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        // Right click: remove piece or reset cell
                        if (piece) {
                            piece->alive = false;
                            cell->occupantId = -1;
                        } else {
                            cell->type = myu::game::CellType::Normal;
                            bool dark = (r + c) % 2 == 1;
                            cell->colorR = dark ? b.darkR : b.lightR;
                            cell->colorG = dark ? b.darkG : b.lightG;
                            cell->colorB = dark ? b.darkB : b.lightB;
                        }
                    }
                }
            }
        }

    // Row/Col labels
    if (drawLabels) {
        for (int c = 0; c < b.cols; ++c) {
            char lbl[4]; std::snprintf(lbl, sizeof(lbl), "%c", 'A' + c);
            dl->AddText(ImVec2(gridOrigin.x + c * cellSz + cellSz * 0.4f,
                               gridOrigin.y - 14), IM_COL32(180,180,180,200), lbl);
        }
        for (int r = 0; r < b.rows; ++r) {
            char lbl[16]; std::snprintf(lbl, sizeof(lbl), "%d", r + 1);
            dl->AddText(ImVec2(gridOrigin.x - 14,
                               gridOrigin.y + r * cellSz + cellSz * 0.3f),
                        IM_COL32(180,180,180,200), lbl);
        }
    }

    // Status bar
    int alive = 0;
    for (auto& p : b.pieces) if (p.alive) alive++;
    ImGui::Text("Pieces: %d  |  Moves: %d  |  Right-click: remove",
                alive, (int)b.moveHistory.size());
    if (ImGui::Button("Clear All Pieces")) b.clearPieces();
    ImGui::SameLine();
    if (!b.moveHistory.empty() && ImGui::Button("Undo Last Move")) b.undoMove();

    ImGui::End();
}

// ─── Card Editor Panel ─────────────────────────────────────────────────────

inline void drawCardEditor(GameEditorState& st, bool allowHeavy = true) {
    if (!ImGui::Begin("Card Editor")) {
        ImGui::End();
        return;
    }
    auto& lib = st.cardLibrary;

    // ── Template list (left) ──
    ImGui::BeginChild("##cardlist", ImVec2(180, 0), ImGuiChildFlags_Border);
    if (ImGui::Button("+ New Card", ImVec2(-1, 0)))
        st.selectedCardTmpl = lib.addTemplate("New Card");

    for (int i = 0; i < (int)lib.templates.size(); ++i) {
        bool sel = (i == st.selectedCardTmpl);
        auto& ct = lib.templates[i];
        uint32_t rc = myu::game::rarityColorHex(ct.rarity);
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(((rc>>16)&0xFF)/255.f, ((rc>>8)&0xFF)/255.f,
                   (rc&0xFF)/255.f, 1.0f));
        if (ImGui::Selectable(ct.name.c_str(), sel)) {
            st.selectedCardTmpl = i;
            std::strncpy(st.cardNameBuf, ct.name.c_str(), sizeof(st.cardNameBuf)-1);
            std::strncpy(st.cardDescBuf, ct.description.c_str(), sizeof(st.cardDescBuf)-1);
            std::strncpy(st.cardArtBuf,  ct.artPath.c_str(),  sizeof(st.cardArtBuf)-1);
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ── Card details (right) ──
    ImGui::BeginChild("##cardedit");
    if (st.selectedCardTmpl >= 0 && st.selectedCardTmpl < (int)lib.templates.size()) {
        auto& ct = lib.templates[st.selectedCardTmpl];

        // Card preview (visual)
        bool drawPreview = st.perf.drawCardPreview && allowHeavy;
        if (drawPreview) {
            ImVec2 cardP0 = ImGui::GetCursorScreenPos();
            float cw = 160, ch = 220;
            ImVec2 cardP1(cardP0.x + cw, cardP0.y + ch);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(cardP0, cardP1, colF(ct.colorR, ct.colorG, ct.colorB), 8.0f);
            dl->AddRect(cardP0, cardP1,
                IM_COL32(180, 180, 200, 255), 8.0f, 0, 2.0f);

            // Cost badge
            char costStr[8]; std::snprintf(costStr, sizeof(costStr), "%d", ct.cost);
            dl->AddCircleFilled(ImVec2(cardP0.x + 20, cardP0.y + 20), 14,
                                IM_COL32(30, 80, 200, 255));
            dl->AddText(ImVec2(cardP0.x + 15, cardP0.y + 13),
                        IM_COL32(255,255,255,255), costStr);

            // Art area
            dl->AddRectFilled(
                ImVec2(cardP0.x + 10, cardP0.y + 40),
                ImVec2(cardP0.x + cw - 10, cardP0.y + 120),
                IM_COL32(50, 50, 60, 255), 4.0f);
            if (ct.artPath.empty())
                dl->AddText(ImVec2(cardP0.x + 50, cardP0.y + 72),
                            IM_COL32(120,120,120,200), "[ART]");

            // Name
            dl->AddText(ImVec2(cardP0.x + 10, cardP0.y + 125),
                        IM_COL32(255,255,255,255), ct.name.c_str());

            // Rarity
            uint32_t rc = myu::game::rarityColorHex(ct.rarity);
            dl->AddText(ImVec2(cardP0.x + 10, cardP0.y + 145),
                colF(((rc>>16)&0xFF)/255.f,((rc>>8)&0xFF)/255.f,
                     (rc&0xFF)/255.f),
                myu::game::rarityName(ct.rarity));

            // Description
            ImVec2 descPos(cardP0.x + 10, cardP0.y + 162);
            dl->AddText(nullptr, 0, descPos,
                IM_COL32(200,200,200,255), ct.description.c_str(),
                ct.description.c_str() + std::min<size_t>(ct.description.size(), 80),
                cw - 20);

            // Stats (for Unit cards)
            if (ct.type == "Unit") {
                char stats[32]; std::snprintf(stats, sizeof(stats),
                    "ATK:%d DEF:%d HP:%d", ct.attack, ct.defense, ct.hp);
                dl->AddText(ImVec2(cardP0.x + 10, cardP0.y + ch - 25),
                            IM_COL32(255,220,100,255), stats);
            }

            ImGui::Dummy(ImVec2(cw + 10, ch + 5));
        } else {
            ImGui::TextDisabled("Card preview disabled (Settings or Idle)");
            ImGui::Dummy(ImVec2(170, 230));
        }

        // Edit form
        ImGui::Separator();
        if (ImGui::InputText("Name", st.cardNameBuf, sizeof(st.cardNameBuf)))
            ct.name = st.cardNameBuf;

        const char* typeOpts[] = {"Spell","Unit","Equipment","Trap"};
        int curType = 0;
        for (int i = 0; i < 4; ++i) if (ct.type == typeOpts[i]) curType = i;
        if (ImGui::Combo("Type", &curType, typeOpts, 4)) ct.type = typeOpts[curType];

        ImGui::DragInt("Cost", &ct.cost, 1, 0, 20);

        int rar = static_cast<int>(ct.rarity);
        const char* rarOpts[] = {"Common","Uncommon","Rare","Epic","Legendary"};
        if (ImGui::Combo("Rarity", &rar, rarOpts, 5))
            ct.rarity = static_cast<myu::game::CardRarity>(rar);

        if (ct.type == "Unit") {
            ImGui::DragInt("Attack",  &ct.attack,  1, 0, 99);
            ImGui::DragInt("Defense", &ct.defense, 1, 0, 99);
            ImGui::DragInt("HP",      &ct.hp,      1, 1, 99);
        }

        if (ImGui::InputTextMultiline("Description", st.cardDescBuf,
                sizeof(st.cardDescBuf), ImVec2(-1, 60)))
            ct.description = st.cardDescBuf;

        if (ImGui::InputText("Art Path", st.cardArtBuf, sizeof(st.cardArtBuf)))
            ct.artPath = st.cardArtBuf;

        ImGui::ColorEdit3("Card Color", &ct.colorR);

        // Effects
        ImGui::Separator();
        ImGui::Text("Effects (%d)", (int)ct.effects.size());
        ImGui::SameLine();
        if (ImGui::Button("+ Effect"))
            ct.effects.push_back({});

        for (int i = 0; i < (int)ct.effects.size(); ++i) {
            ImGui::PushID(i);
            auto& eff = ct.effects[i];
            int et = static_cast<int>(eff.type);
            ImGui::SetNextItemWidth(120);
            const char* effectNames[] = {"None","DealDamage","Heal","Buff","Debuff",
                "DrawCard","Summon","MovePiece","DestroyPiece","Shield","Custom"};
            if (ImGui::Combo("##etype", &et, effectNames, 11))
                eff.type = static_cast<myu::game::EffectType>(et);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::DragInt("##eval", &eff.value, 1, 0, 99);
            ImGui::SameLine();
            if (ImGui::Button("X")) {
                ct.effects.erase(ct.effects.begin() + i); --i;
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("Delete Card")) {
            lib.templates.erase(lib.templates.begin() + st.selectedCardTmpl);
            st.selectedCardTmpl = -1;
        }
    } else {
        ImGui::TextDisabled("Select or create a card template");
    }
    ImGui::EndChild();
    ImGui::End();
}

// ─── Game Systems Panel ────────────────────────────────────────────────────

inline void drawGameSystems(GameEditorState& st) {
    if (!ImGui::Begin("Game Systems")) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##systabs")) {
        // ── Turn ──
        if (ImGui::BeginTabItem("Turns")) {
            auto& tm = st.turnManager;
            ImGui::DragInt("Players", &tm.numPlayers, 1, 2, 4);
            ImGui::Text("Current: Turn %d, Player %d, Phase: %s",
                tm.currentTurn, tm.currentPlayer + 1,
                myu::game::phaseName(tm.currentPhase));
            if (ImGui::Button("Start Game")) tm.startGame();
            ImGui::SameLine();
            if (ImGui::Button("Next Phase")) tm.nextPhase();
            ImGui::SameLine();
            if (ImGui::Button("End Turn")) tm.endTurn();

            ImGui::Separator();
            ImGui::TextWrapped("Turn flow: Start → Main 1 → Battle → Main 2 → End");
            ImGui::EndTabItem();
        }

        // ── Timer ──
        if (ImGui::BeginTabItem("Timer")) {
            auto& t = st.timer;
            ImGui::DragFloat("Turn Time Limit", &t.turnTimeLimit, 1, 0, 600, "%.0f sec");
            ImGui::DragFloat("Game Time Limit", &t.gameTimeLimit, 1, 0, 3600, "%.0f sec");
            ImGui::Text("Remaining: %s", t.formatTime(t.turnTimeRemaining).c_str());
            ImGui::Text("P1 Total: %s  |  P2 Total: %s",
                t.formatTime(t.player1Time).c_str(),
                t.formatTime(t.player2Time).c_str());
            if (ImGui::Button("Start")) t.start();
            ImGui::SameLine();
            if (ImGui::Button("Pause")) t.pause();
            ImGui::SameLine();
            if (ImGui::Button("Reset Turn")) t.resetTurn();
            ImGui::EndTabItem();
        }

        // ── Score ──
        if (ImGui::BeginTabItem("Score")) {
            auto& s = st.score;
            ImGui::DragInt("Win Score (0=no limit)", &s.winScore, 1, 0, 9999);
            ImGui::Text("Player 1: %d  |  Player 2: %d", s.player1Score, s.player2Score);
            if (s.gameOver)
                ImGui::TextColored(ImVec4(1,1,0,1), "GAME OVER! Winner: Player %d", s.winner+1);

            static int addPlayer = 0, addPoints = 1;
            ImGui::SetNextItemWidth(80);
            const char* pn[] = {"Player 1","Player 2"};
            ImGui::Combo("##sp", &addPlayer, pn, 2);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::DragInt("##pts", &addPoints, 1, -99, 99);
            ImGui::SameLine();
            if (ImGui::Button("Add Score"))
                s.addScore(addPlayer, addPoints, "manual");
            ImGui::SameLine();
            if (ImGui::Button("Reset")) s.reset();

            // History
            if (!s.history.empty()) {
                ImGui::Separator();
                ImGui::Text("History:");
                for (auto& ev : s.history)
                    ImGui::BulletText("P%d %+d %s", ev.player+1, ev.delta, ev.reason.c_str());
            }
            ImGui::EndTabItem();
        }

        // ── Shop ──
        if (ImGui::BeginTabItem("Shop")) {
            auto& shop = st.shop;
            ImGui::Text("P1 Gold: %d  |  P2 Gold: %d", shop.player1Gold, shop.player2Gold);
            ImGui::DragInt("P1 Gold##g1", &shop.player1Gold, 1, 0, 99999);
            ImGui::DragInt("P2 Gold##g2", &shop.player2Gold, 1, 0, 99999);

            ImGui::Separator();
            ImGui::Text("Items (%d)", (int)shop.items.size());
            if (ImGui::Button("+ Add Item")) {
                shop.addItem("New Item", 10, "card");
            }

            for (int i = 0; i < (int)shop.items.size(); ++i) {
                auto& item = shop.items[i];
                ImGui::PushID(i);
                char nb[64]; std::strncpy(nb, item.name.c_str(), sizeof(nb)-1); nb[63]=0;
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputText("##sname", nb, sizeof(nb))) item.name = nb;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                ImGui::DragInt("$##price", &item.price, 1, 0, 9999);
                ImGui::SameLine();
                ImGui::Checkbox("Avail", &item.available);
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    shop.items.erase(shop.items.begin() + i); --i;
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        // ── Audio ──
        if (ImGui::BeginTabItem("Audio")) {
            auto& a = st.audio;
            std::strncpy(st.bgmBuf, a.bgmPath.c_str(), sizeof(st.bgmBuf)-1);
            if (ImGui::InputText("BGM Path", st.bgmBuf, sizeof(st.bgmBuf)))
                a.bgmPath = st.bgmBuf;
            ImGui::SliderFloat("BGM Volume", &a.bgmVolume, 0, 1);
            ImGui::Checkbox("BGM Loop", &a.bgmLoop);

            ImGui::Separator();
            ImGui::Text("SFX (%d)", (int)a.sfx.size());
            if (ImGui::Button("+ Add SFX"))
                a.sfx.push_back({"sfx_" + std::to_string(a.sfx.size()), "", 1.0f});

            for (int i = 0; i < (int)a.sfx.size(); ++i) {
                ImGui::PushID(i);
                auto& s = a.sfx[i];
                char nb[64]; std::strncpy(nb, s.name.c_str(), sizeof(nb)-1); nb[63]=0;
                char pb[256]; std::strncpy(pb, s.path.c_str(), sizeof(pb)-1); pb[255]=0;
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputText("##sfxn", nb, sizeof(nb))) s.name = nb;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                if (ImGui::InputText("##sfxp", pb, sizeof(pb))) s.path = pb;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                ImGui::SliderFloat("##sfxv", &s.volume, 0, 1);
                ImGui::SameLine();
                if (ImGui::Button("X")) { a.sfx.erase(a.sfx.begin()+i); --i; }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ─── Game Viewport (2.5D Preview) ──────────────────────────────────────────

inline void drawGameViewport(GameEditorState& st, bool allowHeavy = true) {
    if (!ImGui::Begin("Game Viewport")) {
        ImGui::End();
        return;
    }
    auto& cam = st.camera;
    auto& b   = st.board;

    // Camera controls
    ImGui::SliderFloat("Zoom", &cam.zoom, 0.2f, 4.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::SliderFloat("Orbit", &cam.orbitAngle, -180, 180, "%.0f°");
    ImGui::SameLine();
    ImGui::SliderFloat("Tilt", &cam.tiltAngle, 15, 75, "%.0f°");
    ImGui::SameLine();
    if (ImGui::Button("Reset Cam")) {
        cam.zoom = 1; cam.orbitAngle = 0; cam.tiltAngle = 45;
        cam.targetX = b.cols * b.cellSize * 0.5f;
        cam.targetY = b.rows * b.cellSize * 0.5f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Shake!")) cam.shake(10, 0.5f);

    if (!allowHeavy) {
        ImGui::TextDisabled("Viewport paused (Idle)");
        ImGui::End();
        return;
    }

    // Canvas
    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50) canvasSize.x = 50;
    if (canvasSize.y < 50) canvasSize.y = 50;

    ImGui::InvisibleButton("##gvp", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(18, 22, 35, 255));

    // Update camera
    cam.update(1.0f / 60.0f);

    float centerX = canvasPos.x + canvasSize.x * 0.5f;
    float centerY = canvasPos.y + canvasSize.y * 0.5f;

    // Zoom with scroll
    if (hovered) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0) {
            cam.zoom *= (io.MouseWheel > 0) ? 1.1f : 0.9f;
            cam.zoom = std::clamp(cam.zoom, 0.2f, 5.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            cam.targetX -= io.MouseDelta.x / (cam.smoothZoom * 40.0f);
            cam.targetY -= io.MouseDelta.y / (cam.smoothZoom * 40.0f);
        }
    }

    float scale = st.perf.viewportScale; // pixels per world unit
    bool drawGrid = st.perf.drawViewportGrid && allowHeavy;
    bool drawPieces = st.perf.drawViewportPieces && allowHeavy;
    bool drawHighlights = st.perf.drawBoardHighlights && allowHeavy;
    bool drawShadows = st.perf.drawViewportShadows && allowHeavy;

    // Draw board cells
    for (int r = 0; r < b.rows; ++r)
        for (int c = 0; c < b.cols; ++c) {
            auto* cell = b.getCell(r, c);
            if (!cell) continue;

            // Get 4 corners of cell in world space
            float wx0 = c * b.cellSize, wy0 = r * b.cellSize;
            float wx1 = wx0 + b.cellSize, wy1 = wy0 + b.cellSize;

            // Project corners
            float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;
            cam.worldToScreen(wx0, wy0, 0, sx0, sy0);
            cam.worldToScreen(wx1, wy0, 0, sx1, sy1);
            cam.worldToScreen(wx1, wy1, 0, sx2, sy2);
            cam.worldToScreen(wx0, wy1, 0, sx3, sy3);

            ImVec2 pts[4] = {
                {centerX + sx0 * scale, centerY + sy0 * scale},
                {centerX + sx1 * scale, centerY + sy1 * scale},
                {centerX + sx2 * scale, centerY + sy2 * scale},
                {centerX + sx3 * scale, centerY + sy3 * scale},
            };

            ImU32 bg;
            if (cell->type == myu::game::CellType::Blocked)
                bg = IM_COL32(30, 30, 30, 255);
            else
                bg = colF(cell->colorR, cell->colorG, cell->colorB, cell->colorA);

            dl->AddConvexPolyFilled(pts, 4, bg);
            if (drawGrid)
                dl->AddPolyline(pts, 4, IM_COL32(80, 80, 100, 150), ImDrawFlags_Closed, 1.0f);

            if (drawHighlights && cell->highlighted)
                dl->AddConvexPolyFilled(pts, 4, IM_COL32(0, 200, 100, 50));
        }

    // Draw pieces
    for (auto& piece : b.pieces) {
        if (!piece.alive) continue;
        if (!drawPieces) continue;
        float wx = (piece.col + 0.5f) * b.cellSize;
        float wy = (piece.row + 0.5f) * b.cellSize;
        float sx, sy;
        cam.worldToScreen(wx, wy, 0.3f, sx, sy); // slight elevation

        float pr = b.cellSize * 0.35f * scale * cam.smoothZoom;
        ImVec2 center(centerX + sx * scale, centerY + sy * scale);

        auto* pt = b.getPieceType(piece.typeId);
        ImU32 pcol = pt ? colF(pt->colorR, pt->colorG, pt->colorB)
                       : IM_COL32(200,200,200,255);
        if (piece.owner == myu::game::PieceOwner::Player2) {
            // Adjust color for P2
            pcol = IM_COL32(
                std::min(255, (int)((pt ? pt->colorR : 0.8f) * 255 * 0.6f + 100)),
                (int)((pt ? pt->colorG : 0.2f) * 255 * 0.5f),
                (int)((pt ? pt->colorB : 0.2f) * 255 * 0.5f),
                255);
        }

        // Shadow
        if (drawShadows) {
            dl->AddCircleFilled(ImVec2(center.x + 2, center.y + 3), pr * 0.9f,
                                IM_COL32(0, 0, 0, 80));
        }
        // Body
        dl->AddCircleFilled(center, pr, pcol);
        dl->AddCircle(center, pr, IM_COL32(255,255,255,100), 0, 1.5f);

        // Label
        if (pt && pr > 8) {
            char ch[2] = {pt->name[0], 0};
            dl->AddText(ImVec2(center.x - 4, center.y - 6),
                        IM_COL32(255,255,255,230), ch);
        }
    }

    ImGui::End();
}

// ─── 3D Viewport (OpenGL) ─────────────────────────────────────────────────

inline void render3DScene(GameEditorState& st, const myu::engine::Mat4& view,
                          const myu::engine::Mat4& proj) {
    auto& vr = st.viewport3d;
    glBindFramebuffer(GL_FRAMEBUFFER, vr.fbo);
    glViewport(0, 0, vr.width, vr.height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.1f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(vr.program);
    GLint uModel = glGetUniformLocation(vr.program, "uModel");
    GLint uView  = glGetUniformLocation(vr.program, "uView");
    GLint uProj  = glGetUniformLocation(vr.program, "uProjection");
    GLint uColor = glGetUniformLocation(vr.program, "uColor");
    GLint uLight = glGetUniformLocation(vr.program, "uLightPos");
    GLint uViewPos = glGetUniformLocation(vr.program, "uViewPos");
    GLint uUseLighting = glGetUniformLocation(vr.program, "uUseLighting");

    glUniformMatrix4fv(uView, 1, GL_FALSE, view.m);
    glUniformMatrix4fv(uProj, 1, GL_FALSE, proj.m);
    glUniform3f(uLight, 6.0f, 8.0f, 6.0f);
    glUniform3f(uViewPos, st.camera3d.position.x, st.camera3d.position.y, st.camera3d.position.z);

    // Grid
    if (st.show3DGrid) {
        myu::engine::Mat4 model = myu::engine::identity();
        glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
        glUniform3f(uColor, 0.25f, 0.3f, 0.35f);
        glUniform1i(uUseLighting, 0);
        glBindVertexArray(vr.vaoGrid);
        glDrawArrays(GL_LINES, 0, vr.gridVertexCount);
    }

    // Axis
    if (st.show3DAxis) {
        myu::engine::Mat4 model = myu::engine::identity();
        glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
        glUniform1i(uUseLighting, 0);
        glBindVertexArray(vr.vaoAxis);
        glUniform3f(uColor, 0.9f, 0.2f, 0.2f);
        glDrawArrays(GL_LINES, 0, 2);
        glUniform3f(uColor, 0.2f, 0.9f, 0.2f);
        glDrawArrays(GL_LINES, 2, 2);
        glUniform3f(uColor, 0.2f, 0.4f, 0.95f);
        glDrawArrays(GL_LINES, 4, 2);
    }

    // Objects
    st.scene.forEachObject([&](myu::engine::GameObject& obj) {
        if (!obj.active || !obj.visible) return;
        if (obj.tag != "3d" && obj.tag != "model" && obj.tag != "bbmodel") return;

        if (!obj.modelPath.empty()) {
            std::string modelKey = obj.modelPath;
            std::string path = obj.modelPath;
            if (st.resources) {
                auto res = st.resources->findByName(myu::engine::ResourceType::Model, obj.modelPath);
                if (auto* e = st.resources->get(res)) path = e->path;
            }
            std::filesystem::path fullPath = resolveResourcePath(st, path);
            ModelCacheEntry* entry = getModelEntry(st, modelKey);
            if (!entry || entry->sourcePath != fullPath.string() || !entry->loaded) {
                std::string err;
                loadModelToGPU(st, modelKey, fullPath.string(), err);
                entry = getModelEntry(st, modelKey);
                if (entry && !err.empty()) entry->error = err;
            }
            if (entry && entry->loaded && entry->gpu.vao && entry->gpu.vertexCount > 0) {
                myu::engine::Mat4 model = myu::engine::multiply(
                    myu::engine::translation(obj.position),
                    myu::engine::multiply(
                        myu::engine::rotationY(obj.rotation.y),
                        myu::engine::multiply(
                            myu::engine::rotationX(obj.rotation.x),
                            myu::engine::multiply(myu::engine::rotationZ(obj.rotation.z),
                                myu::engine::scale(obj.scale)))));

                glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
                if (&obj == st.selectedObject)
                    glUniform3f(uColor, 1.0f, 0.75f, 0.25f);
                else
                    glUniform3f(uColor, obj.tint.r, obj.tint.g, obj.tint.b);
                glUniform1i(uUseLighting, 1);
                glBindVertexArray(entry->gpu.vao);
                glDrawArrays(GL_TRIANGLES, 0, entry->gpu.vertexCount);
                return;
            }
        }

        myu::engine::Vec3 scale = obj.scale;
        if (scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f) {
            scale.x = obj.width;
            scale.z = obj.height;
        }

        myu::engine::Mat4 model = myu::engine::multiply(
            myu::engine::translation(obj.position),
            myu::engine::multiply(
                myu::engine::rotationY(obj.rotation.y),
                myu::engine::multiply(
                    myu::engine::rotationX(obj.rotation.x),
                    myu::engine::multiply(myu::engine::rotationZ(obj.rotation.z),
                        myu::engine::scale(scale)))));

        glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
        if (&obj == st.selectedObject)
            glUniform3f(uColor, 1.0f, 0.75f, 0.25f);
        else
            glUniform3f(uColor, obj.tint.r, obj.tint.g, obj.tint.b);
        glUniform1i(uUseLighting, 1);
        glBindVertexArray(vr.vaoCube);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    });

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
}

inline void draw3DViewport(GameEditorState& st, bool allowHeavy = true) {
    if (!ImGui::Begin("3D Viewport")) {
        ImGui::End();
        return;
    }

    if (!allowHeavy) {
        ImGui::TextDisabled("Viewport paused (Idle)");
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = std::max(64, (int)avail.x);
    int h = std::max(64, (int)avail.y);

    init3DResources(st.viewport3d, w, h);

    bool hovered = ImGui::IsWindowHovered();
    if (st.playerControlEnabled)
        updatePlayerControl(st, hovered);
    else
        updateCameraInput(st.camera3d, hovered);

    myu::engine::Mat4 view = makeViewMatrix(st.camera3d);
    myu::engine::Mat4 proj = makeProjectionMatrix(st.camera3d, (float)w / (float)h);
    render3DScene(st, view, proj);

    ImVec2 imgPos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)st.viewport3d.color, avail, ImVec2(0,1), ImVec2(1,0));
    bool imgHovered = ImGui::IsItemHovered();

    ImGuiIO& io = ImGui::GetIO();

    // Selection
    if (imgHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !st.gizmo3d.active) {
        float bestDist = 1e9f;
        myu::engine::GameObject* best = nullptr;
        st.scene.forEachObject([&](myu::engine::GameObject& obj) {
            if (!obj.active || !obj.visible) return;
            if (obj.tag != "3d" && obj.tag != "model" && obj.tag != "bbmodel") return;
            myu::engine::Vec3 sp = projectToScreen(obj.position, view, proj, imgPos, avail);
            float dx = io.MousePos.x - sp.x;
            float dy = io.MousePos.y - sp.y;
            float d = std::sqrt(dx*dx + dy*dy);
            if (d < 18.0f && d < bestDist) {
                bestDist = d;
                best = &obj;
            }
        });
        if (best) st.selectedObject = best;
    }

    // Gizmo translate (G + X/Y/Z)
    if (imgHovered && st.selectedObject && st.show3DGizmo) {
        if (!st.gizmo3d.active && ImGui::IsKeyPressed(ImGuiKey_G)) {
            st.gizmo3d.active = true;
            st.gizmo3d.axis = 0;
            st.gizmo3d.startPos = st.selectedObject->position;
            st.gizmo3d.startMouse = io.MousePos;
        }
        if (st.gizmo3d.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_X)) st.gizmo3d.axis = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) st.gizmo3d.axis = 2;
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) st.gizmo3d.axis = 3;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) st.gizmo3d.active = false;

            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float dx = io.MousePos.x - st.gizmo3d.startMouse.x;
                float dy = io.MousePos.y - st.gizmo3d.startMouse.y;
                float scale = 0.01f * st.camera3d.distance;
                myu::engine::Vec3 f = forwardFromYawPitch(st.camera3d.yaw, st.camera3d.pitch);
                myu::engine::Vec3 right = rightFromForward(f);
                myu::engine::Vec3 up = upFromForward(f);
                myu::engine::Vec3 delta = {0,0,0};
                if (st.gizmo3d.axis == 0) {
                    delta = right * (dx * scale) - up * (dy * scale);
                } else if (st.gizmo3d.axis == 1) {
                    delta = {dx * scale, 0, 0};
                } else if (st.gizmo3d.axis == 2) {
                    delta = {0, -dy * scale, 0};
                } else if (st.gizmo3d.axis == 3) {
                    delta = {0, 0, dx * scale};
                }
                st.selectedObject->position = st.gizmo3d.startPos + delta;
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                st.gizmo3d.active = false;
            }
        }
    }

    ImGui::End();
}

// ─── Main Orchestrator ─────────────────────────────────────────────────────

// ─── Code Editor Panel ──────────────────────────────────────────────────────

inline void drawCodeEditor(GameEditorState& st) {
    ImGui::Begin("Code Editor", nullptr, ImGuiWindowFlags_NoCollapse);

    // File list sidebar in a child region
    ImGui::BeginChild("##FileList", ImVec2(200, 0), true);
    ImGui::TextDisabled("Project Files");
    ImGui::Separator();
    if (ImGui::Button("Refresh")) st.scanProjectFiles();
    for (auto& f : st.projectFiles) {
        bool selected = (st.codeFilePath.find(f) != std::string::npos);
        if (ImGui::Selectable(f.c_str(), selected)) {
            if (st.codeDirty) st.saveCurrentFile();
            st.openFile(f);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Code editor area
    ImGui::BeginChild("##CodeArea", ImVec2(0, 0), true);
    if (!st.codeFilePath.empty()) {
        ImGui::TextDisabled("%s%s", st.codeFilePath.c_str(), st.codeDirty ? " *" : "");
        if (ImGui::Button("Save (Ctrl+S)")) st.saveCurrentFile();
        ImGui::SameLine();
        if (ImGui::Button("Reload")) st.openFile(
            std::filesystem::relative(st.codeFilePath, st.projectDir).string());

        ImGui::Separator();

        // Use InputTextMultiline as a basic code editor
        // Reserve the full available height
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y < 100) avail.y = 400;

        // We need a fixed-size buffer for InputText
        static char editBuf[1024 * 256] = {};
        if (!st.codeDirty) {
            // Sync from codeBuffer to editBuf when file is freshly loaded
            std::strncpy(editBuf, st.codeBuffer.c_str(), sizeof(editBuf) - 1);
            editBuf[sizeof(editBuf) - 1] = '\0';
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
        if (ImGui::InputTextMultiline("##code", editBuf, sizeof(editBuf),
                avail, flags)) {
            st.codeBuffer = editBuf;
            st.codeDirty = true;
        }
    } else {
        ImGui::TextDisabled("Select a file from the list to edit.");
    }
    ImGui::EndChild();
    ImGui::End();
}

// ─── Objects Panel (Add Board, etc.) ────────────────────────────────────────

inline void drawObjectsPanel(GameEditorState& st) {
    ImGui::Begin("Add Game Objects", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::TextDisabled("Create game objects and inject code into your project.");
    ImGui::Separator();

    // --- Board ---
    if (ImGui::CollapsingHeader("Board (Grid)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Variable Name", st.newBoardName, sizeof(st.newBoardName));
        ImGui::SliderInt("Rows", &st.newBoardRows, 1, 32);
        ImGui::SliderInt("Cols", &st.newBoardCols, 1, 32);

        ImGui::TextDisabled("Creates a Board<%s> with %dx%d grid", "int",
                            st.newBoardRows, st.newBoardCols);

        if (ImGui::Button("Add Board to Project")) {
            st.addBoardToProject(st.newBoardName, st.newBoardRows, st.newBoardCols);
            // Also add to scene graph for visual reference
        ImGui::SameLine();
        if (ImGui::Button("Apply to Project")) {
            std::string err;
            if (!exportFlowRuntimeCpp(st, st.projectDir / "src", &err))
                std::fprintf(stderr, "[Flow] Export failed: %s\n", err.c_str());
            else
                injectFlowRuntimeIntoProject(st, &err);
        }
            auto* obj = st.scene.createObject(st.newBoardName, "board");
            obj->width  = static_cast<float>(st.newBoardCols);
            obj->height = static_cast<float>(st.newBoardRows);
        }

        ImGui::Separator();
        ImGui::TextDisabled("Preview:");
        // Draw a small grid preview
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float cellSz = std::min(16.0f, 200.0f / std::max(st.newBoardRows, st.newBoardCols));
        float gridW = st.newBoardCols * cellSz;
        float gridH = st.newBoardRows * cellSz;
        for (int r = 0; r <= st.newBoardRows; ++r) {
            dl->AddLine(ImVec2(pos.x, pos.y + r * cellSz),
                        ImVec2(pos.x + gridW, pos.y + r * cellSz),
                        IM_COL32(100, 100, 100, 200));
        }
        for (int c = 0; c <= st.newBoardCols; ++c) {
            dl->AddLine(ImVec2(pos.x + c * cellSz, pos.y),
                        ImVec2(pos.x + c * cellSz, pos.y + gridH),
                        IM_COL32(100, 100, 100, 200));
        }
        ImGui::Dummy(ImVec2(gridW, gridH + 4));
    }

    ImGui::Spacing();

    // --- Future objects ---
    if (ImGui::CollapsingHeader("Player / Character")) {
        ImGui::TextDisabled("Coming soon — add player entity with health, position.");
    }
    if (ImGui::CollapsingHeader("Timer")) {
        ImGui::TextDisabled("Coming soon — add game timer to project.");
    }
    if (ImGui::CollapsingHeader("Score System")) {
        ImGui::TextDisabled("Coming soon — add score tracking.");
    }

    ImGui::End();
}

// ─── 3D Scene Panel ───────────────────────────────────────────────────────

inline void draw3DPanel(GameEditorState& st) {
    if (!ImGui::Begin("3D Scene")) {
        ImGui::End();
        return;
    }

    if (!st.projectDir.empty()) {
        std::filesystem::path scenePath = st.projectDir / "scene3d.mye";
        if (ImGui::Button("Save 3D Scene")) {
            if (save3DScene(st, scenePath))
                std::fprintf(stdout, "[3D] Saved scene: %s\n", scenePath.string().c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Load 3D Scene")) {
            if (load3DScene(st, scenePath))
                std::fprintf(stdout, "[3D] Loaded scene: %s\n", scenePath.string().c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", scenePath.string().c_str());
    }

    ImGui::TextDisabled("Camera");
    int mode = (st.camera3d.mode == Camera3DState::Mode::Orbit) ? 0 : 1;
    if (ImGui::Combo("Mode", &mode, "Orbit (Pivot)\0Fly (Freecam)\0\0")) {
        st.camera3d.mode = (mode == 0) ? Camera3DState::Mode::Orbit : Camera3DState::Mode::Fly;
    }
    ImGui::Checkbox("Axis Move", &st.camera3d.axisMoveMode);
    ImGui::SameLine();
    ImGui::Checkbox("Invert Y", &st.camera3d.invertY);
    ImGui::DragFloat("Move Speed", &st.camera3d.moveSpeed, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("FOV", &st.camera3d.fov, 0.5f, 25.0f, 90.0f);

    ImGui::Separator();
    ImGui::TextDisabled("Pivot");
    ImGui::DragFloat3("Pivot", &st.camera3d.pivot.x, 0.1f);
    if (ImGui::Button("Pivot To Selection") && st.selectedObject) {
        st.camera3d.pivot = st.selectedObject->position;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Pivot")) {
        st.camera3d.pivot = {0, 0, 0};
    }

    ImGui::Separator();
    ImGui::Checkbox("Grid", &st.show3DGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Axis", &st.show3DAxis);
    ImGui::SameLine();
    ImGui::Checkbox("Gizmo", &st.show3DGizmo);

    ImGui::Separator();
    ImGui::TextDisabled("Create 3D Object");
    ImGui::InputText("Name", st.new3DName, sizeof(st.new3DName));
    ImGui::Combo("Shape", &st.new3DShape, "Cube\0Plane\0\0");
    ImGui::DragFloat("Size", &st.new3DSize, 0.1f, 0.1f, 50.0f);
    if (ImGui::Button("Add Object")) {
        auto* obj = st.scene.createObject(st.new3DName, "3d");
        obj->position = {0, 0, 0};
        if (st.new3DShape == 0) {
            obj->scale = {st.new3DSize, st.new3DSize, st.new3DSize};
        } else {
            obj->scale = {st.new3DSize, 0.05f, st.new3DSize};
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Import .bbmodel (placeholder cube)");
    ImGui::InputText("Path", st.bbmodelPath, sizeof(st.bbmodelPath));
    if (ImGui::Button("Import bbmodel")) {
        myu::tools::BlockbenchModelInfo info;
        if (myu::tools::loadBlockbenchModelInfo(st.bbmodelPath, info)) {
            auto* obj = st.scene.createObject(info.name, "bbmodel");
            float sx = info.resolutionX > 0 ? info.resolutionX / 16.0f : 1.0f;
            float sy = info.resolutionY > 0 ? info.resolutionY / 16.0f : 1.0f;
            obj->scale = {sx, sy, sx};
            obj->tint = {0.7f, 0.8f, 0.9f, 1.0f};
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Player Control");
    ImGui::Checkbox("Enable", &st.playerControlEnabled);
    int viewMode = (st.playerView == PlayerView3D::FirstPerson) ? 0 : 1;
    if (ImGui::Combo("View", &viewMode, "First Person\0Third Person\0\0")) {
        st.playerView = (viewMode == 0) ? PlayerView3D::FirstPerson : PlayerView3D::ThirdPerson;
    }
    ImGui::DragFloat("Speed", &st.playerSpeed, 0.1f, 0.1f, 30.0f);
    ImGui::DragFloat("Sprint Mult", &st.playerSprintMul, 0.1f, 1.0f, 4.0f);
    ImGui::DragFloat("Player Height", &st.playerHeight, 0.05f, 0.5f, 3.0f);
    ImGui::DragFloat("Third Distance", &st.thirdPersonDistance, 0.1f, 1.0f, 20.0f);
    ImGui::DragFloat("Third Height", &st.thirdPersonHeight, 0.05f, 0.0f, 5.0f);
    ImGui::TextDisabled("Use selected object as player.");

    if (ImGui::TreeNode("Keybinds")) {
        drawKeybindCombo("Forward", st.keybinds.forward);
        drawKeybindCombo("Back", st.keybinds.back);
        drawKeybindCombo("Left", st.keybinds.left);
        drawKeybindCombo("Right", st.keybinds.right);
        drawKeybindCombo("Up", st.keybinds.up);
        drawKeybindCombo("Down", st.keybinds.down);
        drawKeybindCombo("Sprint", st.keybinds.sprint);
        drawKeybindCombo("Toggle View", st.keybinds.toggleView);
        ImGui::TreePop();
    }

    ImGui::End();
}

// ─── Flow Editor Panel ─────────────────────────────────────────────────────

inline void drawFlowPanel(GameEditorState& st) {
    ImGui::Begin("Game Flow", nullptr, ImGuiWindowFlags_NoCollapse);

    if (!st.projectDir.empty()) {
        std::filesystem::path flowPath = st.projectDir / "flow.mye";
        if (ImGui::Button("Save Flow")) saveFlow(st, flowPath);
        ImGui::SameLine();
        if (ImGui::Button("Load Flow")) loadFlow(st, flowPath);
        ImGui::SameLine();
        if (ImGui::Button("Export C++ Runtime")) {
            std::string err;
            exportFlowRuntimeCpp(st, st.projectDir / "src", &err);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", flowPath.string().c_str());
    }

    ImGui::Separator();
    ImGui::InputText("Flow Name", st.flowNameBuf, sizeof(st.flowNameBuf));

    ImGui::Separator();
    ImGui::Text("States");
    if (ImGui::Button("+ State")) {
        FlowState s; s.name = "State" + std::to_string(st.flowStates.size());
        s.nodeX = 40.0f + (float)(st.flowStates.size() * 140);
        s.nodeY = 40.0f;
        st.flowStates.push_back(s);
        st.selectedFlowState = (int)st.flowStates.size() - 1;
    }
    for (int i = 0; i < (int)st.flowStates.size(); ++i) {
        ImGui::PushID(i);
        bool selected = (st.selectedFlowState == i);
        if (ImGui::Selectable(st.flowStates[i].name.c_str(), selected))
            st.selectedFlowState = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            st.flowStates.erase(st.flowStates.begin() + i);
            if (st.selectedFlowState == i) st.selectedFlowState = -1;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    if (st.selectedFlowState >= 0 && st.selectedFlowState < (int)st.flowStates.size()) {
        ImGui::Separator();
        auto& s = st.flowStates[st.selectedFlowState];
        char nameBuf[64]; std::strncpy(nameBuf, s.name.c_str(), sizeof(nameBuf)-1); nameBuf[63]=0;
        char enterBuf[128]; std::strncpy(enterBuf, s.onEnter.c_str(), sizeof(enterBuf)-1); enterBuf[127]=0;
        char exitBuf[128]; std::strncpy(exitBuf, s.onExit.c_str(), sizeof(exitBuf)-1); exitBuf[127]=0;
        if (ImGui::InputText("State Name", nameBuf, sizeof(nameBuf))) s.name = nameBuf;
        if (ImGui::InputText("On Enter", enterBuf, sizeof(enterBuf))) s.onEnter = enterBuf;
        if (ImGui::InputText("On Exit", exitBuf, sizeof(exitBuf))) s.onExit = exitBuf;
    }

    ImGui::Separator();
    ImGui::Text("Transitions");
    ImGui::TextDisabled("Ops: && || == != > < >= <=  !  ( )");
    if (ImGui::Button("+ Transition")) {
        FlowTransition t; t.fromState = 0; t.toState = 0; t.eventName = "event";
        st.flowTransitions.push_back(t);
    }
    static int insertIndex = -1;
    static int insertTarget = 0; // 0=Event,1=Condition
    if (ImGui::BeginPopup("FlowInsertPopup")) {
        if (ImGui::MenuItem("key")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size()) {
                auto& tr = st.flowTransitions[insertIndex];
                std::string* target = (insertTarget == 0) ? &tr.eventName : &tr.condition;
                if (!target->empty()) target->push_back(' ');
                *target += "key";
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("keyName")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size()) {
                auto& tr = st.flowTransitions[insertIndex];
                std::string* target = (insertTarget == 0) ? &tr.eventName : &tr.condition;
                if (!target->empty()) target->push_back(' ');
                *target += "keyName";
            }
            ImGui::CloseCurrentPopup();
        }
        if (!st.flowTriggers.empty()) {
            ImGui::Separator();
            for (auto& tr : st.flowTriggers) {
                if (tr.eventName.empty()) continue;
                if (ImGui::MenuItem(tr.eventName.c_str())) {
                    if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size()) {
                        auto& t = st.flowTransitions[insertIndex];
                        std::string* target = (insertTarget == 0) ? &t.eventName : &t.condition;
                        if (!target->empty()) target->push_back(' ');
                        *target += tr.eventName;
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        if (!st.flowVars.empty()) {
            ImGui::Separator();
            for (auto& v : st.flowVars) {
                if (ImGui::MenuItem(v.name.c_str())) {
                    if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size()) {
                        auto& tr = st.flowTransitions[insertIndex];
                        std::string* target = (insertTarget == 0) ? &tr.eventName : &tr.condition;
                        if (!target->empty()) target->push_back(' ');
                        *target += v.name;
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("==")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size())
                st.flowTransitions[insertIndex].condition += " == ";
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("!=")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size())
                st.flowTransitions[insertIndex].condition += " != ";
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(">=")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size())
                st.flowTransitions[insertIndex].condition += " >= ";
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("<=")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size())
                st.flowTransitions[insertIndex].condition += " <= ";
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("&&")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size())
                st.flowTransitions[insertIndex].condition += " && ";
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("||")) {
            if (insertIndex >= 0 && insertIndex < (int)st.flowTransitions.size())
                st.flowTransitions[insertIndex].condition += " || ";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    for (int i = 0; i < (int)st.flowTransitions.size(); ++i) {
        ImGui::PushID(i + 1000);
        auto& t = st.flowTransitions[i];
        ImGui::Text("%d -> %d", t.fromState, t.toState);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) { st.flowTransitions.erase(st.flowTransitions.begin() + i); ImGui::PopID(); break; }
        ImGui::SetNextItemWidth(80);
        ImGui::DragInt("From", &t.fromState, 1, -1, (int)st.flowStates.size() - 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::DragInt("To", &t.toState, 1, -1, (int)st.flowStates.size() - 1);
        char evBuf[64]; std::strncpy(evBuf, t.eventName.c_str(), sizeof(evBuf)-1); evBuf[63]=0;
        char condBuf[128]; std::strncpy(condBuf, t.condition.c_str(), sizeof(condBuf)-1); condBuf[127]=0;
        if (ImGui::InputText("Event", evBuf, sizeof(evBuf))) t.eventName = evBuf;
        ImGui::SameLine();
        if (ImGui::SmallButton("Insert##event")) {
            insertIndex = i;
            insertTarget = 0;
            ImGui::OpenPopup("FlowInsertPopup");
        }
        if (ImGui::InputText("Condition", condBuf, sizeof(condBuf))) t.condition = condBuf;
        ImGui::SameLine();
        if (ImGui::SmallButton("Insert##cond")) {
            insertIndex = i;
            insertTarget = 1;
            ImGui::OpenPopup("FlowInsertPopup");
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Text("Sequence");
    if (ImGui::Button("+ Step")) {
        FlowSequenceStep s; s.name = "Step" + std::to_string(st.flowSteps.size());
        s.nodeX = 40.0f + (float)(st.flowSteps.size() * 140);
        s.nodeY = 80.0f;
        st.flowSteps.push_back(s);
        st.selectedFlowStep = (int)st.flowSteps.size() - 1;
    }
    for (int i = 0; i < (int)st.flowSteps.size(); ++i) {
        ImGui::PushID(i + 2000);
        bool selected = (st.selectedFlowStep == i);
        if (ImGui::Selectable(st.flowSteps[i].name.c_str(), selected))
            st.selectedFlowStep = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            st.flowSteps.erase(st.flowSteps.begin() + i);
            if (st.selectedFlowStep == i) st.selectedFlowStep = -1;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    if (st.selectedFlowStep >= 0 && st.selectedFlowStep < (int)st.flowSteps.size()) {
        ImGui::Separator();
        auto& s = st.flowSteps[st.selectedFlowStep];
        char nameBuf[64]; std::strncpy(nameBuf, s.name.c_str(), sizeof(nameBuf)-1); nameBuf[63]=0;
        char onStartBuf[128]; std::strncpy(onStartBuf, s.onStart.c_str(), sizeof(onStartBuf)-1); onStartBuf[127]=0;
        char onUpdateBuf[128]; std::strncpy(onUpdateBuf, s.onUpdate.c_str(), sizeof(onUpdateBuf)-1); onUpdateBuf[127]=0;
        char onEndBuf[128]; std::strncpy(onEndBuf, s.onEnd.c_str(), sizeof(onEndBuf)-1); onEndBuf[127]=0;
        if (ImGui::InputText("Step Name", nameBuf, sizeof(nameBuf))) s.name = nameBuf;
        ImGui::DragFloat("Duration", &s.duration, 0.1f, 0.0f, 999.0f);
        if (ImGui::InputText("On Start", onStartBuf, sizeof(onStartBuf))) s.onStart = onStartBuf;
        if (ImGui::InputText("On Update", onUpdateBuf, sizeof(onUpdateBuf))) s.onUpdate = onUpdateBuf;
        if (ImGui::InputText("On End", onEndBuf, sizeof(onEndBuf))) s.onEnd = onEndBuf;
    }

    ImGui::Separator();
    ImGui::Text("Event Triggers");
    if (ImGui::Button("+ Trigger")) {
        FlowEventTrigger t; t.eventName = "event"; t.keyName = "Space";
        st.flowTriggers.push_back(t);
    }
    for (int i = 0; i < (int)st.flowTriggers.size(); ++i) {
        ImGui::PushID(i + 3000);
        char enBuf[64]; std::strncpy(enBuf, st.flowTriggers[i].eventName.c_str(), sizeof(enBuf)-1); enBuf[63]=0;
        char keyBuf[64]; std::strncpy(keyBuf, st.flowTriggers[i].keyName.c_str(), sizeof(keyBuf)-1); keyBuf[63]=0;
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("Event", enBuf, sizeof(enBuf))) st.flowTriggers[i].eventName = enBuf;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("Key", keyBuf, sizeof(keyBuf))) st.flowTriggers[i].keyName = keyBuf;
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) { st.flowTriggers.erase(st.flowTriggers.begin() + i); ImGui::PopID(); break; }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Text("Variables");
    if (ImGui::Button("+ Var")) {
        FlowVar v; v.name = "var" + std::to_string(st.flowVars.size());
        st.flowVars.push_back(v);
    }
    for (int i = 0; i < (int)st.flowVars.size(); ++i) {
        ImGui::PushID(i + 4000);
        auto& v = st.flowVars[i];
        char nameBuf[64]; std::strncpy(nameBuf, v.name.c_str(), sizeof(nameBuf)-1); nameBuf[63]=0;
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) v.name = nameBuf;
        ImGui::SameLine();
        const char* typeOpts[] = {"Number", "Bool", "String"};
        int t = static_cast<int>(v.type);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("Type", &t, typeOpts, 3)) v.type = static_cast<FlowVar::Type>(t);
        ImGui::SameLine();
        if (v.type == FlowVar::Type::Number) {
            ImGui::SetNextItemWidth(120);
            ImGui::DragFloat("Value", &v.numValue, 0.1f);
        } else if (v.type == FlowVar::Type::Bool) {
            ImGui::Checkbox("Value", &v.boolValue);
        } else {
            char valBuf[128]; std::strncpy(valBuf, v.strValue.c_str(), sizeof(valBuf)-1); valBuf[127]=0;
            ImGui::SetNextItemWidth(160);
            if (ImGui::InputText("Value", valBuf, sizeof(valBuf))) v.strValue = valBuf;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) { st.flowVars.erase(st.flowVars.begin() + i); ImGui::PopID(); break; }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Checkbox("Show Graph", &st.showFlowGraph);
    static int graphMode = 0; // 0=States,1=Sequence
    ImGui::SameLine();
    ImGui::Combo("Mode", &graphMode, "States\0Sequence\0\0");
    ImGui::SameLine();
    ImGui::TextDisabled("(Shift-click to link)");

    if (st.showFlowGraph) {
        ImGui::BeginChild("FlowGraph", ImVec2(0, 260), true);
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.x < 50) canvasSize.x = 50;
        if (canvasSize.y < 50) canvasSize.y = 50;
        ImGui::InvisibleButton("##flowcanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                          IM_COL32(20, 24, 32, 255));

        static int dragIndex = -1;
        static bool dragIsStep = false;
        static int linkFrom = -1;
        static bool linkIsStep = false;
        static ImVec2 dragOffset = {0, 0};

        auto drawNode = [&](const std::string& name, float& x, float& y, int idx, bool isStep) {
            ImVec2 pos = ImVec2(canvasPos.x + x, canvasPos.y + y);
            ImVec2 size = ImVec2(120, 44);
            ImU32 col = isStep ? IM_COL32(60, 120, 200, 220) : IM_COL32(120, 80, 200, 220);
            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col, 6.0f);
            dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 40), 6.0f);
            dl->AddText(ImVec2(pos.x + 8, pos.y + 12), IM_COL32(255, 255, 255, 220), name.c_str());

            ImVec2 mp = ImGui::GetIO().MousePos;
            bool hovered = (mp.x >= pos.x && mp.x <= pos.x + size.x && mp.y >= pos.y && mp.y <= pos.y + size.y);
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (ImGui::GetIO().KeyShift) {
                    if (linkFrom < 0 || linkIsStep != isStep) {
                        linkFrom = idx;
                        linkIsStep = isStep;
                    } else {
                        if (!isStep) {
                            FlowTransition t; t.fromState = linkFrom; t.toState = idx;
                            std::string fromName = (linkFrom >= 0 && linkFrom < (int)st.flowStates.size())
                                ? st.flowStates[linkFrom].name : "State";
                            std::string toName = (idx >= 0 && idx < (int)st.flowStates.size())
                                ? st.flowStates[idx].name : "State";
                            std::string ev = "On" + sanitizeIdentifier(fromName) + "To" + sanitizeIdentifier(toName);
                            t.eventName = ev.empty() ? "event" : ev;
                            st.flowTransitions.push_back(t);
                        } else {
                            int from = linkFrom;
                            int to = idx;
                            if (from != to && from >= 0 && to >= 0 &&
                                from < (int)st.flowSteps.size() && to < (int)st.flowSteps.size()) {
                                auto step = st.flowSteps[to];
                                st.flowSteps.erase(st.flowSteps.begin() + to);
                                int insertAt = (from < to) ? (from + 1) : from;
                                if (insertAt < 0) insertAt = 0;
                                if (insertAt > (int)st.flowSteps.size()) insertAt = (int)st.flowSteps.size();
                                st.flowSteps.insert(st.flowSteps.begin() + insertAt, step);
                            }
                        }
                        linkFrom = -1;
                        linkIsStep = false;
                    }
                } else {
                    dragIndex = idx;
                    dragIsStep = isStep;
                    dragOffset = ImVec2(mp.x - pos.x, mp.y - pos.y);
                }
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && dragIndex == idx && dragIsStep == isStep) {
                x = mp.x - canvasPos.x - dragOffset.x;
                y = mp.y - canvasPos.y - dragOffset.y;
            }
        };

        if (graphMode == 0) {
            // Draw transitions
            for (auto& t : st.flowTransitions) {
                if (t.fromState < 0 || t.toState < 0) continue;
                if (t.fromState >= (int)st.flowStates.size() || t.toState >= (int)st.flowStates.size()) continue;
                auto& a = st.flowStates[t.fromState];
                auto& b = st.flowStates[t.toState];
                ImVec2 p0(canvasPos.x + a.nodeX + 120, canvasPos.y + a.nodeY + 22);
                ImVec2 p1(canvasPos.x + b.nodeX, canvasPos.y + b.nodeY + 22);
                dl->AddBezierCubic(p0, ImVec2(p0.x + 40, p0.y), ImVec2(p1.x - 40, p1.y), p1,
                                   IM_COL32(200, 200, 220, 120), 2.0f);
            }
            for (int i = 0; i < (int)st.flowStates.size(); ++i) {
                drawNode(st.flowStates[i].name, st.flowStates[i].nodeX, st.flowStates[i].nodeY, i, false);
            }
        } else {
            for (int i = 0; i < (int)st.flowSteps.size(); ++i) {
                drawNode(st.flowSteps[i].name, st.flowSteps[i].nodeX, st.flowSteps[i].nodeY, i, true);
                if (i + 1 < (int)st.flowSteps.size()) {
                    ImVec2 p0(canvasPos.x + st.flowSteps[i].nodeX + 120, canvasPos.y + st.flowSteps[i].nodeY + 22);
                    ImVec2 p1(canvasPos.x + st.flowSteps[i + 1].nodeX, canvasPos.y + st.flowSteps[i + 1].nodeY + 22);
                    dl->AddBezierCubic(p0, ImVec2(p0.x + 30, p0.y), ImVec2(p1.x - 30, p1.y), p1,
                                       IM_COL32(80, 170, 240, 140), 2.0f);
                }
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            dragIndex = -1;
            dragIsStep = false;
        }

        ImGui::EndChild();
    }

    ImGui::End();
}

inline void drawGameEditor(GameEditorState& st, bool allowHeavy = true) {
    // Tab bar at the top of the workspace
    ImGui::Begin("Game Editor", nullptr, ImGuiWindowFlags_NoCollapse);
    if (ImGui::BeginTabBar("##gedtabs")) {
        if (ImGui::BeginTabItem("Scene"))   { st.currentTab = GameEditorState::Tab::Scene;   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Code"))    { st.currentTab = GameEditorState::Tab::Code;    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Objects")) { st.currentTab = GameEditorState::Tab::Objects; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Systems")) { st.currentTab = GameEditorState::Tab::Systems; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Flow"))    { st.currentTab = GameEditorState::Tab::Flow;    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Preview")) { st.currentTab = GameEditorState::Tab::Preview; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("3D"))       { st.currentTab = GameEditorState::Tab::ThreeD; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();

    // Draw the relevant panels based on current tab
    switch (st.currentTab) {
    case GameEditorState::Tab::Scene:
        drawScenePanel(st);
        drawInspector(st);
        if (st.perf.showViewportInSceneTab)
            drawGameViewport(st, allowHeavy);
        break;
    case GameEditorState::Tab::Code:
        drawCodeEditor(st);
        break;
    case GameEditorState::Tab::Objects:
        drawObjectsPanel(st);
        drawScenePanel(st);
        drawInspector(st);
        break;
    case GameEditorState::Tab::Systems:
        drawGameSystems(st);
        break;
    case GameEditorState::Tab::Flow:
        drawFlowPanel(st);
        break;
    case GameEditorState::Tab::Preview:
        if (st.perf.showViewportInPreviewTab)
            drawGameViewport(st, allowHeavy);
        break;
    case GameEditorState::Tab::ThreeD: {
        draw3DPanel(st);
        drawScenePanel(st);
        drawInspector(st);
        draw3DViewport(st, allowHeavy);
        break;
    }
    }
}

} // namespace myu::editor
