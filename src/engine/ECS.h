#pragma once
// =============================================================================
// ECS.h – Minimal cache-friendly ECS (sparse arrays + component masks)
// =============================================================================

#include "Core.h"

#include <cstdint>
#include <string>
#include <vector>

namespace myu::engine {

using Entity = uint32_t;
static constexpr Entity kInvalidEntity = 0;

enum ComponentMask : uint64_t {
    kCompTransform = 1ull << 0,
    kCompRender    = 1ull << 1,
    kCompName      = 1ull << 2,
    kCompTag       = 1ull << 3,
};

struct Transform3D {
    Vec3 position = {0, 0, 0};
    Vec3 rotation = {0, 0, 0};
    Vec3 scale    = {1, 1, 1};
};

struct RenderMesh {
    uint32_t modelId   = 0;  // Resource handle id
    uint32_t materialId= 0;
    bool     visible   = true;
};

struct Name {
    std::string value;
};

struct Tag {
    std::string value;
};

class ECSWorld {
public:
    Entity createEntity(const std::string& name = "Entity") {
        Entity id = static_cast<Entity>(entities_.size() + 1);
        entities_.push_back(id);
        masks_.push_back(0);
        transforms_.push_back({});
        renders_.push_back({});
        names_.push_back({name});
        tags_.push_back({""});
        masks_.back() |= kCompTransform | kCompName;
        return id;
    }

    size_t count() const { return entities_.size(); }
    const std::vector<Entity>& entities() const { return entities_; }

    uint64_t mask(Entity e) const { return inRange(e) ? masks_[idx(e)] : 0; }

    Transform3D* transform(Entity e) { return inRange(e) ? &transforms_[idx(e)] : nullptr; }
    RenderMesh* render(Entity e) { return inRange(e) ? &renders_[idx(e)] : nullptr; }
    Name* name(Entity e) { return inRange(e) ? &names_[idx(e)] : nullptr; }
    Tag* tag(Entity e) { return inRange(e) ? &tags_[idx(e)] : nullptr; }

    void addRender(Entity e) { if (inRange(e)) masks_[idx(e)] |= kCompRender; }
    void removeRender(Entity e) { if (inRange(e)) masks_[idx(e)] &= ~kCompRender; }
    void setTag(Entity e, const std::string& v) {
        if (!inRange(e)) return;
        tags_[idx(e)].value = v;
        masks_[idx(e)] |= kCompTag;
    }

    bool has(Entity e, uint64_t compMask) const {
        return inRange(e) && ((masks_[idx(e)] & compMask) == compMask);
    }

private:
    bool inRange(Entity e) const { return e != kInvalidEntity && idx(e) < entities_.size(); }
    size_t idx(Entity e) const { return static_cast<size_t>(e - 1); }

    std::vector<Entity>     entities_;
    std::vector<uint64_t>   masks_;
    std::vector<Transform3D> transforms_;
    std::vector<RenderMesh>  renders_;
    std::vector<Name>        names_;
    std::vector<Tag>         tags_;
};

} // namespace myu::engine
