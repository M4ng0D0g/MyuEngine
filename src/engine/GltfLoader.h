#pragma once
// =============================================================================
// GltfLoader.h – Minimal glTF 2.0 loader (positions/normals/indices only)
// =============================================================================

#include "Core.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace myu::engine {

struct MeshData {
    std::vector<float> vertices; // interleaved pos(3) + normal(3)
    int vertexCount = 0;
};

inline bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    out.resize(size);
    f.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

inline bool parseGlb(const std::filesystem::path& path, nlohmann::json& outJson,
                     std::vector<uint8_t>& outBin, std::string& err) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes)) {
        err = "Failed to read GLB";
        return false;
    }
    if (bytes.size() < 12) {
        err = "GLB too small";
        return false;
    }
    uint32_t magic = *reinterpret_cast<uint32_t*>(bytes.data());
    uint32_t version = *reinterpret_cast<uint32_t*>(bytes.data() + 4);
    if (magic != 0x46546C67 || version != 2) {
        err = "Invalid GLB header";
        return false;
    }
    size_t offset = 12;
    std::string jsonText;
    outBin.clear();

    while (offset + 8 <= bytes.size()) {
        uint32_t chunkLen = *reinterpret_cast<uint32_t*>(bytes.data() + offset);
        uint32_t chunkType = *reinterpret_cast<uint32_t*>(bytes.data() + offset + 4);
        offset += 8;
        if (offset + chunkLen > bytes.size()) break;
        if (chunkType == 0x4E4F534A) { // JSON
            jsonText.assign(reinterpret_cast<char*>(bytes.data() + offset), chunkLen);
        } else if (chunkType == 0x004E4942) { // BIN
            outBin.assign(bytes.begin() + offset, bytes.begin() + offset + chunkLen);
        }
        offset += chunkLen;
    }

    if (jsonText.empty()) {
        err = "Missing JSON chunk";
        return false;
    }
    outJson = nlohmann::json::parse(jsonText, nullptr, false);
    if (outJson.is_discarded()) {
        err = "Failed to parse JSON";
        return false;
    }
    return true;
}

inline bool parseGltf(const std::filesystem::path& path, nlohmann::json& outJson,
                      std::vector<std::vector<uint8_t>>& outBuffers, std::string& err) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes)) {
        err = "Failed to read glTF";
        return false;
    }
    outJson = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (outJson.is_discarded()) {
        err = "Failed to parse glTF JSON";
        return false;
    }
    if (!outJson.contains("buffers")) {
        err = "No buffers in glTF";
        return false;
    }
    std::filesystem::path base = path.parent_path();
    outBuffers.clear();
    for (auto& b : outJson["buffers"]) {
        if (!b.contains("uri")) {
            err = "Buffer missing uri";
            return false;
        }
        std::filesystem::path bufPath = base / b["uri"].get<std::string>();
        std::vector<uint8_t> buf;
        if (!readFileBytes(bufPath, buf)) {
            err = "Failed to read buffer: " + bufPath.string();
            return false;
        }
        outBuffers.push_back(std::move(buf));
    }
    return true;
}

inline size_t accessorComponentSize(int componentType) {
    switch (componentType) {
        case 5120: return 1; // BYTE
        case 5121: return 1; // UNSIGNED_BYTE
        case 5122: return 2; // SHORT
        case 5123: return 2; // UNSIGNED_SHORT
        case 5125: return 4; // UNSIGNED_INT
        case 5126: return 4; // FLOAT
        default: return 0;
    }
}

inline size_t accessorTypeCount(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 0;
}

inline void readAccessorFloat(const nlohmann::json& j,
                              const std::vector<std::vector<uint8_t>>& buffers,
                              int accessorIndex,
                              std::vector<float>& out) {
    out.clear();
    const auto& acc = j["accessors"][accessorIndex];
    int count = acc["count"].get<int>();
    int componentType = acc["componentType"].get<int>();
    std::string type = acc["type"].get<std::string>();
    size_t compSize = accessorComponentSize(componentType);
    size_t typeCount = accessorTypeCount(type);

    if (!acc.contains("bufferView")) return;
    int viewIndex = acc["bufferView"].get<int>();
    size_t accOffset = acc.value("byteOffset", 0);

    const auto& view = j["bufferViews"][viewIndex];
    int bufferIndex = view["buffer"].get<int>();
    size_t viewOffset = view.value("byteOffset", 0);
    size_t stride = view.value("byteStride", (int)(compSize * typeCount));

    const auto& buf = buffers[bufferIndex];
    out.resize(count * typeCount);

    for (int i = 0; i < count; ++i) {
        size_t base = viewOffset + accOffset + i * stride;
        for (size_t c = 0; c < typeCount; ++c) {
            size_t off = base + c * compSize;
            float v = 0.0f;
            if (componentType == 5126) {
                v = *reinterpret_cast<const float*>(buf.data() + off);
            } else if (componentType == 5123) {
                v = static_cast<float>(*reinterpret_cast<const uint16_t*>(buf.data() + off));
            } else if (componentType == 5125) {
                v = static_cast<float>(*reinterpret_cast<const uint32_t*>(buf.data() + off));
            } else if (componentType == 5121) {
                v = static_cast<float>(*reinterpret_cast<const uint8_t*>(buf.data() + off));
            }
            out[i * typeCount + c] = v;
        }
    }
}

