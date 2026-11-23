// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#pragma once

#include <slx/seabalance.h>

#include <filesystem>
#include <type_traits>

namespace slx::sb {
namespace fs = std::filesystem;

fs::path executable_directory();
fs::path core_file(const fs::path &directory, bool model);
void require_ok(SLX_SB_Result result, const char *operation);
uint32_t platform_id() noexcept;

template <class T> T abi_struct()
{
    T value{};
    value.size = sizeof(T);
    if constexpr (std::is_same_v<T, SLX_SB_ModelConfig> || std::is_same_v<T, SLX_SB_Reference> ||
                  std::is_same_v<T, SLX_SB_StabilityCurve> || std::is_same_v<T, SLX_SB_NavigationTarget> ||
                  std::is_same_v<T, SLX_SB_ModelUpdate>)
        value.abi = SLX_SB_MODEL_ABI_VERSION;
    else
        value.abi = SLX_SB_ABI_VERSION;
    return value;
}

class Library {
public:
    explicit Library(const fs::path &);
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    void *symbol(const char *name) const;

private:
    void *handle_{};
};

class Model {
public:
    Model(const fs::path &core, const SLX_SB_ModelConfig &config);
    ~Model();
    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;
    void observe(SLX_SB_Sample &, SLX_SB_Reference &) const;
    void advance(const SLX_SB_Command &) const;
    SLX_SB_StabilityCurve stability_curve() const;
    void set_navigation(double speed_mps, double relative_heading_rad) const;
    SLX_SB_Result queue_update(const SLX_SB_ModelUpdate &) const;
    uint32_t build() const { return api_.build; }

private:
    Library lib_;
    SLX_SB_ModelAPI api_{};
    SLX_SB_Model *instance_{};
};

class Control {
public:
    Control(const SLX_SB_ControlAPI &, uint32_t retained_cause, double command_rate);
    ~Control();
    Control(const Control &) = delete;
    Control &operator=(const Control &) = delete;
    SLX_SB_Result try_step(const SLX_SB_Sample &sample, SLX_SB_Command &out) const;
    SLX_SB_Command step(const SLX_SB_Sample &) const;

private:
    SLX_SB_ControlAPI api_{};
    SLX_SB_Control *instance_{};
};

// Keep the library alive until all instances created from api() are destroyed.
class ControlCore {
public:
    explicit ControlCore(const fs::path &core);
    const SLX_SB_ControlAPI &api() const { return api_; }

private:
    Library lib_;
    SLX_SB_ControlAPI api_{};
};
} // namespace slx::sb
