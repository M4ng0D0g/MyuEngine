#pragma once
// =============================================================================
// UIDesigner.h – Full 2D UI designer: toolbar, canvas, properties, HTML mode
// =============================================================================

#include "UIElement.h"
#include "UIHtmlCss.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace myu::ui {

// ─── Designer state ─────────────────────────────────────────────────────────

enum class DesignerMode { Visual, HtmlCss };
enum class DragOp       { None, Move, ResizeBR, Reparent };

struct DesignerPerf {
    bool drawCanvas    = true;
    bool drawGrid      = true;
    bool drawElements  = true;
    bool drawSelection = true;
    bool drawOverlay   = true;
};

struct DesignerState {
    // Root element (virtual canvas root)
    UIElement root;

    // Canvas
    float canvasW = 800, canvasH = 600;
    float zoom    = 1.0f;
    Vec2  pan     = {0, 0};
    bool  showGrid = true;
    int   gridSize = 20;
    bool  snapToGrid = true;

    // Selection
    UIElement* selected   = nullptr;
    UIElement* hovered    = nullptr;
    UIElement* dragTarget = nullptr;
    DragOp     dragOp     = DragOp::None;
    Vec2       dragStart  = {};
    Vec2       dragElemStart = {};    // offsetMin at drag start
    Vec2       dragElemMaxStart = {}; // offsetMax at drag start

    // Mode
    DesignerMode mode = DesignerMode::Visual;
    char htmlBuffer[32768] = {};
    bool htmlDirty = false;

    // Temp buffers for properties editing
    char nameBuf[64]    = {};
    char textBuf[256]   = {};
    char cssBuf[128]    = {};
    char imgBuf[256]    = {};

    // Project file path
    std::filesystem::path savePath;

    // Performance
    DesignerPerf perf;

    DesignerState() {
        root.id   = 0;
        root.name = "Canvas";
        root.type = ElementType::Container;
        root.anchor.offsetMin = {0, 0};
        root.anchor.offsetMax = {canvasW, canvasH};
    }

    void selectElement(UIElement* e) {
        selected = e;
        if (e) {
            std::strncpy(nameBuf, e->name.c_str(), sizeof(nameBuf) - 1);
            std::strncpy(textBuf, e->text.c_str(), sizeof(textBuf) - 1);
            std::strncpy(cssBuf,  e->cssClass.c_str(), sizeof(cssBuf) - 1);
            std::strncpy(imgBuf,  e->imagePath.c_str(), sizeof(imgBuf) - 1);
        }
    }
};

// ─── Canvas rendering helpers ───────────────────────────────────────────────

inline ImVec2 canvasToScreen(const DesignerState& ds, const ImVec2& origin, float x, float y) {
    return ImVec2(origin.x + (x + ds.pan.x) * ds.zoom,
                  origin.y + (y + ds.pan.y) * ds.zoom);
}

inline ImVec2 screenToCanvas(const DesignerState& ds, const ImVec2& origin, float sx, float sy) {
    return ImVec2((sx - origin.x) / ds.zoom - ds.pan.x,
                  (sy - origin.y) / ds.zoom - ds.pan.y);
}

inline ImU32 vec4ToImCol(const Vec4& c) {
    return IM_COL32(
        static_cast<int>(c.x * 255),
        static_cast<int>(c.y * 255),
        static_cast<int>(c.z * 255),
        static_cast<int>(c.w * 255));
}

