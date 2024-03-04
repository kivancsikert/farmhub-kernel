#pragma once

#include <chrono>
#include <list>
#include <memory>

#include <Arduino.h>

#include <ArduinoJson.h>
#include <ArduinoLog.h>

#include <devices/Peripheral.hpp>
#include <kernel/Service.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using std::make_unique;
using std::move;
using std::unique_ptr;

using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals::valve {

class Valve
    : public Peripheral<ValveConfig> {
public:
    Valve(
        const String& name,
        SleepManager& sleepManager,
        PwmMotorDriver& controller,
        ValveControlStrategy& strategy,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot)
        : Peripheral<ValveConfig>(name, mqttRoot)
        , valve(name, sleepManager, controller, strategy, mqttRoot, [this]() {
            publishTelemetry();
        }) {
    }

    void configure(const ValveConfig& config) override {
        valve.setSchedules(config.schedule.get());
    }

    void populateTelemetry(JsonObject& telemetry) override {
        valve.populateTelemetry(telemetry);
    }

private:
    ValveComponent valve;
};

class ValveFactory
    : public PeripheralFactory<ValveDeviceConfig, ValveConfig, ValveControlStrategyType> {
public:
    ValveFactory(
        const std::list<ServiceRef<PwmMotorDriver>>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<ValveDeviceConfig, ValveConfig, ValveControlStrategyType>("valve", defaultStrategy)
        , motors(motors) {
    }

    unique_ptr<Peripheral<ValveConfig>> createPeripheral(const String& name, const ValveDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        PwmMotorDriver& targetMotor = findMotor(name, deviceConfig.motor.get(), motors);
        ValveControlStrategy* strategy;
        try {
            strategy = createValveControlStrategy(
                deviceConfig.strategy.get(),
                deviceConfig.switchDuration.get(),
                deviceConfig.duty.get() / 100.0);
        } catch (const std::exception& e) {
            throw PeripheralCreationException(name, "failed to create strategy: " + String(e.what()));
        }
        return make_unique<Valve>(name, services.sleepManager, targetMotor, *strategy, mqttRoot);
    }

    static PwmMotorDriver& findMotor(const String& name, const String& motorName, const std::list<ServiceRef<PwmMotorDriver>>& motors) {
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

}    // namespace farmhub::peripherals::valve
