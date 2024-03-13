#pragma once

#include <chrono>
#include <deque>
#include <memory>

#include <Arduino.h>
#include <Wire.h>

#include <ArduinoLog.h>
#include <BH1750.h>

#include <kernel/Component.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Telemetry.hpp>
#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::light_sensor {

class Bh1750DeviceConfig
    : public I2CDeviceConfig {
public:
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class Bh1750Component
    : public Component,
      public TelemetryProvider {
public:
    Bh1750Component(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        const I2CConfig& config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : Component(name, mqttRoot)
        , measurementFrequency(measurementFrequency) {

        Log.infoln("Initializing BH1750 light sensor with %s",
            config.toString().c_str());

        // TODO Make mode configurable
        // TODO What's the difference between one-time and continuous mode here?
        //      Can we save some battery by using one-time mode? Are we losing anything by doing so?
        if (!sensor.begin(BH1750::CONTINUOUS_LOW_RES_MODE, config.address, &i2c.getWireFor(config))) {
            throw PeripheralCreationException("Failed to initialize BH1750 light sensor");
        }

        Task::loop(name, 3072, [this, measurementFrequency, latencyInterval](Task& task) {
            auto currentLevel = sensor.readLightLevel();

            size_t maxMaxmeasurements = latencyInterval.count() / measurementFrequency.count();
            while (measurements.size() >= maxMaxmeasurements) {
                sum -= measurements.front();
                measurements.pop_front();
            }
            measurements.emplace_back(currentLevel);
            sum += currentLevel;

            {
                Lock lock(updateAverageMutex);
                averageLevel = sum / measurements.size();
            }

            task.delayUntil(measurementFrequency);
        });
    }

    double getCurrentLevel() {
        Lock lock(updateAverageMutex);
        return averageLevel;
    }

    seconds getMeasurementFrequency() {
        return measurementFrequency;
    }

    void populateTelemetry(JsonObject& json) override {
        Lock lock(updateAverageMutex);
        json["light"] = averageLevel;
    }

private:
    BH1750 sensor;
    const seconds measurementFrequency;

    std::deque<double> measurements;
    double sum;

    Mutex updateAverageMutex;
    double averageLevel = 0;
};

class Bh1750
    : public Peripheral<EmptyConfiguration> {

public:
    Bh1750(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        const I2CConfig& config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , component(name, mqttRoot, i2c, config, measurementFrequency, latencyInterval) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        component.populateTelemetry(telemetryJson);
    }

private:
    Bh1750Component component;
};

class Bh1750Factory
    : public PeripheralFactory<Bh1750DeviceConfig, EmptyConfiguration> {
public:
    Bh1750Factory()
        : PeripheralFactory<Bh1750DeviceConfig, EmptyConfiguration>("light-sensor:bh1750", "light-sensor") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const Bh1750DeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        I2CConfig i2cConfig = deviceConfig.parse(0x23);
        return std::make_unique<Bh1750>(name, mqttRoot, services.i2c, i2cConfig, deviceConfig.measurementFrequency.get(), deviceConfig.latencyInterval.get());
    }
};

}    // namespace farmhub::peripherals::light_sensor
