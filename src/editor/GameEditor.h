#pragma once
// =============================================================================
// GameEditor.h – All-in-one ImGui editor panels for game development
//   Scene hierarchy, Inspector, Board editor, Card editor,
//   Game systems config, 2.5D viewport
// =============================================================================

#include "../engine/Core.h"
#include "../engine/Camera2D5.h"
#include "../game/BoardGame.h"
#include "../game/CardGame.h"
#include "../game/GameSystems.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

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

// ─── Editor State ───────────────────────────────────────────────────────────

struct GameEditorState {
    // Engine
    myu::engine::Scene      scene;
    myu::engine::Camera2D5  camera;
    myu::engine::GameObject* selectedObject = nullptr;

    // Game systems
    myu::game::Board        board;
    myu::game::CardLibrary  cardLibrary;
    myu::game::PlayerCards  player1Cards, player2Cards;
    myu::game::TurnManager  turnManager;
    myu::game::GameTimer    timer;
    myu::game::ScoreSystem  score;
    myu::game::Shop         shop;
    myu::game::AudioConfig  audio;

    // Editor tabs
    enum class Tab { Scene, Board, Cards, Systems, Preview, ThreeD };
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

    GameEditorState() {
        board.init(8, 8);
        boardInited = true;
    }

    void initDefaultScene() {
        scene = myu::engine::Scene();
        auto* bg = scene.createObject("Background", "background");
        bg->position = {0, 0, -1};
        bg->width = 20; bg->height = 20;
        bg->tint = {0.08f, 0.12f, 0.22f, 1.0f};

        auto* boardObj = scene.createObject("Board", "board");
        boardObj->position = {0, 0, 0};

        auto* piecesGrp = scene.createObject("Pieces", "group");
        auto* cardsGrp  = scene.createObject("Cards",  "group");

        auto* uiLayer = scene.createObject("UI Layer", "ui");
        auto* handDisp = scene.createObject("Hand Display", "ui", uiLayer);
        handDisp->position = {0, -4, 0};
        auto* deckDisp = scene.createObject("Deck", "ui", uiLayer);
        deckDisp->position = {5, -4, 0};
        auto* timerObj = scene.createObject("Timer", "ui", uiLayer);
        timerObj->position = {0, 4.5f, 0};
        auto* scoreObj = scene.createObject("Score", "ui", uiLayer);
        scoreObj->position = {-5, 4.5f, 0};

        (void)piecesGrp; (void)cardsGrp;
    }
};

// ─── Helpers ────────────────────────────────────────────────────────────────

inline ImU32 colF(float r, float g, float b, float a = 1.0f) {
    return IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),(int)(a*255));
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

// ─── Main Orchestrator ─────────────────────────────────────────────────────

inline void drawGameEditor(GameEditorState& st, bool allowHeavy = true) {
    // Tab bar at the top of the workspace
    ImGui::Begin("Game Editor", nullptr, ImGuiWindowFlags_NoCollapse);
    if (ImGui::BeginTabBar("##gedtabs")) {
        if (ImGui::BeginTabItem("Scene"))   { st.currentTab = GameEditorState::Tab::Scene;   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Board"))   { st.currentTab = GameEditorState::Tab::Board;   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Cards"))   { st.currentTab = GameEditorState::Tab::Cards;   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Systems")) { st.currentTab = GameEditorState::Tab::Systems; ImGui::EndTabItem(); }
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
    case GameEditorState::Tab::Board:
        drawBoardEditor(st, allowHeavy);
        if (st.perf.showViewportInBoardTab)
            drawGameViewport(st, allowHeavy);
        break;
    case GameEditorState::Tab::Cards:
        drawCardEditor(st, allowHeavy);
        break;
    case GameEditorState::Tab::Systems:
        drawGameSystems(st);
        break;
    case GameEditorState::Tab::Preview:
        if (st.perf.showViewportInPreviewTab)
            drawGameViewport(st, allowHeavy);
        break;
    case GameEditorState::Tab::ThreeD: {
        ImGui::Begin("3D Scene");
        ImGui::Text("Create 3D Object");
        static char objName[64] = "New3D";
        static int shape = 0; // 0=Cube,1=Plane
        ImGui::InputText("Name", objName, sizeof(objName));
        ImGui::Combo("Shape", &shape, "Cube\0Plane\0\0");
        if (ImGui::Button("Add Object")) {
            // For now create in the Scene graph (3D -> z!=0). ECS integration next step.
            auto* obj = st.scene.createObject(objName, "3d");
            obj->position = {0, 0, (shape == 0 ? 1.0f : 0.0f)};
            obj->width = 1.0f;
            obj->height = 1.0f;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Resource/Model assignment will appear here.");
        ImGui::End();
        if (st.perf.showViewportInPreviewTab)
            drawGameViewport(st, allowHeavy);
        break;
    }
    }
}

} // namespace myu::editor
