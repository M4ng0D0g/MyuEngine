#pragma once
// =============================================================================
// UIElement.h – Core data model for the 2D UI designer
// Anchor-based layout system (similar to Unity RectTransform)
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace myu::ui {

// ─── Basic types ────────────────────────────────────────────────────────────

struct Vec2 { float x = 0, y = 0; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 1; };  // also RGBA

inline Vec4 rgba(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }
inline Vec4 hex(uint32_t c, float a = 1.0f) {
    return {((c >> 16) & 0xFF) / 255.0f,
            ((c >>  8) & 0xFF) / 255.0f,
            ((c      ) & 0xFF) / 255.0f, a};
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// ─── Anchor ─────────────────────────────────────────────────────────────────
//
//  anchorMin/anchorMax are in [0..1] relative to parent rect.
//  offsetMin/offsetMax are pixel offsets applied after anchor mapping.
//
//  Example: stretch to full parent  → anchorMin={0,0} anchorMax={1,1} offsets=0
//           center, fixed 200x100   → anchorMin={0.5,0.5} anchorMax={0.5,0.5}
//                                      offsetMin={-100,-50} offsetMax={100,50}
//
struct Anchor {
    Vec2 min  = {0, 0};    // normalised anchor for left-top
    Vec2 max  = {0, 0};    // normalised anchor for right-bottom
    Vec2 offsetMin = {0, 0};   // pixel offset from anchor min
    Vec2 offsetMax = {100, 40}; // pixel offset from anchor max
    Vec2 pivot = {0.5f, 0.5f}; // pivot point for transforms
};

// Pre-defined anchor presets
enum class AnchorPreset {
    TopLeft,     TopCenter,     TopRight,     TopStretch,
    MiddleLeft,  MiddleCenter,  MiddleRight,  MiddleStretch,
    BottomLeft,  BottomCenter,  BottomRight,  BottomStretch,
    StretchLeft, StretchCenter, StretchRight, StretchAll,
    kCount
};

inline void applyAnchorPreset(Anchor& a, AnchorPreset preset, float w = 100, float h = 40) {
    float hw = w * 0.5f, hh = h * 0.5f;
    // rows: y anchors   cols: x anchors
    static const float yAnchors[][2] = {
        {0,0},{0.5f,0.5f},{1,1},{0,1}
    };
    static const float xAnchors[][2] = {
        {0,0},{0.5f,0.5f},{1,1},{0,1}
    };
    int row = static_cast<int>(preset) / 4;
    int col = static_cast<int>(preset) % 4;

    a.min.x = xAnchors[col][0];
    a.max.x = xAnchors[col][1];
    a.min.y = yAnchors[row][0];
    a.max.y = yAnchors[row][1];

    bool stretchX = (col == 3);
    bool stretchY = (row == 3);

    if (stretchX) { a.offsetMin.x = 0; a.offsetMax.x = 0; }
    else          { a.offsetMin.x = -hw; a.offsetMax.x = hw; }

    if (stretchY) { a.offsetMin.y = 0; a.offsetMax.y = 0; }
    else          { a.offsetMin.y = -hh; a.offsetMax.y = hh; }
}

// ─── Element types ──────────────────────────────────────────────────────────

enum class ElementType {
    Panel,
    Button,
    Label,
    Image,
    Input,
    Checkbox,
    Toggle,
    Slider,
    Progress,
    Dropdown,
    Container,   // layout container (flex-like)
    kCount
};

inline const char* elementTypeName(ElementType t) {
    static const char* names[] = {
        "Panel", "Button", "Label", "Image", "Input",
        "Checkbox", "Toggle", "Slider", "Progress", "Dropdown",
        "Container"
    };
    return names[static_cast<int>(t)];
}

// ─── Visual style ───────────────────────────────────────────────────────────

struct Style {
    Vec4  bgColor     = {0.2f, 0.2f, 0.25f, 1.0f};
    Vec4  fgColor     = {1, 1, 1, 1};             // text / icon color
    Vec4  borderColor = {0.4f, 0.4f, 0.5f, 1.0f};
    float borderWidth = 1.0f;
    float borderRadius= 0.0f;
    float fontSize    = 14.0f;
    float opacity     = 1.0f;
    float padding[4]  = {4, 4, 4, 4}; // top right bottom left
};

// ─── Breakpoint overrides ────────────────────────────────────────────────

struct BreakpointOverride {
    std::string name;
    Anchor anchor;
    Style  style;
    bool   useAnchor = false;
    bool   useStyle  = false;
};

// ─── UIElement ──────────────────────────────────────────────────────────────

struct UIElement {
    // Identity
    uint32_t    id   = 0;
    std::string name = "Element";
    ElementType type = ElementType::Panel;

    // Layout
    Anchor  anchor;
    float   rotation = 0; // degrees
    bool    visible  = true;
    bool    locked   = false;

    // Visuals
    Style   style;

    // Responsive overrides
    std::vector<BreakpointOverride> overrides;

    // Content
    std::string text;         // for Label / Button
    std::string imagePath;    // for Image
    std::string cssClass;     // optional CSS class name

    // Hierarchy
    UIElement*                              parent = nullptr;
    std::vector<std::unique_ptr<UIElement>> children;

    // ── Computed rect (resolved at layout time) ──
    Rect computedRect = {};

    // ── Methods ──

    Rect resolveRectWithAnchor(const Rect& parentRect, const Anchor& a) const {
        float px = parentRect.x, py = parentRect.y;
        float pw = parentRect.w, ph = parentRect.h;

        float x0 = px + pw * a.min.x + a.offsetMin.x;
        float y0 = py + ph * a.min.y + a.offsetMin.y;
        float x1 = px + pw * a.max.x + a.offsetMax.x;
        float y1 = py + ph * a.max.y + a.offsetMax.y;

        return {x0, y0, x1 - x0, y1 - y0};
    }

    Rect resolveRect(const Rect& parentRect) const {
        return resolveRectWithAnchor(parentRect, anchor);
    }

    const BreakpointOverride* findOverride(const std::string& name) const {
        for (auto& o : overrides)
            if (o.name == name) return &o;
        return nullptr;
    }

    BreakpointOverride* findOverride(const std::string& name) {
        for (auto& o : overrides)
            if (o.name == name) return &o;
        return nullptr;
    }

    BreakpointOverride& findOrCreateOverride(const std::string& name) {
        if (auto* o = findOverride(name)) return *o;
        BreakpointOverride o;
        o.name = name;
        o.anchor = anchor;
        o.style = style;
        overrides.push_back(o);
        return overrides.back();
    }

    const Anchor& getAnchorForBreakpoint(const std::string* bp) const {
        if (bp && !bp->empty()) {
            if (auto* o = findOverride(*bp); o && o->useAnchor)
                return o->anchor;
        }
        return anchor;
    }

    const Style& getStyleForBreakpoint(const std::string* bp) const {
        if (bp && !bp->empty()) {
            if (auto* o = findOverride(*bp); o && o->useStyle)
                return o->style;
        }
        return style;
    }

    void layout(const Rect& parentRect, const std::string* bp = nullptr) {
        const Anchor& a = getAnchorForBreakpoint(bp);
        computedRect = resolveRectWithAnchor(parentRect, a);
        for (auto& child : children)
            child->layout(computedRect, bp);
    }

    UIElement* hitTest(float px, float py) {
        if (!visible) return nullptr;
        // Children first (front-to-back = last child on top)
        for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
            if (auto* hit = children[i]->hitTest(px, py))
                return hit;
        }
        if (computedRect.contains(px, py))
            return this;
        return nullptr;
    }

    UIElement* findById(uint32_t targetId) {
        if (id == targetId) return this;
        for (auto& c : children)
            if (auto* found = c->findById(targetId)) return found;
        return nullptr;
    }

    // Detach from current parent and move under newParent
    std::unique_ptr<UIElement> detach() {
        if (!parent) return nullptr;
        auto& siblings = parent->children;
        for (auto it = siblings.begin(); it != siblings.end(); ++it) {
            if (it->get() == this) {
                auto owned = std::move(*it);
                siblings.erase(it);
                owned->parent = nullptr;
                return owned;
            }
        }
        return nullptr;
    }

    void addChild(std::unique_ptr<UIElement> child) {
        child->parent = this;
        children.push_back(std::move(child));
    }

    void moveChildBefore(UIElement* child, UIElement* before) {
        // find and extract child
        std::unique_ptr<UIElement> owned;
        for (auto it = children.begin(); it != children.end(); ++it) {
            if (it->get() == child) {
                owned = std::move(*it);
                children.erase(it);
                break;
            }
        }
        if (!owned) return;
        if (!before) {
            children.push_back(std::move(owned));
            return;
        }
        for (auto it = children.begin(); it != children.end(); ++it) {
            if (it->get() == before) {
                children.insert(it, std::move(owned));
                return;
            }
        }
        children.push_back(std::move(owned));
    }
};

// ─── ID generator ──────────────────────────────────────────────────────────

inline uint32_t nextElementId() {
    static uint32_t counter = 1;
    return counter++;
}

inline std::unique_ptr<UIElement> createElement(ElementType type, const std::string& name = "") {
    auto e = std::make_unique<UIElement>();
    e->id   = nextElementId();
    e->type = type;
    e->name = name.empty() ? elementTypeName(type) : name;

    // Sensible defaults per type
    switch (type) {
        case ElementType::Panel:
            e->style.bgColor = {0.15f, 0.15f, 0.18f, 0.9f};
            e->anchor.offsetMax = {200, 150};
            break;
        case ElementType::Button:
            e->style.bgColor = {0.25f, 0.45f, 0.85f, 1.0f};
            e->style.borderRadius = 4.0f;
            e->text = "Button";
            e->anchor.offsetMax = {120, 36};
            break;
        case ElementType::Label:
            e->style.bgColor = {0, 0, 0, 0};
            e->style.borderWidth = 0;
            e->text = "Label";
            e->anchor.offsetMax = {100, 24};
            break;
        case ElementType::Image:
            e->style.bgColor = {0.3f, 0.3f, 0.3f, 1.0f};
            e->anchor.offsetMax = {100, 100};
            break;
        case ElementType::Input:
            e->style.bgColor = {0.12f, 0.12f, 0.15f, 1.0f};
            e->style.borderWidth = 1.0f;
            e->text = "";
            e->anchor.offsetMax = {180, 30};
            break;
        case ElementType::Checkbox:
            e->style.bgColor = {0, 0, 0, 0};
            e->style.borderWidth = 0;
            e->text = "Checkbox";
            e->anchor.offsetMax = {140, 24};
            break;
        case ElementType::Toggle:
            e->style.bgColor = {0.2f, 0.6f, 0.3f, 1.0f};
            e->style.borderRadius = 12.0f;
            e->text = "Toggle";
            e->anchor.offsetMax = {90, 28};
            break;
        case ElementType::Slider:
            e->style.bgColor = {0.18f, 0.18f, 0.22f, 1.0f};
            e->style.borderWidth = 1.0f;
            e->text = "Slider";
            e->anchor.offsetMax = {200, 28};
            break;
        case ElementType::Progress:
            e->style.bgColor = {0.18f, 0.18f, 0.22f, 1.0f};
            e->style.borderWidth = 1.0f;
            e->text = "Progress";
            e->anchor.offsetMax = {200, 22};
            break;
        case ElementType::Dropdown:
            e->style.bgColor = {0.2f, 0.2f, 0.25f, 1.0f};
            e->style.borderWidth = 1.0f;
            e->text = "Dropdown";
            e->anchor.offsetMax = {160, 30};
            break;
        case ElementType::Container:
            e->style.bgColor = {0.1f, 0.1f, 0.12f, 0.5f};
            e->style.borderWidth = 1.0f;
            e->anchor.offsetMax = {300, 200};
            break;
        default: break;
    }
    return e;
}

} // namespace myu::ui
