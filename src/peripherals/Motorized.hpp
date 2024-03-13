#pragma once

#include <list>

#include <Arduino.h>
#include <ArduinoLog.h>

#include <devices/Peripheral.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/MotorDriver.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

class Motorized {
public:
    Motorized(const std::list<ServiceRef<PwmMotorDriver>>& motors)
        : motors(motors) {
    }

protected:
    PwmMotorDriver& findMotor(const String& name, const String& motorName) {
        // If there's only one motor and no name is specified, use it
        if (motorName.isEmpty() && motors.size() == 1) {
            return motors.front().get();
        }
        for (auto& motor : motors) {
            if (motor.getName() == motorName) {
                return motor.get();
            }
        }
        throw PeripheralCreationException(name, "failed to find motor: " + motorName);
    }

private:
    const std::list<ServiceRef<PwmMotorDriver>> motors;
};

}    // namespace farmhub::peripherals
