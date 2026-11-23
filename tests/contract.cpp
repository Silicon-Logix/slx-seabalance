// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "pid.hpp"
#include "runtime.hpp"
#include "wire.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace slx::sb;

void check(bool pass, const char *message)
{
    if (!pass)
        throw std::runtime_error(message);
}

int main()
{
    try {
        using namespace wire;
        Packet packet{};
        packet.kind = Kind::sample;
        packet.session = 0x1122334455667788;
        packet.sequence = 3;
        packet.sim_ns = 3 * SLX_SB_STEP_NS;
        packet.sample.imu_ns = packet.sim_ns;
        packet.sample.speed_mps = 4.75;
        packet.sample.accel_mps2[2] = 9.8;
        auto valid = encode(packet);
        check(valid.size == 144 && valid.bytes[0] == 'S' && valid.bytes[16] == 0x88,
              "Wire schema and little endian");
        auto decoded = decode({valid.bytes.data(), valid.size});
        check(decoded && decoded->session == packet.session && decoded->sample.speed_mps == 4.75,
              "Sensor round trip");
        for (size_t size = 0; size < valid.size; ++size)
            check(!decode({valid.bytes.data(), size}), "Short frame accepted");
        check(!decode(valid.bytes), "Oversized frame accepted");
        valid.bytes[4] = 2;
        check(!decode({valid.bytes.data(), valid.size}), "Incompatible protocol accepted");
        packet.sample.gyro_rad_s[0] = std::nan("");
        valid = encode(packet);
        check(!decode({valid.bytes.data(), valid.size}), "Non-finite sensor accepted");
        packet.sample.gyro_rad_s[0] = 0.;
        packet.kind = Kind::sample_operated;
        packet.navigation = {.8, 5.25, .65};
        valid = encode(packet);
        decoded = decode({valid.bytes.data(), valid.size});
        check(valid.size == 168 && decoded && decoded->navigation.target_speed_mps == 5.25 &&
              decoded->navigation.target_heading_rad == .65, "Extended sample round trip");
        packet.kind = Kind::command_operated;
        packet.order = {7, operator_local | operator_navigation | operator_mark, SLX_SB_MANUAL, .02, 5.25, .65};
        valid = encode(packet);
        decoded = decode({valid.bytes.data(), valid.size});
        check(valid.size == 136 && decoded && decoded->order.revision == 7 &&
              decoded->order.flags == packet.order.flags && decoded->order.relative_heading_rad == .65,
              "Operator order wire round trip");
        packet.order.flags = operator_navigation;
        valid = encode(packet);
        check(!decode({valid.bytes.data(), valid.size}), "Navigation accepted without panel ownership");

        Control control(reference_api(), SLX_SB_CAUSE_NONE, .95);
        auto sample = abi_struct<SLX_SB_Sample>();
        sample.valid = SLX_SB_ACCEL_VALID | SLX_SB_GYRO_VALID | SLX_SB_FINS_VALID;
        sample.mode = SLX_SB_PID;
        sample.speed_mps = 4.7;
        sample.accel_mps2[1] = std::sin(.13) * 9.80665;
        sample.accel_mps2[2] = std::cos(.13) * 9.80665;
        sample.gyro_rad_s[0] = .25;
        auto step = [&] {
            const auto command = control.step(sample);
            ++sample.sequence;
            sample.imu_ns = sample.sim_ns = sample.sequence * SLX_SB_STEP_NS;
            sample.port_feedback_rad = command.port_rad;
            sample.starboard_feedback_rad = command.starboard_rad;
            return command;
        };
        auto c = step();
        check((c.flags & SLX_SB_EST_VALID) && std::abs(c.estimated_roll_rad - .13) < .001,
              "Estimator not initialized from IMU");
        check(c.port_rad < 0 && c.starboard_rad > 0 && std::abs(c.port_rad) <= .0191,
              "Control sign or first transition slope");
        auto duplicate = sample;
        duplicate.sequence = 0;
        duplicate.sim_ns = 0;
        SLX_SB_Command rejected{};
        check(control.try_step(duplicate, rejected) == SLX_SB_E_SEQUENCE, "Duplicate sequence accepted");
        sample.valid = SLX_SB_FINS_VALID;
        c = step();
        check(c.cause == SLX_SB_CAUSE_IMU && c.state == SLX_SB_DEGRADED && c.effective_mode == SLX_SB_OFF,
              "Invalid gyro did not latch and degrade");
        sample.valid |= SLX_SB_ACCEL_VALID | SLX_SB_GYRO_VALID;
        for (int i = 0; i < 28; ++i)
            c = step();
        check(c.state == SLX_SB_AWAIT_ACK, "Recovery should wait for the operator");
        sample.valid |= SLX_SB_ACK_ALARM;
        c = step();
        check(c.cause == SLX_SB_CAUSE_NONE && (c.flags & SLX_SB_ALARM_ACKED), "Acknowledgement lost");
        sample.valid &= ~SLX_SB_ACK_ALARM;
        sample.speed_mps = 0;
        c = step();
        check(c.effective_mode == SLX_SB_OFF && (c.flags & SLX_SB_SPEED_LIMIT), "Speed authority missing");
        std::cout << "ABI, controller safeguards, estimator and protocol passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
