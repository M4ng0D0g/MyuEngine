#pragma once
// =============================================================================
// Resources.h – Simple memory-friendly resource registry
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace myu::engine {

enum class ResourceType { Texture, Model, Material, Audio, Font, Unknown };

struct ResourceHandle {
    uint32_t id = 0;
    ResourceType type = ResourceType::Unknown;
    bool valid() const { return id != 0; }
};

struct ResourceEntry {
    ResourceType type = ResourceType::Unknown;
    std::string  name;
    std::string  path;
    std::string  meta;
    int          refCount = 0;
    bool         loaded = false;
};

class ResourceManager {
public:
    ResourceHandle add(ResourceType type, const std::string& name, const std::string& path,
                       const std::string& meta = "") {
        ResourceEntry e; e.type = type; e.name = name; e.path = path; e.meta = meta;
        entries_.push_back(e);
        return {static_cast<uint32_t>(entries_.size()), type};
    }

    ResourceEntry* get(ResourceHandle h) {
        if (!h.valid() || h.id == 0 || h.id > entries_.size()) return nullptr;
        return &entries_[h.id - 1];
    }

    const std::vector<ResourceEntry>& entries() const { return entries_; }

    ResourceHandle findByName(ResourceType type, const std::string& name) const {
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].type == type && entries_[i].name == name)
                return {static_cast<uint32_t>(i + 1), type};
        }
        return {};
    }

private:
    std::vector<ResourceEntry> entries_;
};

} // namespace myu::engine
