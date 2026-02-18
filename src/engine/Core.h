#pragma once
// =============================================================================
// Core.h – GameObject, Property, Component, Scene
// Design-oriented property system for visual editing
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace myu::engine {

// ─── Basic 3D types ─────────────────────────────────────────────────────────

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

// ─── Property value ─────────────────────────────────────────────────────────

using PropValue = std::variant<bool, int, float, std::string, Vec3, Color>;

enum class PropType { Bool, Int, Float, String, Vec3, Color };

inline PropType propTypeOf(const PropValue& v) {
    return static_cast<PropType>(v.index());
}

// ─── Property ───────────────────────────────────────────────────────────────
// A named, typed value that appears in the Inspector panel.

struct Property {
    std::string name;
    std::string display;    // UI label (defaults to name)
    std::string category;   // inspector group
    std::string tooltip;
    PropValue   value;
    float       rangeMin = 0, rangeMax = 0; // 0,0 = no constraint
    std::vector<std::string> enumOptions;   // dropdown options for string
    bool        readOnly = false;

    template <typename T>
    T as(T def = {}) const {
        if (auto* v = std::get_if<T>(&value)) return *v;
        return def;
    }
};

// ─── Component ──────────────────────────────────────────────────────────────
// A named group of properties attached to a GameObject.
// Pre-defined types: "Sprite", "BoxCollider", "Animator", "AudioSource",
//     "Label", "BoardPiece", "CardHolder" …

struct Component {
    std::string typeName;
    bool enabled = true;
    std::vector<Property> properties;

    Property* findProp(const std::string& n) {
        for (auto& p : properties)
            if (p.name == n) return &p;
        return nullptr;
    }
    const Property* findProp(const std::string& n) const {
        for (auto& p : properties)
            if (p.name == n) return &p;
        return nullptr;
    }

    template <typename T>
    T get(const std::string& n, T def = {}) const {
        auto* p = findProp(n);
        return p ? p->as<T>(def) : def;
    }

    void set(const std::string& n, PropValue val,
             const std::string& disp = "") {
        if (auto* p = findProp(n)) {
            p->value = val;
        } else {
            properties.push_back(
                {n, disp.empty() ? n : disp, "", "", val});
        }
    }

    Property& addProp(const std::string& n, PropValue defVal,
                      const std::string& disp = "",
                      const std::string& cat = "") {
        properties.push_back(
            {n, disp.empty() ? n : disp, cat, "", defVal});
        return properties.back();
    }
};

// ─── Component templates ────────────────────────────────────────────────────

namespace components {

inline Component sprite(const std::string& path = "", float w = 1, float h = 1) {
    Component c; c.typeName = "Sprite";
    c.addProp("path",   std::string(path), "Image Path", "Rendering");
    c.addProp("width",  w,  "Width",  "Size");
    c.addProp("height", h,  "Height", "Size");
    c.addProp("flipX",  false, "Flip X", "Rendering");
    c.addProp("flipY",  false, "Flip Y", "Rendering");
    c.addProp("color",  Color{1,1,1,1}, "Tint", "Rendering");
    return c;
}

inline Component boxCollider(float w = 1, float h = 1) {
    Component c; c.typeName = "BoxCollider";
    c.addProp("width",     w,     "Width");
    c.addProp("height",    h,     "Height");
    c.addProp("isTrigger", false, "Is Trigger");
    return c;
}

inline Component animator() {
    Component c; c.typeName = "Animator";
    c.addProp("clip",    std::string(""), "Animation Clip");
    c.addProp("speed",   1.0f,            "Speed");
    c.addProp("loop",    true,             "Loop");
    c.addProp("playing", false,            "Playing");
    return c;
}

inline Component audioSource() {
    Component c; c.typeName = "AudioSource";
    c.addProp("clip",        std::string(""), "Audio Clip");
    auto& vol = c.addProp("volume", 1.0f, "Volume");
    vol.rangeMin = 0; vol.rangeMax = 1;
    c.addProp("loop",        false, "Loop");
    c.addProp("playOnStart", false, "Play On Start");
    return c;
}

inline Component label(const std::string& text = "Text", float size = 16) {
    Component c; c.typeName = "Label";
    c.addProp("text",     text, "Text");
    c.addProp("fontSize", size, "Font Size");
    c.addProp("color",    Color{1,1,1,1}, "Color");
    auto& al = c.addProp("align", std::string("center"), "Alignment");
    al.enumOptions = {"left", "center", "right"};
    return c;
}

} // namespace components

// ─── GameObject ─────────────────────────────────────────────────────────────

struct GameObject {
    uint32_t    id      = 0;
    std::string name    = "Object";
    std::string tag;            // "piece", "card", "ui", "board" …
    bool        active  = true;
    bool        visible = true;
    int         layer   = 0;    // render order (higher = front)

    // Transform
    Vec3 position = {0, 0, 0};
    Vec3 rotation = {0, 0, 0};  // Euler degrees
    Vec3 scale    = {1, 1, 1};

    // Built-in sprite shortcut
    std::string spritePath;
    // 3D model/material
    std::string modelPath;
    std::string materialName;
    Color       tint = {1, 1, 1, 1};
    float       width = 1, height = 1;  // world units

    // Components
    std::vector<Component> components;

    // Hierarchy
    GameObject* parent = nullptr;
    std::vector<std::unique_ptr<GameObject>> children;

    // ── Component API ──

    Component* getComponent(const std::string& type) {
        for (auto& c : components) if (c.typeName == type) return &c;
        return nullptr;
    }
    Component& addComponent(const Component& comp) {
        components.push_back(comp);
        return components.back();
    }
    Component& requireComponent(const std::string& type) {
        if (auto* c = getComponent(type)) return *c;
        components.push_back({type});
        return components.back();
    }

    // ── Hierarchy API ──

    void addChild(std::unique_ptr<GameObject> child) {
        child->parent = this;
        children.push_back(std::move(child));
    }

    std::unique_ptr<GameObject> detach() {
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

    // ── Search ──

    GameObject* findById(uint32_t tid) {
        if (id == tid) return this;
        for (auto& c : children)
            if (auto* f = c->findById(tid)) return f;
        return nullptr;
    }
    GameObject* findByName(const std::string& n) {
        if (name == n) return this;
        for (auto& c : children)
            if (auto* f = c->findByName(n)) return f;
        return nullptr;
    }
    GameObject* findByTag(const std::string& t) {
        if (tag == t) return this;
        for (auto& c : children)
            if (auto* f = c->findByTag(t)) return f;
        return nullptr;
    }
    void forEachRecursive(const std::function<void(GameObject&)>& fn) {
        fn(*this);
        for (auto& c : children) c->forEachRecursive(fn);
    }
};

// ─── Scene ──────────────────────────────────────────────────────────────────

struct Scene {
    std::string name = "Main Scene";
    Color bgColor      = {0.05f, 0.05f, 0.08f, 1.0f};
    Color ambientLight = {0.3f,  0.3f,  0.35f, 1.0f};

    std::vector<std::unique_ptr<GameObject>> roots;
    uint32_t nextId = 1;

    GameObject* createObject(const std::string& n,
                             const std::string& tag = "",
                             GameObject* parent = nullptr) {
        auto obj  = std::make_unique<GameObject>();
        obj->id   = nextId++;
        obj->name = n;
        obj->tag  = tag;
        GameObject* raw = obj.get();
        if (parent) parent->addChild(std::move(obj));
        else        roots.push_back(std::move(obj));
        return raw;
    }

    bool removeObject(uint32_t id) {
        for (auto it = roots.begin(); it != roots.end(); ++it) {
            if ((*it)->id == id) { roots.erase(it); return true; }
        }
        for (auto& r : roots) {
            auto* f = r->findById(id);
            if (f && f->parent) { f->detach(); return true; }
        }
        return false;
    }

    GameObject* findById(uint32_t id) {
        for (auto& r : roots) if (auto* f = r->findById(id)) return f;
        return nullptr;
    }
    GameObject* findByName(const std::string& n) {
        for (auto& r : roots) if (auto* f = r->findByName(n)) return f;
        return nullptr;
    }

    std::vector<GameObject*> findAllByTag(const std::string& tag) {
        std::vector<GameObject*> result;
        for (auto& r : roots)
            r->forEachRecursive([&](GameObject& o) {
                if (o.tag == tag) result.push_back(&o);
            });
        return result;
    }

    void forEachObject(const std::function<void(GameObject&)>& fn) {
        for (auto& r : roots) r->forEachRecursive(fn);
    }
};

} // namespace myu::engine