inline void drawElementOnCanvas(ImDrawList* dl, const DesignerState& ds,
                                 const ImVec2& origin, const UIElement& e,
                                 const UIElement* selected,
                                 bool drawSelection)
{
    if (!e.visible) return;

    auto& r = e.computedRect;
    ImVec2 p0 = canvasToScreen(ds, origin, r.x, r.y);
    ImVec2 p1 = canvasToScreen(ds, origin, r.x + r.w, r.y + r.h);

    float rounding = e.style.borderRadius * ds.zoom;

    // Background
    dl->AddRectFilled(p0, p1, vec4ToImCol(e.style.bgColor), rounding);

    // Border
    if (e.style.borderWidth > 0) {
        dl->AddRect(p0, p1, vec4ToImCol(e.style.borderColor),
                    rounding, 0, e.style.borderWidth * ds.zoom);
    }

    // Text
    if (!e.text.empty()) {
        float fs = e.style.fontSize * ds.zoom;
        if (fs >= 6.0f) {
            ImVec2 textPos(p0.x + e.style.padding[3] * ds.zoom,
                           p0.y + e.style.padding[0] * ds.zoom);
            dl->AddText(nullptr, fs, textPos, vec4ToImCol(e.style.fgColor), e.text.c_str());
        }
    }

    // Checkbox
    if (e.type == ElementType::Checkbox) {
        float box = 14.0f * ds.zoom;
        ImVec2 cb0(p0.x + 4 * ds.zoom, p0.y + 4 * ds.zoom);
        ImVec2 cb1(cb0.x + box, cb0.y + box);
        dl->AddRect(cb0, cb1, IM_COL32(200, 200, 210, 220), 2.0f, 0, 1.0f);
    }

    // Toggle
    if (e.type == ElementType::Toggle) {
        float r = (p1.y - p0.y) * 0.4f;
        ImVec2 c(p0.x + r + 6 * ds.zoom, (p0.y + p1.y) * 0.5f);
        dl->AddCircleFilled(c, r, IM_COL32(255, 255, 255, 200));
    }

    // Slider
    if (e.type == ElementType::Slider) {
        float cy = (p0.y + p1.y) * 0.5f;
        dl->AddLine(ImVec2(p0.x + 6 * ds.zoom, cy), ImVec2(p1.x - 6 * ds.zoom, cy),
                    IM_COL32(180, 180, 190, 200), 2.0f * ds.zoom);
        dl->AddCircleFilled(ImVec2(p0.x + (p1.x - p0.x) * 0.6f, cy),
                            5.0f * ds.zoom, IM_COL32(230, 230, 240, 230));
    }

    // Progress
    if (e.type == ElementType::Progress) {
        float w = (p1.x - p0.x) * 0.6f;
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p1.y), IM_COL32(80, 170, 255, 200));
    }

    // Dropdown
    if (e.type == ElementType::Dropdown) {
        ImVec2 a(p1.x - 14 * ds.zoom, (p0.y + p1.y) * 0.5f - 3 * ds.zoom);
        ImVec2 b(p1.x - 6 * ds.zoom, (p0.y + p1.y) * 0.5f - 3 * ds.zoom);
        ImVec2 c(p1.x - 10 * ds.zoom, (p0.y + p1.y) * 0.5f + 3 * ds.zoom);
        dl->AddTriangleFilled(a, b, c, IM_COL32(210, 210, 220, 220));
    }

    // Image placeholder
    if (e.type == ElementType::Image && e.imagePath.empty()) {
        float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
        dl->AddText(ImVec2(cx - 10, cy - 6), IM_COL32(180,180,180,200), "[IMG]");
    }

    // Type label for Input
    if (e.type == ElementType::Input) {
        std::string ph = e.text.empty() ? "Input..." : e.text;
        float fs = e.style.fontSize * ds.zoom * 0.9f;
        if (fs >= 5) {
            dl->AddText(nullptr, fs,
                ImVec2(p0.x + 4*ds.zoom, p0.y + 4*ds.zoom),
                IM_COL32(150,150,150,180), ph.c_str());
        }
    }

    // Selection highlight
    if (drawSelection && &e == selected) {
        dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1),
                    IM_COL32(80, 180, 255, 220), rounding, 0, 2.0f);
        // Resize handle (bottom-right)
        float hs = 6 * ds.zoom;
        ImVec2 hP0(p1.x - hs, p1.y - hs);
        dl->AddRectFilled(hP0, p1, IM_COL32(80, 180, 255, 255));
    }

    // Children
    for (auto& child : e.children)
        drawElementOnCanvas(dl, ds, origin, *child, selected, drawSelection);
}

