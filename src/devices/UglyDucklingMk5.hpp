#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8874Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <peripherals/chicken_door/ChickenDoor.hpp>
#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>

#include <devices/DeviceDefinition.hpp>
#include <devices/Pin.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::chicken_door;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub {
namespace devices {

class Mk5Config
    : public DeviceConfiguration {
public:
    Mk5Config()
        : DeviceConfiguration("mk5") {
    }
};

namespace pins {
static gpio_num_t BOOT = Pin::registerPin("BOOT", GPIO_NUM_0);
static gpio_num_t BATTERY = Pin::registerPin("BATTERY", GPIO_NUM_1);
static gpio_num_t STATUS = Pin::registerPin("STATUS", GPIO_NUM_2);
static gpio_num_t AIPROPI = Pin::registerPin("AIPROPI", GPIO_NUM_4);

static gpio_num_t IOA1 = Pin::registerPin("A1", GPIO_NUM_5);
static gpio_num_t IOA2 = Pin::registerPin("A2", GPIO_NUM_6);
static gpio_num_t BIPROPI = Pin::registerPin("BIPROPI", GPIO_NUM_7);
static gpio_num_t IOB1 = Pin::registerPin("B1", GPIO_NUM_15);
static gpio_num_t AIN1 = Pin::registerPin("AIN1", GPIO_NUM_16);
static gpio_num_t AIN2 = Pin::registerPin("AIN2", GPIO_NUM_17);
static gpio_num_t BIN1 = Pin::registerPin("BIN1", GPIO_NUM_18);
static gpio_num_t BIN2 = Pin::registerPin("BIN2", GPIO_NUM_8);

static gpio_num_t DMINUS = Pin::registerPin("D-", GPIO_NUM_19);
static gpio_num_t DPLUS = Pin::registerPin("D+", GPIO_NUM_20);

static gpio_num_t IOB2 = Pin::registerPin("B2", GPIO_NUM_9);

static gpio_num_t NSLEEP = Pin::registerPin("NSLEEP", GPIO_NUM_10);
static gpio_num_t NFault = Pin::registerPin("NFault", GPIO_NUM_11);
static gpio_num_t IOC4 = Pin::registerPin("C4", GPIO_NUM_12);
static gpio_num_t IOC3 = Pin::registerPin("C3", GPIO_NUM_13);
static gpio_num_t IOC2 = Pin::registerPin("C2", GPIO_NUM_14);
static gpio_num_t IOC1 = Pin::registerPin("C1", GPIO_NUM_21);
static gpio_num_t IOD4 = Pin::registerPin("D4", GPIO_NUM_47);
static gpio_num_t IOD3 = Pin::registerPin("D3", GPIO_NUM_48);

static gpio_num_t SDA = Pin::registerPin("SDA", GPIO_NUM_35);
static gpio_num_t SCL = Pin::registerPin("SCL", GPIO_NUM_36);

static gpio_num_t IOD1 = Pin::registerPin("D1", GPIO_NUM_37);
static gpio_num_t IOD2 = Pin::registerPin("D2", GPIO_NUM_38);

static gpio_num_t TCK = Pin::registerPin("TCK", GPIO_NUM_39);
static gpio_num_t TDO = Pin::registerPin("TDO", GPIO_NUM_40);
static gpio_num_t TDI = Pin::registerPin("TDI", GPIO_NUM_41);
static gpio_num_t TMS = Pin::registerPin("TMS", GPIO_NUM_42);
static gpio_num_t RXD0 = Pin::registerPin("RXD0", GPIO_NUM_44);
static gpio_num_t TXD0 = Pin::registerPin("TXD0", GPIO_NUM_43);
}    // namespace pins

class UglyDucklingMk5 : public BatteryPoweredDeviceDefinition<Mk5Config> {
public:
    UglyDucklingMk5()
        : BatteryPoweredDeviceDefinition<Mk5Config>(
            pins::STATUS,
            pins::BOOT,
            pins::BATTERY, 2.4848) {
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
        peripheralManager.registerFactory(valveFactory);
        peripheralManager.registerFactory(flowMeterFactory);
        peripheralManager.registerFactory(flowControlFactory);
        peripheralManager.registerFactory(chickenDoorFactory);
    }

    Drv8874Driver motorADriver {
        pwm,
        pins::AIN1,
        pins::AIN2,
        pins::AIPROPI,
        pins::NFault,
        pins::NSLEEP
    };

    Drv8874Driver motorBDriver {
        pwm,
        pins::BIN1,
        pins::BIN2,
        pins::AIPROPI,
        pins::NFault,
        pins::NSLEEP
    };

    const ServiceRef<PwmMotorDriver> motorA { "a", motorADriver };
    const ServiceRef<PwmMotorDriver> motorB { "b", motorBDriver };
    const std::list<ServiceRef<PwmMotorDriver>> motors { motorA, motorB };

    ValveFactory valveFactory { motors, ValveControlStrategyType::Latching };
    FlowMeterFactory flowMeterFactory;
    FlowControlFactory flowControlFactory { motors, ValveControlStrategyType::Latching };
    ChickenDoorFactory chickenDoorFactory { motors };
};

}}    // namespace farmhub::devices
