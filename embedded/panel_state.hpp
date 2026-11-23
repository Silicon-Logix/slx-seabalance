// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include "runtime.hpp"
#include "wire.hpp"

#include <atomic>
#include <memory>

namespace slx::sb {
struct PanelState {
    wire::Timing timing{};
    bool connected{}, sampled{}, operator_enabled{}, has_slx{};
    uint64_t boot{}, session{}, updated_ns{}, sequence{};
    uint32_t invalid_frames{}, last_alarm{};
    SLX_SB_Sample sample{};
    SLX_SB_Command command{};
    wire::Navigation navigation;
    wire::OperatorOrder order;
};

class PanelWriter {
public:
    PanelWriter(fs::path state, fs::path runtime, uint64_t boot);
    ~PanelWriter();
    PanelWriter(const PanelWriter &) = delete;
    PanelWriter &operator=(const PanelWriter &) = delete;
    PanelState initial() const;
    void update(const PanelState &, const char *event = nullptr);
    bool take_ack(uint64_t session);
    std::optional<wire::OperatorOrder> take_order(uint64_t session);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace slx::sb
