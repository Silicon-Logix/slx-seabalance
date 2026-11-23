// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include "scenario.hpp"

#include <memory>

namespace slx::sb {
struct Camera {
    float yaw{.51f}, elevation{.26f}, distance{23.f};
    bool dry{};
};
struct View {
    SLX_SB_Reference pose{};
    SLX_SB_ModelConfig scenario{};
    const char *clip{};
    double clip_s{};
    bool assembly{};
};

class Renderer {
public:
    explicit Renderer(const fs::path &assets);
    ~Renderer();
    unsigned render(unsigned slot, int width, int height, const View &, Camera);
    unsigned logo() const;
    bool has_presentation() const;
    static void screenshot(const fs::path &, int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace slx::sb
