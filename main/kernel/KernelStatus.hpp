#pragma once

#include <chrono>
#include <concepts>
#include <functional>
#include <optional>

#include <esp_system.h>

#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>

#include <devices/DeviceConfiguration.hpp>

#include <kernel/I2CManager.hpp>
#include <kernel/NetworkUtil.hpp>
#include <kernel/PowerManager.hpp>
#include <kernel/StateManager.hpp>
#include <kernel/Watchdog.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <kernel/drivers/MdnsDriver.hpp>
#include <kernel/drivers/RtcDriver.hpp>
#include <kernel/drivers/SwitchManager.hpp>
#include <kernel/drivers/WiFiDriver.hpp>
#include <kernel/mqtt/MqttDriver.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

namespace farmhub::kernel {

static RTC_DATA_ATTR int bootCount = 0;

class KernelStatusTask;

struct ModuleStates {
private:
    StateManager manager;
    friend class KernelStatusTask;

public:
    StateSource networkConnecting = manager.createStateSource("network-connecting");
    StateSource networkReady = manager.createStateSource("network-ready");
    StateSource configPortalRunning = manager.createStateSource("config-portal-running");
    StateSource mdnsReady = manager.createStateSource("mdns-ready");
    StateSource rtcInSync = manager.createStateSource("rtc-in-sync");
    StateSource mqttReady = manager.createStateSource("mqtt-ready");
    StateSource kernelReady = manager.createStateSource("kernel-ready");
};

class KernelStatusTask {
public:
    static void init(std::shared_ptr<LedDriver> statusLed, std::shared_ptr<ModuleStates> states) {
        Task::run("status-update", 3072, [statusLed, states](Task&) {
            updateState(statusLed, states);
        });
    }

private:
    enum class KernelState {
        BOOTING,
        NETWORK_CONNECTING,
        NETWORK_CONFIGURING,
        RTC_SYNCING,
        MQTT_CONNECTING,
        INIT_FINISHING,
        TRANSMITTING,
        IDLE
    };

    static void updateState(std::shared_ptr<LedDriver> statusLed, std::shared_ptr<ModuleStates> states) {
        KernelState state = KernelState::BOOTING;
        while (true) {
            KernelState newState;
            if (states->configPortalRunning.isSet()) {
                // We are waiting for the user to configure the network
                newState = KernelState::NETWORK_CONFIGURING;
            } else if (states->networkConnecting.isSet()) {
                // We are waiting for network connection
                newState = KernelState::NETWORK_CONNECTING;
            } else if (!states->rtcInSync.isSet()) {
                newState = KernelState::RTC_SYNCING;
            } else if (!states->mqttReady.isSet()) {
                // We are waiting for MQTT connection
                newState = KernelState::MQTT_CONNECTING;
            } else if (!states->kernelReady.isSet()) {
                // We are waiting for init to finish
                newState = KernelState::INIT_FINISHING;
            } else if (states->networkReady.isSet()) {
                newState = KernelState::TRANSMITTING;
            } else {
                newState = KernelState::IDLE;
            }

            if (newState != state) {
                LOGD("Kernel state changed from %d to %d",
                    static_cast<int>(state), static_cast<int>(newState));
                state = newState;
                switch (newState) {
                    case KernelState::BOOTING:
                        statusLed->turnOff();
                        break;
                    case KernelState::NETWORK_CONNECTING:
                        statusLed->blink(200ms);
                        break;
                    case KernelState::NETWORK_CONFIGURING:
                        statusLed->blinkPattern({ 100ms, -100ms, 100ms, -100ms, 100ms, -500ms });
                        break;
                    case KernelState::RTC_SYNCING:
                        statusLed->blink(500ms);
                        break;
                    case KernelState::MQTT_CONNECTING:
                        statusLed->blink(1000ms);
                        break;
                    case KernelState::INIT_FINISHING:
                        statusLed->blink(1500ms);
                        break;
                    case KernelState::TRANSMITTING:
                        statusLed->turnOff();
                        break;
                    case KernelState::IDLE:
                        statusLed->turnOff();
                        break;
                };
            }
            states->manager.awaitStateChange();
        }
    }
};

}    // namespace farmhub::kernel
