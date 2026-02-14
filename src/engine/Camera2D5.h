#pragma once
// =============================================================================
// Camera2D5.h – 2.5D camera with orbit rotation, zoom, smooth follow, shake
// =============================================================================

#include <algorithm>
#include <cmath>
#include <random>

namespace myu::engine {

struct Camera2D5 {
    // World position target
    float targetX = 0, targetY = 0;
    float zoom    = 1.0f;

    // 2.5D orbit (degrees)
    float orbitAngle = 0.0f;    // rotation around vertical axis
    float tiltAngle  = 45.0f;   // pitch from top (30°–60° is classic 2.5D)

    // Smoothed values (interpolated each frame)
    float smoothX = 0, smoothY = 0;
    float smoothZoom   = 1.0f;
    float smoothOrbit  = 0.0f;
    float smoothing    = 8.0f;  // lerp speed

    // Shake
    float shakeIntensity = 0;
    float shakeDuration  = 0;
    float shakeTimer     = 0;
    float shakeOffX = 0, shakeOffY = 0;

    // Constraints
    float minZoom = 0.2f, maxZoom = 5.0f;
    float minTilt = 15.0f, maxTilt = 75.0f;

    // ── Update ──

    void update(float dt) {
        float t = 1.0f - std::exp(-smoothing * dt);
        smoothX     += (targetX     - smoothX)     * t;
        smoothY     += (targetY     - smoothY)     * t;
        smoothZoom  += (zoom        - smoothZoom)   * t;
        smoothOrbit += (orbitAngle  - smoothOrbit)  * t;

        // Shake
        if (shakeTimer > 0) {
            shakeTimer -= dt;
            float factor = shakeTimer / shakeDuration;
            static std::mt19937 rng(42);
            std::uniform_real_distribution<float> dist(-1.f, 1.f);
            shakeOffX = dist(rng) * shakeIntensity * factor;
            shakeOffY = dist(rng) * shakeIntensity * factor;
        } else {
            shakeOffX = shakeOffY = 0;
        }
    }

    void shake(float intensity, float duration) {
        shakeIntensity = intensity;
        shakeDuration  = duration;
        shakeTimer     = duration;
    }

    void setZoom(float z) { zoom = std::clamp(z, minZoom, maxZoom); }
    void setTilt(float t) { tiltAngle = std::clamp(t, minTilt, maxTilt); }

    // ── 2.5D Projection ──
    // Project world (x, y, z) → screen (sx, sy)
    // z = elevation (height above board plane)

    void worldToScreen(float wx, float wy, float wz,
                       float& sx, float& sy) const {
        float radO = smoothOrbit * 3.14159265f / 180.f;
        float radT = tiltAngle   * 3.14159265f / 180.f;
        float cosO = std::cos(radO), sinO = std::sin(radO);
        float cosT = std::cos(radT);

        float rx = (wx - smoothX) * cosO - (wy - smoothY) * sinO;
        float ry = (wx - smoothX) * sinO + (wy - smoothY) * cosO;

        sx = (rx + shakeOffX) * smoothZoom;
        sy = (ry * cosT - wz + shakeOffY) * smoothZoom;
    }

    // Screen → world (assumes z = 0 ground plane)
    void screenToWorld(float sx, float sy,
                       float& wx, float& wy) const {
        float radO = smoothOrbit * 3.14159265f / 180.f;
        float radT = tiltAngle   * 3.14159265f / 180.f;
        float cosO = std::cos(-radO), sinO = std::sin(-radO);
        float cosT = std::cos(radT);

        float rx = (sx / smoothZoom) - shakeOffX;
        float ry = ((sy / smoothZoom) - shakeOffY) / cosT;

        wx = rx * cosO - ry * sinO + smoothX;
        wy = rx * sinO + ry * cosO + smoothY;
    }

    // ── Isometric helpers ──

    // Convert grid (row, col) → world center position
    static void gridToWorld(int row, int col, float cellSize,
                            float& wx, float& wy) {
        wx = col * cellSize;
        wy = row * cellSize;
    }

    // Convert world → grid (floored)
    static void worldToGrid(float wx, float wy, float cellSize,
                            int& row, int& col) {
        col = static_cast<int>(std::floor(wx / cellSize));
        row = static_cast<int>(std::floor(wy / cellSize));
    }
};

} // namespace myu::engine