// ─── Toolbar (Left Panel) ──────────────────────────────────────────────────

inline void drawToolbar(DesignerState& ds) {
    if (!ImGui::Begin("Toolbox", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Elements");
    ImGui::Separator();

    struct ToolEntry { ElementType type; const char* label; };
    static const ToolEntry tools[] = {
        {ElementType::Panel,     "Panel"},
        {ElementType::Button,    "Button"},
        {ElementType::Label,     "Label"},
        {ElementType::Image,     "Image"},
        {ElementType::Input,     "Input"},
        {ElementType::Checkbox,  "Checkbox"},
        {ElementType::Toggle,    "Toggle"},
        {ElementType::Slider,    "Slider"},
        {ElementType::Progress,  "Progress"},
        {ElementType::Dropdown,  "Dropdown"},
        {ElementType::Container, "Container"},
    };

    for (auto& t : tools) {
        if (ImGui::Button(t.label, ImVec2(-1, 30))) {
            // Add element to selected parent or root
            UIElement* parent = ds.selected ? ds.selected : &ds.root;
            // If selected is a leaf type, add to its parent instead
            if (parent->type == ElementType::Label ||
                parent->type == ElementType::Input ||
                parent->type == ElementType::Image ||
                parent->type == ElementType::Checkbox ||
                parent->type == ElementType::Toggle ||
                parent->type == ElementType::Slider ||
                parent->type == ElementType::Progress ||
                parent->type == ElementType::Dropdown) {
                parent = parent->parent ? parent->parent : &ds.root;
            }

            auto elem = createElement(t.type);
            // Position relative to parent
            float offsetX = static_cast<float>((parent->children.size() % 5) * 20);
            float offsetY = static_cast<float>((parent->children.size() / 5) * 20);
            elem->anchor.offsetMin.x += offsetX;
            elem->anchor.offsetMin.y += offsetY;
            elem->anchor.offsetMax.x += offsetX;
            elem->anchor.offsetMax.y += offsetY;

            UIElement* raw = elem.get();
            parent->addChild(std::move(elem));
            ds.selectElement(raw);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add %s element", t.label);
    }

    ImGui::Separator();
    ImGui::Text("Hierarchy");
    ImGui::Separator();

    // Recursive hierarchy tree
    struct Local {
        static void drawTree(DesignerState& ds, UIElement& e) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (e.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            if (&e == ds.selected) flags |= ImGuiTreeNodeFlags_Selected;

            bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e.id)),
                                          flags, "%s [%s]", e.name.c_str(), elementTypeName(e.type));

            // Click to select
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                ds.selectElement(&e);

            // Drag source for reparenting
            if (e.parent && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                uint32_t eid = e.id;
                ImGui::SetDragDropPayload("UIE_REPARENT", &eid, sizeof(eid));
                ImGui::Text("Move: %s", e.name.c_str());
                ImGui::EndDragDropSource();
            }

            // Drop target for reparenting
            if (ImGui::BeginDragDropTarget()) {
                if (auto* payload = ImGui::AcceptDragDropPayload("UIE_REPARENT")) {
                    uint32_t srcId = *static_cast<const uint32_t*>(payload->Data);
                    UIElement* src = ds.root.findById(srcId);
                    if (src && src != &e && src->parent) {
                        // Prevent making an element its own descendant
                        bool isDescendant = false;
                        for (UIElement* p = &e; p; p = p->parent)
                            if (p == src) { isDescendant = true; break; }
                        if (!isDescendant) {
                            auto owned = src->detach();
                            if (owned) {
                                e.addChild(std::move(owned));
                                ds.selectElement(src);
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (open) {
                for (auto& c : e.children)
                    drawTree(ds, *c);
                ImGui::TreePop();
            }
        }
    };

    Local::drawTree(ds, ds.root);

    // Delete selected
    if (ds.selected && ds.selected != &ds.root) {
        ImGui::Separator();
        if (ImGui::Button("Delete Selected", ImVec2(-1, 0))) {
            auto owned = ds.selected->detach();
            ds.selected = nullptr;
            // owned goes out of scope → deleted
        }
    }

    ImGui::End();
}

// ─── Canvas (Center Panel) ─────────────────────────────────────────────────

inline void drawCanvas(DesignerState& ds, bool allowHeavy = true) {
    ImGuiWindowFlags cflags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Canvas", nullptr, cflags)) {
        ImGui::End();
        return;
    }

    if (!allowHeavy || !ds.perf.drawCanvas) {
        ImGui::TextDisabled("Canvas paused (Idle/Settings)");
        ImGui::End();
        return;
    }

    // Controls row
    ImGui::Checkbox("Grid", &ds.showGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &ds.snapToGrid);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::DragInt("##grid", &ds.gridSize, 1, 5, 100);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderFloat("Zoom", &ds.zoom, 0.25f, 4.0f, "%.1fx");
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) { ds.zoom = 1.0f; ds.pan = {0,0}; }

    // Canvas area
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50) canvasSize.x = 50;
    if (canvasSize.y < 50) canvasSize.y = 50;

    ImGui::InvisibleButton("##canvas_area", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool canvasHovered = ImGui::IsItemHovered();
    bool canvasActive  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(30, 30, 35, 255));

    // Canvas rectangle
    ImVec2 cP0 = canvasToScreen(ds, canvasPos, 0, 0);
    ImVec2 cP1 = canvasToScreen(ds, canvasPos, ds.canvasW, ds.canvasH);
    dl->AddRectFilled(cP0, cP1, IM_COL32(22, 33, 62, 255));
    dl->AddRect(cP0, cP1, IM_COL32(60, 80, 120, 255));

    // Grid
    if (ds.showGrid && ds.perf.drawGrid && ds.zoom >= 0.3f) {
        float gs = ds.gridSize * ds.zoom;
        ImU32 gc = IM_COL32(255, 255, 255, static_cast<int>(20 * std::min(ds.zoom, 1.0f)));
        for (float x = 0; x <= ds.canvasW; x += ds.gridSize) {
            ImVec2 a = canvasToScreen(ds, canvasPos, x, 0);
            ImVec2 b = canvasToScreen(ds, canvasPos, x, ds.canvasH);
            dl->AddLine(a, b, gc);
        }
        for (float y = 0; y <= ds.canvasH; y += ds.gridSize) {
            ImVec2 a = canvasToScreen(ds, canvasPos, 0, y);
            ImVec2 b = canvasToScreen(ds, canvasPos, ds.canvasW, y);
            dl->AddLine(a, b, gc);
        }
    }

    // Layout elements
    Rect rootRect = {0, 0, ds.canvasW, ds.canvasH};
    ds.root.layout(rootRect);

    // Draw elements
    if (ds.perf.drawElements)
        drawElementOnCanvas(dl, ds, canvasPos, ds.root, ds.selected, ds.perf.drawSelection);

    // ── Mouse interaction ──
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    ImVec2 canvasMouse = screenToCanvas(ds, canvasPos, mousePos.x, mousePos.y);

    if (canvasHovered) {
        // Zoom with scroll
        if (io.MouseWheel != 0) {
            ImVec2 before = screenToCanvas(ds, canvasPos, mousePos.x, mousePos.y);
            ds.zoom *= (io.MouseWheel > 0) ? 1.1f : 0.9f;
            ds.zoom = std::clamp(ds.zoom, 0.1f, 8.0f);
            ImVec2 after = screenToCanvas(ds, canvasPos, mousePos.x, mousePos.y);
            ds.pan.x += (before.x - after.x);
            ds.pan.y += (before.y - after.y);
        }

        // Pan with right mouse
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ds.pan.x += io.MouseDelta.x / ds.zoom;
            ds.pan.y += io.MouseDelta.y / ds.zoom;
        }

        // Hit test
        ds.hovered = ds.root.hitTest(canvasMouse.x, canvasMouse.y);
        if (ds.hovered == &ds.root) ds.hovered = nullptr;

        // Click to select
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (ds.hovered) {
                ds.selectElement(ds.hovered);

                // Check resize handle
                if (ds.selected) {
                    auto& sr = ds.selected->computedRect;
                    float hx = sr.x + sr.w, hy = sr.y + sr.h;
                    float handleR = 8.0f / ds.zoom;
                    if (canvasMouse.x >= hx - handleR && canvasMouse.y >= hy - handleR) {
                        ds.dragOp = DragOp::ResizeBR;
                    } else {
                        ds.dragOp = DragOp::Move;
                    }
                    ds.dragTarget = ds.selected;
                    ds.dragStart = {canvasMouse.x, canvasMouse.y};
                    ds.dragElemStart = {ds.selected->anchor.offsetMin.x,
                                        ds.selected->anchor.offsetMin.y};
                    ds.dragElemMaxStart = {ds.selected->anchor.offsetMax.x,
                                           ds.selected->anchor.offsetMax.y};
                }
            } else {
                ds.selectElement(nullptr);
            }
        }

        // Dragging
        if (ds.dragTarget && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float dx = canvasMouse.x - ds.dragStart.x;
            float dy = canvasMouse.y - ds.dragStart.y;

            if (ds.snapToGrid && ds.gridSize > 0) {
                dx = std::round(dx / ds.gridSize) * ds.gridSize;
                dy = std::round(dy / ds.gridSize) * ds.gridSize;
            }

            if (ds.dragOp == DragOp::Move) {
                float w = ds.dragElemMaxStart.x - ds.dragElemStart.x;
                float h = ds.dragElemMaxStart.y - ds.dragElemStart.y;
                ds.dragTarget->anchor.offsetMin.x = ds.dragElemStart.x + dx;
                ds.dragTarget->anchor.offsetMin.y = ds.dragElemStart.y + dy;
                ds.dragTarget->anchor.offsetMax.x = ds.dragTarget->anchor.offsetMin.x + w;
                ds.dragTarget->anchor.offsetMax.y = ds.dragTarget->anchor.offsetMin.y + h;
            } else if (ds.dragOp == DragOp::ResizeBR) {
                ds.dragTarget->anchor.offsetMax.x = ds.dragElemMaxStart.x + dx;
                ds.dragTarget->anchor.offsetMax.y = ds.dragElemMaxStart.y + dy;
                // Ensure min size
                if (ds.dragTarget->anchor.offsetMax.x - ds.dragTarget->anchor.offsetMin.x < 10)
                    ds.dragTarget->anchor.offsetMax.x = ds.dragTarget->anchor.offsetMin.x + 10;
                if (ds.dragTarget->anchor.offsetMax.y - ds.dragTarget->anchor.offsetMin.y < 10)
                    ds.dragTarget->anchor.offsetMax.y = ds.dragTarget->anchor.offsetMin.y + 10;
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ds.dragTarget = nullptr;
            ds.dragOp = DragOp::None;
        }

        // Hover cursor
        if (ds.hovered) {
            auto& hr = ds.hovered->computedRect;
            float hx = hr.x + hr.w, hy = hr.y + hr.h;
            float handleR = 8.0f / ds.zoom;
            if (canvasMouse.x >= hx - handleR && canvasMouse.y >= hy - handleR)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
            else
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }

    if (ds.selected && ds.selected != &ds.root && !ds.selected->locked) {
        bool canvasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (canvasFocus && ImGui::IsKeyPressed(ImGuiKey_Delete) && !io.WantTextInput) {
            auto owned = ds.selected->detach();
            ds.selectElement(nullptr);
        }
    }

    // Info overlay
    if (ds.selected && ds.perf.drawOverlay) {
        auto& sr = ds.selected->computedRect;
        char info[128];
        std::snprintf(info, sizeof(info), "%s  %.0fx%.0f  @(%.0f,%.0f)",
            ds.selected->name.c_str(), sr.w, sr.h, sr.x, sr.y);
        ImVec2 ip = canvasToScreen(ds, canvasPos, sr.x, sr.y - 16 / ds.zoom);
        dl->AddText(ip, IM_COL32(200, 220, 255, 220), info);
    }

    ImGui::End();
}

// ─── Properties Panel (Right) ──────────────────────────────────────────────

inline void drawAnchorPresetGrid(DesignerState& ds) {
    if (!ds.selected) return;

    static const char* presetLabels[] = {
        "TL","TC","TR","T-",
        "ML","MC","MR","M-",
        "BL","BC","BR","B-",
        "-L","-C","-R","--"
    };

    ImGui::Text("Anchor Presets:");
    for (int i = 0; i < 16; ++i) {
        if (i % 4 != 0) ImGui::SameLine();
        ImGui::PushID(i);
        if (ImGui::Button(presetLabels[i], ImVec2(32, 24))) {
            float w = ds.selected->anchor.offsetMax.x - ds.selected->anchor.offsetMin.x;
            float h = ds.selected->anchor.offsetMax.y - ds.selected->anchor.offsetMin.y;
            applyAnchorPreset(ds.selected->anchor, static_cast<AnchorPreset>(i), w, h);
        }
        ImGui::PopID();
    }
}

inline void drawProperties(DesignerState& ds) {
    if (!ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (!ds.selected) {
        ImGui::TextDisabled("No element selected");

        // Canvas settings
        ImGui::Separator();
        ImGui::Text("Canvas Settings");
        ImGui::DragFloat("Width", &ds.canvasW, 1, 100, 4096);
        ImGui::DragFloat("Height", &ds.canvasH, 1, 100, 4096);
        ImGui::End();
        return;
    }

    UIElement& e = *ds.selected;

    // --- Identity ---
    if (ImGui::CollapsingHeader("Identity", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::InputText("Name", ds.nameBuf, sizeof(ds.nameBuf)))
            e.name = ds.nameBuf;
        ImGui::Text("Type: %s", elementTypeName(e.type));
        ImGui::Text("ID: %u", e.id);
        if (ImGui::InputText("CSS Class", ds.cssBuf, sizeof(ds.cssBuf)))
            e.cssClass = ds.cssBuf;
        ImGui::Checkbox("Visible", &e.visible);
        ImGui::SameLine();
        ImGui::Checkbox("Locked", &e.locked);
    }

    // --- Anchor / Layout ---
    if (ImGui::CollapsingHeader("Anchor & Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawAnchorPresetGrid(ds);
        ImGui::Separator();

        ImGui::DragFloat2("Anchor Min", &e.anchor.min.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Anchor Max", &e.anchor.max.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Offset Min", &e.anchor.offsetMin.x, 1.0f);
        ImGui::DragFloat2("Offset Max", &e.anchor.offsetMax.x, 1.0f);
        ImGui::DragFloat2("Pivot",      &e.anchor.pivot.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Rotation",    &e.rotation, 0.5f, -360, 360);

        // Computed rect (read-only)
        ImGui::TextDisabled("Computed: %.0f,%.0f  %.0fx%.0f",
            e.computedRect.x, e.computedRect.y, e.computedRect.w, e.computedRect.h);
    }

    // --- Style ---
    if (ImGui::CollapsingHeader("Style", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit4("Background", &e.style.bgColor.x);
        ImGui::ColorEdit4("Text Color", &e.style.fgColor.x);
        ImGui::ColorEdit4("Border Color", &e.style.borderColor.x);
        ImGui::DragFloat("Border Width", &e.style.borderWidth, 0.1f, 0, 10);
        ImGui::DragFloat("Border Radius", &e.style.borderRadius, 0.5f, 0, 100);
        ImGui::DragFloat("Font Size", &e.style.fontSize, 0.5f, 4, 120);
        ImGui::SliderFloat("Opacity", &e.style.opacity, 0.0f, 1.0f);
        ImGui::DragFloat4("Padding", e.style.padding, 0.5f, 0, 100);
    }

    // --- Content ---
    if (ImGui::CollapsingHeader("Content", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (e.type == ElementType::Label || e.type == ElementType::Button ||
            e.type == ElementType::Input || e.type == ElementType::Checkbox ||
            e.type == ElementType::Toggle || e.type == ElementType::Slider ||
            e.type == ElementType::Progress || e.type == ElementType::Dropdown) {
            if (ImGui::InputText("Text", ds.textBuf, sizeof(ds.textBuf)))
                e.text = ds.textBuf;
        }
        if (e.type == ElementType::Image) {
            if (ImGui::InputText("Image Path", ds.imgBuf, sizeof(ds.imgBuf)))
                e.imagePath = ds.imgBuf;
        }
    }

    ImGui::End();
}

// ─── HTML/CSS Editor tab ───────────────────────────────────────────────────

inline void drawHtmlEditor(DesignerState& ds) {
    if (!ImGui::Begin("HTML + CSS", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Mode:");
    ImGui::SameLine();
    if (ds.mode == DesignerMode::Visual) {
        if (ImGui::Button("Switch to HTML/CSS")) {
            // Export current tree to HTML
            std::string html = exportToHTML(ds.root,
                static_cast<int>(ds.canvasW), static_cast<int>(ds.canvasH));
            std::strncpy(ds.htmlBuffer, html.c_str(), sizeof(ds.htmlBuffer) - 1);
            ds.htmlBuffer[sizeof(ds.htmlBuffer) - 1] = '\0';
            ds.mode = DesignerMode::HtmlCss;
            ds.htmlDirty = false;
        }
    } else {
        if (ImGui::Button("Switch to Visual")) {
            // Import HTML back into tree
            std::string html(ds.htmlBuffer);
            importFromHTML(html, ds.root);
            ds.selected = nullptr;
            ds.mode = DesignerMode::Visual;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply HTML")) {
            std::string html(ds.htmlBuffer);
            importFromHTML(html, ds.root);
            ds.selected = nullptr;
            ds.htmlDirty = false;
        }
    }

    ImGui::Separator();

    if (ds.mode == DesignerMode::HtmlCss) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (ImGui::InputTextMultiline("##htmledit", ds.htmlBuffer, sizeof(ds.htmlBuffer),
                                       avail, ImGuiInputTextFlags_AllowTabInput)) {
            ds.htmlDirty = true;
        }
    } else {
        // Show read-only preview
        std::string html = exportToHTML(ds.root,
            static_cast<int>(ds.canvasW), static_cast<int>(ds.canvasH));
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InputTextMultiline("##htmlpreview",
            const_cast<char*>(html.c_str()), html.size() + 1,
            avail, ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::End();
}

// ─── Save / Load ────────────────────────────────────────────────────────────

inline void saveDesign(DesignerState& ds, const std::filesystem::path& path) {
    std::string json = exportToJSON(ds.root);
    std::ofstream f(path);
    if (f) f << json;
}

inline void loadDesignHTML(DesignerState& ds, const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string html((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    importFromHTML(html, ds.root);
    ds.selected = nullptr;
}

// ─── Main draw function ────────────────────────────────────────────────────

inline void drawUIDesigner(DesignerState& ds, bool allowHeavy = true) {
    drawToolbar(ds);
    drawCanvas(ds, allowHeavy);
    drawProperties(ds);
    drawHtmlEditor(ds);
}

} // namespace myu::ui
