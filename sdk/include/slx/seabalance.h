/* Silicon LogiX / SLX SeaBalance
 * Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
 * Author: Marco Pezzullo
 * License: Silicon LogiX Evaluation License 1.0. See LICENSE.
 */
#ifndef SLX_SEABALANCE_H
#define SLX_SEABALANCE_H

#include <stdint.h>

#if defined(SLX_SB_BUILD_DLL) && defined(_WIN32)
#define SLX_SB_EXPORT __declspec(dllexport)
#elif defined(SLX_SB_BUILD_DLL) && defined(__GNUC__)
#define SLX_SB_EXPORT __attribute__((visibility("default")))
#else
#define SLX_SB_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SLX_SB_ABI_VERSION 1u
#define SLX_SB_MODEL_ABI_VERSION 2u
#define SLX_SB_CALIBRATION_VERSION 1u
#define SLX_SB_STEP_NS UINT64_C(20000000)
#define SLX_SB_FIN_LIMIT_RAD 0.4363323129985824
#define SLX_SB_BUILD_VERSION 0x000300u
#define SLX_SB_CONTROLLER_BUILD_VERSION 0x000200u
#define SLX_SB_MAX_GZ_POINTS 32u
#define SLX_SB_GZ_SAMPLES 73u

typedef int32_t SLX_SB_Result;
enum {
    SLX_SB_OK = 0, SLX_SB_E_ARGUMENT = -1, SLX_SB_E_ABI = -2,
    SLX_SB_E_SEQUENCE = -3, SLX_SB_E_MEMORY = -4, SLX_SB_E_RANGE = -5
};
enum { SLX_SB_OFF, SLX_SB_MANUAL, SLX_SB_PID, SLX_SB_SLX };
enum { SLX_SB_REGULAR, SLX_SB_IRREGULAR };
enum {
    SLX_SB_NO_FAULT, SLX_SB_FIN_STUCK, SLX_SB_FIN_SLOW, SLX_SB_FIN_LIMITED,
    SLX_SB_GYRO_BIAS, SLX_SB_GYRO_CLIP, SLX_SB_STALE_IMU, SLX_SB_MISSING_IMU,
    SLX_SB_LINK_LOSS
};
enum {
    SLX_SB_ACCEL_VALID = 1u << 0, SLX_SB_GYRO_VALID = 1u << 1,
    SLX_SB_FINS_VALID = 1u << 2, SLX_SB_ACCEL_CLIPPED = 1u << 3,
    SLX_SB_GYRO_CLIPPED = 1u << 4, SLX_SB_ACK_ALARM = 1u << 5
};
enum {
    SLX_SB_EST_VALID = 1u << 0, SLX_SB_ACCEL_REJECTED = 1u << 1,
    SLX_SB_AT_LIMIT = 1u << 2, SLX_SB_SPEED_LIMIT = 1u << 3,
    SLX_SB_ALARM_ACKED = 1u << 4,
    SLX_SB_PORT_MISMATCH = 1u << 5, SLX_SB_STARBOARD_MISMATCH = 1u << 6
};
enum { SLX_SB_READY, SLX_SB_ACTIVE, SLX_SB_DEGRADED, SLX_SB_AWAIT_ACK };
enum {
    SLX_SB_CAUSE_NONE, SLX_SB_CAUSE_IMU, SLX_SB_CAUSE_STALE,
    SLX_SB_CAUSE_TRACKING, SLX_SB_CAUSE_LINK, SLX_SB_CAUSE_RESTART
};
enum {
    SLX_SB_STABILITY_AVAILABLE = 1u << 0, SLX_SB_OPENING_IMMERSED = 1u << 1,
    SLX_SB_RIGHTING_LOST = 1u << 2, SLX_SB_INVERTED = 1u << 3,
    SLX_SB_CAPSIZE_RECORDED = 1u << 4, SLX_SB_RESERVE_EXCEEDED = 1u << 5
};

typedef struct SLX_SB_GZPoint { double heel_rad, arm_m; } SLX_SB_GZPoint;

/* SI units, radians. Configuration is copied by create. */
typedef struct SLX_SB_ModelConfig {
    uint32_t size, abi;
    uint64_t seed;
    uint32_t sea_kind, fault;
    double speed_mps, relative_heading_rad, wave_height_m, wave_period_s;
    double load_ratio, cg_rise_m, initial_roll_rad;
    double actuator_rate_rad_s, actuator_delay_s, fault_at_s, fault_duration_s;
    double imu_port_m, imu_up_m;
    double accel_noise_mps2, gyro_noise_rad_s, gyro_bias_rad_s, gyro_drift_rad_s_sqrt_s;
    double accel_quant_mps2, gyro_quant_rad_s, accel_clip_mps2, gyro_clip_rad_s;
    uint32_t imu_drop_every, calibration;
    uint32_t gz_point_count, reserved;
    double positive_range_rad, downflood_rad, free_surface_loss_m, load_offset_m;
    double gm_modulation, flood_area_m2, flood_capacity_kg, flood_lever_m;
    double packet_height_m, packet_center_s, packet_width_s, jonswap_gamma;
    SLX_SB_GZPoint gz_curve[SLX_SB_MAX_GZ_POINTS];
} SLX_SB_ModelConfig;

