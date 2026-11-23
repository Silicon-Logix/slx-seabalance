// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "runtime.hpp"

#include <cstddef>

#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace slx::sb {
fs::path executable_directory()
{
#if defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Cannot resolve executable directory");
    return fs::path(std::wstring(path.data(), length)).parent_path();
#else
    return fs::read_symlink("/proc/self/exe").parent_path();
#endif
}

fs::path core_file(const fs::path &directory, bool model)
{
    const std::string kind = model ? "model" : "control";
#if defined(_WIN32)
    return directory / ("slx_seabalance_" + kind + ".dll");
#else
    return directory / ("libslx_seabalance_" + kind + ".so");
#endif
}

uint32_t platform_id() noexcept
{
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return 1;
#elif defined(__linux__) && defined(__x86_64__)
    return 2;
#elif defined(__linux__) && defined(__arm__) && defined(__ARM_PCS_VFP)
    return 3;
#else
    return 0;
#endif
}

void require_ok(SLX_SB_Result result, const char *operation)
{
    if (result == SLX_SB_OK)
        return;
    const char *reason = "unknown error";
    switch (result) {
    case SLX_SB_E_ARGUMENT: reason = "invalid argument"; break;
    case SLX_SB_E_ABI: reason = "incompatible ABI or structure size"; break;
    case SLX_SB_E_SEQUENCE: reason = "unexpected sample sequence/time"; break;
    case SLX_SB_E_MEMORY: reason = "allocation failed"; break;
    case SLX_SB_E_RANGE: reason = "unsupported mode, calibration or value outside operating range"; break;
    }
    throw std::runtime_error(std::string(operation) + " - " + reason);
}

Library::Library(const fs::path &file)
{
    const auto path = fs::absolute(file);
#if defined(_WIN32)
    handle_ = LoadLibraryExW(path.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle_)
        throw std::runtime_error("Cannot load " + path.filename().string() +
                                 " (Windows error " + std::to_string(GetLastError()) + ")");
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_)
        throw std::runtime_error("Cannot load " + path.filename().string() + " - " + dlerror());
#endif
}

Library::~Library()
{
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
}

void *Library::symbol(const char *name) const
{
#if defined(_WIN32)
    auto *address = reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    auto *address = dlsym(handle_, name);
#endif
    if (!address)
        throw std::runtime_error(std::string("Core entry point missing - ") + name);
    return address;
}

Model::Model(const fs::path &file, const SLX_SB_ModelConfig &config) : lib_(file)
{
    const auto get = reinterpret_cast<SLX_SB_GetModelAPIFn>(lib_.symbol("SLX_SB_GetModelAPI"));
    require_ok(get(SLX_SB_MODEL_ABI_VERSION, sizeof(api_), &api_), "Model ABI");
    if (api_.abi != SLX_SB_MODEL_ABI_VERSION || api_.size < offsetof(SLX_SB_ModelAPI, queue_update) || !api_.build ||
        !api_.create || !api_.destroy || !api_.observe || !api_.advance || !api_.stability_curve || !api_.set_navigation)
        throw std::runtime_error("Invalid model API table");
    require_ok(api_.create(&config, &instance_), "Model configuration");
}

Model::~Model()
{
    api_.destroy(instance_);
}

void Model::observe(SLX_SB_Sample &sample, SLX_SB_Reference &ref) const
{
    sample = abi_struct<SLX_SB_Sample>();
    ref = abi_struct<SLX_SB_Reference>();
    require_ok(api_.observe(instance_, &sample, &ref), "Model observation");
}

void Model::advance(const SLX_SB_Command &command) const
{
    require_ok(api_.advance(instance_, &command), "Model integration");
}

SLX_SB_StabilityCurve Model::stability_curve() const
{
    auto curve = abi_struct<SLX_SB_StabilityCurve>();
    require_ok(api_.stability_curve(instance_, &curve), "Stability curve");
    return curve;
}

void Model::set_navigation(double speed_mps, double relative_heading_rad) const
{
    const SLX_SB_NavigationTarget nav{sizeof(nav), SLX_SB_MODEL_ABI_VERSION, speed_mps, relative_heading_rad};
    require_ok(api_.set_navigation(instance_, &nav), "Model navigation target");
}

SLX_SB_Result Model::queue_update(const SLX_SB_ModelUpdate &update) const
{
    return api_.queue_update ? api_.queue_update(instance_, &update) : SLX_SB_E_ABI;
}
Control::Control(const SLX_SB_ControlAPI &api, uint32_t retained_cause, double command_rate)
    : api_(api)
{
    const SLX_SB_ControlConfig config{sizeof(config), SLX_SB_ABI_VERSION, SLX_SB_CALIBRATION_VERSION,
                                      retained_cause, command_rate};
    require_ok(api_.create(&config, &instance_), "Controller configuration");
}

Control::~Control()
{
    api_.destroy(instance_);
}

SLX_SB_Result Control::try_step(const SLX_SB_Sample &sample, SLX_SB_Command &out) const
{
    out = abi_struct<SLX_SB_Command>();
    return api_.step(instance_, &sample, &out);
}

SLX_SB_Command Control::step(const SLX_SB_Sample &sample) const
{
    SLX_SB_Command out{};
    require_ok(try_step(sample, out), "Controller step");
    return out;
}

ControlCore::ControlCore(const fs::path &file) : lib_(file)
{
    const auto get = reinterpret_cast<SLX_SB_GetControlAPIFn>(lib_.symbol("SLX_SB_GetControlAPI"));
    require_ok(get(SLX_SB_ABI_VERSION, sizeof(api_), &api_), "Control ABI");
    if (api_.abi != SLX_SB_ABI_VERSION || api_.size < sizeof(api_) || !api_.build ||
        !api_.create || !api_.destroy || !api_.step)
        throw std::runtime_error("Invalid control API table");
}
} // namespace slx::sb