inline void readAccessorIndices(const nlohmann::json& j,
                                const std::vector<std::vector<uint8_t>>& buffers,
                                int accessorIndex,
                                std::vector<uint32_t>& out) {
    out.clear();
    const auto& acc = j["accessors"][accessorIndex];
    int count = acc["count"].get<int>();
    int componentType = acc["componentType"].get<int>();

    if (!acc.contains("bufferView")) return;
    int viewIndex = acc["bufferView"].get<int>();
    size_t accOffset = acc.value("byteOffset", 0);

    const auto& view = j["bufferViews"][viewIndex];
    int bufferIndex = view["buffer"].get<int>();
    size_t viewOffset = view.value("byteOffset", 0);

    const auto& buf = buffers[bufferIndex];
    out.resize(count);

    for (int i = 0; i < count; ++i) {
        size_t off = viewOffset + accOffset;
        if (componentType == 5123) {
            out[i] = *reinterpret_cast<const uint16_t*>(buf.data() + off + i * 2);
        } else if (componentType == 5125) {
            out[i] = *reinterpret_cast<const uint32_t*>(buf.data() + off + i * 4);
        } else if (componentType == 5121) {
            out[i] = *reinterpret_cast<const uint8_t*>(buf.data() + off + i);
        } else {
            out[i] = i;
        }
    }
}

inline bool buildMeshFromJson(const nlohmann::json& j,
                              const std::vector<std::vector<uint8_t>>& buffers,
                              MeshData& out, std::string& err) {
    if (!j.contains("meshes")) {
        err = "No meshes";
        return false;
    }
    const auto& mesh = j["meshes"][0];
    if (!mesh.contains("primitives") || mesh["primitives"].empty()) {
        err = "No primitives";
        return false;
    }
    const auto& prim = mesh["primitives"][0];
    if (!prim.contains("attributes") || !prim["attributes"].contains("POSITION")) {
        err = "Missing POSITION";
        return false;
    }
    int posAcc = prim["attributes"]["POSITION"].get<int>();
    int normAcc = prim["attributes"].value("NORMAL", -1);

    std::vector<float> positions;
    std::vector<float> normals;
    readAccessorFloat(j, buffers, posAcc, positions);
    if (normAcc >= 0) readAccessorFloat(j, buffers, normAcc, normals);

    std::vector<uint32_t> indices;
    if (prim.contains("indices")) {
        int idxAcc = prim["indices"].get<int>();
        readAccessorIndices(j, buffers, idxAcc, indices);
    }

    size_t vcount = positions.size() / 3;
    if (vcount == 0) {
        err = "No vertex data";
        return false;
    }

    if (normals.size() / 3 != vcount) {
        normals.assign(vcount * 3, 0.0f);
        for (size_t i = 0; i + 2 < vcount; i += 3) {
            Vec3 a{positions[i*3+0], positions[i*3+1], positions[i*3+2]};
            Vec3 b{positions[(i+1)*3+0], positions[(i+1)*3+1], positions[(i+1)*3+2]};
            Vec3 c{positions[(i+2)*3+0], positions[(i+2)*3+1], positions[(i+2)*3+2]};
            Vec3 n = normalize(cross(b - a, c - a));
            normals[i*3+0] = n.x; normals[i*3+1] = n.y; normals[i*3+2] = n.z;
            normals[(i+1)*3+0] = n.x; normals[(i+1)*3+1] = n.y; normals[(i+1)*3+2] = n.z;
            normals[(i+2)*3+0] = n.x; normals[(i+2)*3+1] = n.y; normals[(i+2)*3+2] = n.z;
        }
    }

    out.vertices.clear();
    if (!indices.empty()) {
        for (size_t i = 0; i < indices.size(); ++i) {
            uint32_t idx = indices[i];
            if (idx >= vcount) continue;
            out.vertices.push_back(positions[idx * 3 + 0]);
            out.vertices.push_back(positions[idx * 3 + 1]);
            out.vertices.push_back(positions[idx * 3 + 2]);
            out.vertices.push_back(normals[idx * 3 + 0]);
            out.vertices.push_back(normals[idx * 3 + 1]);
            out.vertices.push_back(normals[idx * 3 + 2]);
        }
    } else {
        for (size_t i = 0; i < vcount; ++i) {
            out.vertices.push_back(positions[i * 3 + 0]);
            out.vertices.push_back(positions[i * 3 + 1]);
            out.vertices.push_back(positions[i * 3 + 2]);
            out.vertices.push_back(normals[i * 3 + 0]);
            out.vertices.push_back(normals[i * 3 + 1]);
            out.vertices.push_back(normals[i * 3 + 2]);
        }
    }
    out.vertexCount = static_cast<int>(out.vertices.size() / 6);
    return true;
}

inline bool loadGltfMesh(const std::filesystem::path& path, MeshData& out, std::string& err) {
    nlohmann::json j;
    std::vector<std::vector<uint8_t>> buffers;

    if (path.extension() == ".glb") {
        std::vector<uint8_t> bin;
        if (!parseGlb(path, j, bin, err)) return false;
        buffers.clear();
        buffers.push_back(std::move(bin));
    } else {
        if (!parseGltf(path, j, buffers, err)) return false;
    }

    return buildMeshFromJson(j, buffers, out, err);
}

} // namespace myu::engine
