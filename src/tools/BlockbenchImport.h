#pragma once
// =============================================================================
// BlockbenchImport.h – Minimal .bbmodel import (metadata only)
// =============================================================================

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace myu::tools {

struct BlockbenchModelInfo {
    std::string name;
    int elementCount = 0;
    int textureCount = 0;
    int resolutionX = 0;
    int resolutionY = 0;
};

inline bool loadBlockbenchModelInfo(const std::string& path, BlockbenchModelInfo& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // name
    std::smatch m;
    static const std::regex nameRx(R"bb("name"\s*:\s*"([^"]+)")bb");
    if (std::regex_search(json, m, nameRx))
        out.name = m[1].str();

    // count elements by "from" occurrences
    out.elementCount = 0;
    static const std::regex fromRx(R"bb("from"\s*:\s*\[)bb");
    auto it = std::sregex_iterator(json.begin(), json.end(), fromRx);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) out.elementCount++;

    // textures
    out.textureCount = 0;
    static const std::regex texRx(R"bb("textures"\s*:\s*\[)bb");
    auto it2 = std::sregex_iterator(json.begin(), json.end(), texRx);
    for (; it2 != end; ++it2) out.textureCount++;

    // resolution
    static const std::regex resRx(R"bb("resolution"\s*:\s*\[\s*(\d+)\s*,\s*(\d+)\s*\])bb");
    if (std::regex_search(json, m, resRx)) {
        out.resolutionX = std::stoi(m[1].str());
        out.resolutionY = std::stoi(m[2].str());
    }

    if (out.name.empty()) out.name = "BlockbenchModel";
    return true;
}

} // namespace myu::tools