/* The controller receives only this structure. No ground truth or fault schedule. */
typedef struct SLX_SB_Sample {
    uint32_t size, abi;
    uint64_t sequence, sim_ns, imu_ns;
    uint32_t valid, mode;
    double accel_mps2[3], gyro_rad_s[3];
    double speed_mps, port_feedback_rad, starboard_feedback_rad, manual_rad;
} SLX_SB_Sample;

typedef struct SLX_SB_Command {
    uint32_t size, abi;
    uint64_t sequence;
    uint32_t state, cause, flags, effective_mode;
    double port_rad, starboard_rad, estimated_roll_rad, estimated_rate_rad_s;
} SLX_SB_Command;

/* Local reference for rendering and analysis. */
typedef struct SLX_SB_Reference {
    uint32_t size, abi;
    uint64_t sequence, sim_ns;
    double roll_rad, roll_rate_rad_s, port_actual_rad, starboard_actual_rad;
    double wave_moment_nm, fin_moment_nm, wave_elevation_m;
    uint32_t imposed_fault, limited_fins;
    uint32_t stability_flags, reserved;
    double righting_moment_nm, damping_moment_nm, heel_moment_nm, roll_accel_rad_s2;
    double roll_unwrapped_rad, gm_m, gz_m, positive_range_rad, downflood_rad;
    double flooded_kg, reserve_j, kinetic_j, port_authority, starboard_authority;
    double relative_heading_rad, yaw_from_start_rad;
} SLX_SB_Reference;

typedef struct SLX_SB_StabilityCurve {
    uint32_t size, abi, count, reserved;
    SLX_SB_GZPoint points[SLX_SB_GZ_SAMPLES];
} SLX_SB_StabilityCurve;

typedef struct SLX_SB_NavigationTarget {
    uint32_t size, abi;
    double speed_mps, relative_heading_rad;
} SLX_SB_NavigationTarget;

enum {
    SLX_SB_UPDATE_SEA = 1u << 0, SLX_SB_UPDATE_LOAD = 1u << 1, SLX_SB_UPDATE_DELAY = 1u << 2,
    SLX_SB_UPDATE_NAVIGATION = 1u << 3, SLX_SB_UPDATE_FAULT = 1u << 4,
    SLX_SB_UPDATE_MASK = (1u << 5) - 1
};

typedef struct SLX_SB_LiveConditions {
    uint32_t sea_kind;
    double wave_height_m, wave_period_s;
    double load_ratio, cg_rise_m, free_surface_loss_m, load_offset_m, actuator_delay_s;
} SLX_SB_LiveConditions;

/* Selected fields take effect at the first sample after a successful advance.
 * Zero fault duration holds the fault until cancelled or replaced.
 */
typedef struct SLX_SB_ModelUpdate {
    uint32_t size, abi, fields, fault;
    SLX_SB_LiveConditions conditions;
    double speed_mps, relative_heading_rad, fault_duration_s;
} SLX_SB_ModelUpdate;

typedef struct SLX_SB_ControlConfig {
    uint32_t size, abi;
    uint32_t calibration, retained_cause;
    double command_rate_rad_s;
} SLX_SB_ControlConfig;

typedef struct SLX_SB_Model SLX_SB_Model;
typedef struct SLX_SB_Control SLX_SB_Control;
typedef struct SLX_SB_ModelAPI {
    uint32_t size, abi, build;
    SLX_SB_Result (*create)(const SLX_SB_ModelConfig *, SLX_SB_Model **);
    void (*destroy)(SLX_SB_Model *);
    SLX_SB_Result (*observe)(SLX_SB_Model *, SLX_SB_Sample *, SLX_SB_Reference *);
    SLX_SB_Result (*advance)(SLX_SB_Model *, const SLX_SB_Command *);
    SLX_SB_Result (*stability_curve)(SLX_SB_Model *, SLX_SB_StabilityCurve *);
    SLX_SB_Result (*set_navigation)(SLX_SB_Model *, const SLX_SB_NavigationTarget *);
    SLX_SB_Result (*queue_update)(SLX_SB_Model *, const SLX_SB_ModelUpdate *);
} SLX_SB_ModelAPI;
typedef struct SLX_SB_ControlAPI {
    uint32_t size, abi, build;
    SLX_SB_Result (*create)(const SLX_SB_ControlConfig *, SLX_SB_Control **);
    void (*destroy)(SLX_SB_Control *);
    SLX_SB_Result (*step)(SLX_SB_Control *, const SLX_SB_Sample *, SLX_SB_Command *);
} SLX_SB_ControlAPI;

typedef SLX_SB_Result (*SLX_SB_GetModelAPIFn)(uint32_t abi, uint32_t size, SLX_SB_ModelAPI *);
typedef SLX_SB_Result (*SLX_SB_GetControlAPIFn)(uint32_t abi, uint32_t size, SLX_SB_ControlAPI *);
SLX_SB_EXPORT SLX_SB_Result SLX_SB_GetModelAPI(uint32_t abi, uint32_t size, SLX_SB_ModelAPI *);
SLX_SB_EXPORT SLX_SB_Result SLX_SB_GetControlAPI(uint32_t abi, uint32_t size, SLX_SB_ControlAPI *);

#ifdef __cplusplus
}
#endif
#endif
