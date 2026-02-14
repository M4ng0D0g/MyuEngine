#pragma once
// =============================================================================
// VoxelEditor.h – Simple voxel editor (2D slice) for pixel/block models
// =============================================================================

#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace myu::editor {

struct VoxelModel {
    int sx = 16, sy = 16, sz = 16;
    std::vector<uint32_t> voxels; // RGBA8

    void resize(int x, int y, int z) {
        sx = x; sy = y; sz = z;
        voxels.assign(sx * sy * sz, 0x00000000u);
    }

    uint32_t get(int x, int y, int z) const {
        int idx = (z * sy + y) * sx + x;
        if (idx < 0 || idx >= (int)voxels.size()) return 0;
        return voxels[idx];
    }

    void set(int x, int y, int z, uint32_t rgba) {
        int idx = (z * sy + y) * sx + x;
        if (idx < 0 || idx >= (int)voxels.size()) return;
        voxels[idx] = rgba;
    }
};

struct VoxelEditorState {
    VoxelModel model;
    int sliceZ = 0;
    uint32_t color = 0xFFCC66FFu;
    bool erase = false;

    VoxelEditorState() { model.resize(16, 16, 16); }
};

inline ImU32 rgbaToImU32(uint32_t c) {
    uint8_t r = (c >> 24) & 0xFF;
    uint8_t g = (c >> 16) & 0xFF;
    uint8_t b = (c >> 8) & 0xFF;
    uint8_t a = (c) & 0xFF;
    return IM_COL32(r, g, b, a);
}

inline void drawVoxelEditor(VoxelEditorState& st, bool allowHeavy = true) {
    if (!ImGui::Begin("Voxel Modeler")) {
        ImGui::End();
        return;
    }

    ImGui::Text("Size: %dx%dx%d", st.model.sx, st.model.sy, st.model.sz);
    ImGui::SameLine();
    if (ImGui::Button("Reset")) st.model.resize(16, 16, 16);

    ImGui::SliderInt("Slice Z", &st.sliceZ, 0, std::max(0, st.model.sz - 1));
    ImGui::Checkbox("Eraser", &st.erase);

    float col[4] = {
        ((st.color >> 24) & 0xFF) / 255.0f,
        ((st.color >> 16) & 0xFF) / 255.0f,
        ((st.color >> 8) & 0xFF) / 255.0f,
        (st.color & 0xFF) / 255.0f
    };
    if (ImGui::ColorEdit4("Brush", col)) {
        uint32_t r = (uint32_t)(col[0] * 255.0f);
        uint32_t g = (uint32_t)(col[1] * 255.0f);
        uint32_t b = (uint32_t)(col[2] * 255.0f);
        uint32_t a = (uint32_t)(col[3] * 255.0f);
        st.color = (r << 24) | (g << 16) | (b << 8) | a;
    }

    if (!allowHeavy) {
        ImGui::TextDisabled("Voxel editor paused (Idle)");
        ImGui::End();
        return;
    }

    // Grid
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float cell = std::min(avail.x / (st.model.sx + 1), avail.y / (st.model.sy + 1));
    cell = std::max(10.0f, std::min(cell, 32.0f));

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size(cell * st.model.sx, cell * st.model.sy);
    ImGui::InvisibleButton("##voxgrid", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGuiIO& io = ImGui::GetIO();

    for (int y = 0; y < st.model.sy; ++y) {
        for (int x = 0; x < st.model.sx; ++x) {
            ImVec2 p0(origin.x + x * cell, origin.y + y * cell);
            ImVec2 p1(p0.x + cell, p0.y + cell);
            uint32_t v = st.model.get(x, y, st.sliceZ);
            ImU32 colv = (v == 0) ? IM_COL32(40, 40, 45, 255) : rgbaToImU32(v);
            dl->AddRectFilled(p0, p1, colv);
            dl->AddRect(p0, p1, IM_COL32(70, 70, 80, 255));

            if (ImGui::IsItemHovered()) {
                ImVec2 mp = io.MousePos;
                if (mp.x >= p0.x && mp.x < p1.x && mp.y >= p0.y && mp.y < p1.y) {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        st.model.set(x, y, st.sliceZ, st.erase ? 0x00000000u : st.color);
                    }
                }
            }
        }
    }

    ImGui::End();
}

} // namespace myu::editor
