#pragma once

#include <chrono>
#include <memory>

#include <Arduino.h>

#include "esp_netif.h"
#include "esp_wifi.h"
#include <esp_pm.h>
#include <esp_private/esp_clk.h>

#include <Print.h>

#include <kernel/Command.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Console.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Task.hpp>
#include <kernel/drivers/RtcDriver.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using std::shared_ptr;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
typedef farmhub::devices::UglyDucklingMk4 TDeviceDefinition;
typedef farmhub::devices::Mk4Config TDeviceConfiguration;

/**
 * @brief Do not boot if battery is below this threshold.
 */
static constexpr double BATTERY_BOOT_THRESHOLD = 0;

/**
 * @brief Shutdown if battery drops below this threshold.
 */
static constexpr double BATTERY_SHUTDOWN_THRESHOLD = 0;

#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
typedef farmhub::devices::UglyDucklingMk5 TDeviceDefinition;
typedef farmhub::devices::Mk5Config TDeviceConfiguration;

/**
 * @brief Do not boot if battery is below this threshold.
 */
static constexpr double BATTERY_BOOT_THRESHOLD = 0;

/**
 * @brief Shutdown if battery drops below this threshold.
 */
static constexpr double BATTERY_SHUTDOWN_THRESHOLD = 0;

#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
typedef farmhub::devices::UglyDucklingMk6 TDeviceDefinition;
typedef farmhub::devices::Mk6Config TDeviceConfiguration;

/**
 * @brief Do not boot if battery is below this threshold.
 */
static constexpr double BATTERY_BOOT_THRESHOLD = 3.7;

/**
 * @brief Shutdown if battery drops below this threshold.
 */
static constexpr double BATTERY_SHUTDOWN_THRESHOLD = 3.6;

#elif defined(MK7)
#include <devices/UglyDucklingMk7.hpp>
typedef farmhub::devices::UglyDucklingMk7 TDeviceDefinition;
typedef farmhub::devices::Mk7Config TDeviceConfiguration;

/**
 * @brief Do not boot if battery is below this threshold.
 */
static constexpr double BATTERY_BOOT_THRESHOLD = 3.2;

/**
 * @brief Shutdown if battery drops below this threshold.
 */
static constexpr double BATTERY_SHUTDOWN_THRESHOLD = 3.0;

#else
#error "No device defined"
#endif

namespace farmhub::devices {

#ifdef FARMHUB_DEBUG
class ConsolePrinter {
public:
    ConsolePrinter(const shared_ptr<BatteryDriver> battery)
        : battery(battery) {
        status.reserve(256);
        Task::loop("console", 3072, 1, [this](Task& task) {
            printStatus();
            task.delayUntil(100ms);
        });
    }

private:
    void printStatus() {
        static const char* spinner = "|/-\\";
        static const int spinnerLength = strlen(spinner);

        status.clear();
        counter = (counter + 1) % spinnerLength;
        status.concat("[");
        status.concat(spinner[counter]);
        status.concat("] ");

        status.concat("\033[33m");
        status.concat(FARMHUB_VERSION);
        status.concat("\033[0m");

        status.concat(", WIFI: ");
        status.concat(wifiStatus());

        status.concat(", uptime: \033[33m");
        status.concat(String(float(millis()) / 1000.0f, 1));
        status.concat("\033[0m s");

        status.concat(", RTC: \033[33m");
        status.concat(RtcDriver::isTimeSet() ? "OK" : "UNSYNCED");
        status.concat("\033[0m");

        status.concat(", heap: \033[33m");
        status.concat(String(float(ESP.getFreeHeap()) / 1024.0f, 2));
        status.concat("\033[0m kB");

        status.concat(", CPU: \033[33m");
        status.concat(esp_clk_cpu_freq() / 1000000);
        status.concat("\033[0m MHz");

        if (battery != nullptr) {
            status.concat(", battery: \033[33m");
            status.concat(String(battery->getVoltage(), 2));
            status.concat("\033[0m V");
        }

        printf("\033[1G\033[0K%s", status.c_str());
    }

    static const char* wifiStatus() {
        auto netif = esp_netif_get_default_netif();
        if (!netif) {
            return "\033[0;31moff\033[0m";
        }

        wifi_mode_t mode;
        ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));

        switch (mode) {
            case WIFI_MODE_STA:
                break;
            case WIFI_MODE_NULL:
                return "\033[0;31moff\033[0m";
            case WIFI_MODE_AP:
                return "\033[0;32mAP\033[0m";
            case WIFI_MODE_APSTA:
                return "\033[0;32mAPSTA\033[0m";
            default:
                return "\033[0;31munknown mode\033[0m";
        }

        // Retrieve the current Wi-Fi station connection status
        wifi_ap_record_t ap_info;
        esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

        // TODO Handle ESP_ERR_WIFI_CONN, or better yet, use `WiFiDriver` directly
        switch (err) {
            case ESP_OK:
                break;
            case ESP_ERR_WIFI_CONN:
                return "\033[0;32mconnection-error\033[0m";
            case ESP_ERR_WIFI_NOT_CONNECT:
                return "\033[0;33mdisconnected\033[0m";
            case ESP_ERR_WIFI_NOT_STARTED:
                return "\033[0;31mWi-Fi not started\033[0m";
            default:
                return esp_err_to_name(err);
        }

        // Check IP address
        esp_netif_ip_info_t ip_info;
        err = esp_netif_get_ip_info(netif, &ip_info);
        if (err == ESP_OK && ip_info.ip.addr != 0) {
            static char ip_str[32];
            snprintf(ip_str, sizeof(ip_str), "\033[0;33m" IPSTR "\033[0m", IP2STR(&ip_info.ip));
            return ip_str;
        }

        return "\033[0;31midle\033[0m";
    }

    int counter;
    String status;
    const std::shared_ptr<BatteryDriver> battery;
};
#endif

class MemoryTelemetryProvider : public TelemetryProvider {
public:
    void populateTelemetry(JsonObject& json) override {
        json["free-heap"] = ESP.getFreeHeap();
    }
};

class MqttTelemetryPublisher : public TelemetryPublisher {
public:
    MqttTelemetryPublisher(shared_ptr<MqttDriver::MqttRoot> mqttRoot, TelemetryCollector& telemetryCollector)
        : mqttRoot(mqttRoot)
        , telemetryCollector(telemetryCollector) {
    }

    void publishTelemetry() {
        mqttRoot->publish("telemetry", [&](JsonObject& json) { telemetryCollector.collect(json); });
    }

private:
    shared_ptr<MqttDriver::MqttRoot> mqttRoot;
    TelemetryCollector& telemetryCollector;
};

class ConfiguredKernel {
public:
    ConfiguredKernel(Queue<LogRecord>& logRecords)
        : consoleProvider(logRecords, deviceDefinition.config.publishLogs.get())
        , battery(deviceDefinition.createBatteryDriver(kernel.i2c)) {
        if (battery != nullptr) {
            // If the battery voltage is below threshold, we should not boot yet.
            // This is to prevent the device from booting and immediately shutting down
            // due to the high current draw of the boot process.
            auto voltage = battery->getVoltage();
            if (voltage != 0.0 && voltage < BATTERY_BOOT_THRESHOLD) {
                ESP_LOGW("battery", "Battery voltage too low (%.2f V < %.2f), entering deep sleep\n",
                    voltage, BATTERY_BOOT_THRESHOLD);
                enterLowPowerDeepSleep();
            }

            Task::loop("battery", 1536, [this](Task& task) {
                checkBatteryVoltage(task);
            });
        }

        LOGD("   ______                   _    _       _");
        LOGD("  |  ____|                 | |  | |     | |");
        LOGD("  | |__ __ _ _ __ _ __ ___ | |__| |_   _| |__");
        LOGD("  |  __/ _` | '__| '_ ` _ \\|  __  | | | | '_ \\");
        LOGD("  | | | (_| | |  | | | | | | |  | | |_| | |_) |");
        LOGD("  |_|  \\__,_|_|  |_| |_| |_|_|  |_|\\__,_|_.__/ " FARMHUB_VERSION);
        LOGD("  ");
    }

    void registerShutdownListener(std::function<void()> listener) {
        shutdownListeners.push_back(listener);
    }

    double getBatteryVoltage() {
        return batteryVoltage.getAverage();
    }

    TDeviceDefinition deviceDefinition;
    ConsoleProvider consoleProvider;
    Kernel<TDeviceConfiguration> kernel { deviceDefinition.config, deviceDefinition.mqttConfig, deviceDefinition.statusLed };
    const shared_ptr<BatteryDriver> battery;

private:
#ifdef FARMHUB_DEBUG
    ConsolePrinter consolePrinter { battery };
#endif

    void checkBatteryVoltage(Task& task) {
        task.delayUntil(LOW_POWER_CHECK_INTERVAL);
        auto currentVoltage = battery->getVoltage();
        batteryVoltage.record(currentVoltage);
        auto voltage = batteryVoltage.getAverage();

        if (voltage != 0.0 && voltage < BATTERY_SHUTDOWN_THRESHOLD) {
            LOGI("Battery voltage low (%.2f V < %.2f), starting shutdown process, will go to deep sleep in %lld seconds",
                voltage, BATTERY_SHUTDOWN_THRESHOLD, duration_cast<seconds>(LOW_BATTERY_SHUTDOWN_TIMEOUT).count());

            // TODO Publish all MQTT messages, then shut down WiFi, and _then_ start shutting down peripherals
            //      Doing so would result in less of a power spike, which can be important if the battery is already low

            // Run in separate task to allocate enough stack
            Task::run("shutdown", 8192, [&](Task& task) {
                // Notify all shutdown listeners
                for (auto& listener : shutdownListeners) {
                    listener();
                }
                printf("Shutdown process finished\n");
            });
            task.delay(LOW_BATTERY_SHUTDOWN_TIMEOUT);
            enterLowPowerDeepSleep();
        }
    }

    [[noreturn]] inline void enterLowPowerDeepSleep() {
        printf("Entering low power deep sleep\n");
        ESP.deepSleep(duration_cast<microseconds>(LOW_POWER_SLEEP_CHECK_INTERVAL).count());
        // Signal to the compiler that we are not returning for real
        abort();
    }

    MovingAverage<double> batteryVoltage { 5 };
    std::list<function<void()>> shutdownListeners;

    /**
     * @brief Time to wait between battery checks.
     */
    static constexpr auto LOW_POWER_SLEEP_CHECK_INTERVAL = 10s;

    /**
     * @brief How often we check the battery voltage while in operation.
     *
     * We use a prime number to avoid synchronizing with other tasks.
     */
    static constexpr auto LOW_POWER_CHECK_INTERVAL = 10313ms;

    /**
     * @brief Time to wait for shutdown process to finish before going to deep sleep.
     */
    static constexpr auto LOW_BATTERY_SHUTDOWN_TIMEOUT = 10s;
};

class BatteryTelemetryProvider : public TelemetryProvider {
public:
    BatteryTelemetryProvider(ConfiguredKernel& kernel)
        : kernel(kernel) {
    }

    void populateTelemetry(JsonObject& json) override {
        json["voltage"] = kernel.getBatteryVoltage();
    }

private:
    ConfiguredKernel& kernel;
};

class Device {
public:
    Device() {
        kernel.switches.onReleased("factory-reset", deviceDefinition.bootPin, SwitchMode::PullUp, [this](const Switch&, milliseconds duration) {
            if (duration >= 15s) {
                LOGI("Factory reset triggered after %lld ms", duration.count());
                kernel.performFactoryReset(true);
            } else if (duration >= 5s) {
                LOGI("WiFi reset triggered after %lld ms", duration.count());
                kernel.performFactoryReset(false);
            }
        });

        if (configuredKernel.battery != nullptr) {
            deviceTelemetryCollector.registerProvider("battery", std::make_shared<BatteryTelemetryProvider>(configuredKernel));
            configuredKernel.registerShutdownListener([this]() {
                peripheralManager.shutdown();
            });
            LOGI("Battery configured");
        } else {
            LOGI("No battery configured");
        }

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
        deviceTelemetryCollector.registerProvider("memory", std::make_shared<MemoryTelemetryProvider>());
#endif

        deviceDefinition.registerPeripheralFactories(peripheralManager);

        mqttDeviceRoot->registerCommand(echoCommand);
        mqttDeviceRoot->registerCommand(pingCommand);
        // TODO Add reset-wifi command
        // mqttDeviceRoot->registerCommand(resetWifiCommand);
        mqttDeviceRoot->registerCommand(restartCommand);
        mqttDeviceRoot->registerCommand(sleepCommand);
        mqttDeviceRoot->registerCommand(fileListCommand);
        mqttDeviceRoot->registerCommand(fileReadCommand);
        mqttDeviceRoot->registerCommand(fileWriteCommand);
        mqttDeviceRoot->registerCommand(fileRemoveCommand);
        mqttDeviceRoot->registerCommand(httpUpdateCommand);

        Task::loop("mqtt:log", 3072, [this](Task& task) {
            logRecords.take([&](const LogRecord& record) {
                if (record.level > deviceConfig.publishLogs.get()) {
                    return;
                }
                auto length = record.message.length();
                // Remove the level prefix
                auto messageStart = 2;
                // Remove trailing newline
                auto messageEnd = record.message.charAt(length - 1) == '\n'
                    ? length - 1
                    : length;
                String message = record.message.substring(messageStart, messageEnd);

                mqttDeviceRoot->publish(
                    "log", [&](JsonObject& json) {
                        json["level"] = record.level;
                        json["message"] = message;
                    },
                    MqttDriver::Retention::NoRetain, MqttDriver::QoS::AtLeastOnce, ticks::zero(), MqttDriver::LogPublish::Silent);
            });
        });

        // We want RTC to be in sync before we start setting up peripherals
        kernel.getRtcInSyncState().awaitSet();

        JsonDocument peripheralsInitDoc;
        JsonArray peripheralsInitJson = peripheralsInitDoc.to<JsonArray>();

        auto builtInPeripheralsConfig = deviceDefinition.getBuiltInPeripherals();
        LOGD("Loading configuration for %d built-in peripherals",
            builtInPeripheralsConfig.size());
        for (auto& peripheralConfig : builtInPeripheralsConfig) {
            peripheralManager.createPeripheral(peripheralConfig, peripheralsInitJson);
        }

        auto& peripheralsConfig = deviceConfig.peripherals.get();
        LOGI("Loading configuration for %d user-configured peripherals",
            peripheralsConfig.size());
        bool peripheralError = false;
        for (auto& peripheralConfig : peripheralsConfig) {
            if (!peripheralManager.createPeripheral(peripheralConfig.get(), peripheralsInitJson)) {
                peripheralError = true;
            }
        }

        InitState initState = peripheralError ? InitState::PeripheralError : InitState::Success;

        mqttDeviceRoot->publish(
            "init",
            [&](JsonObject& json) {
                // TODO Remove redundant mentions of "ugly-duckling"
                json["type"] = "ugly-duckling";
                json["model"] = deviceConfig.model.get();
                json["id"] = deviceConfig.id.get();
                json["instance"] = deviceConfig.instance.get();
                json["mac"] = getMacAddress();
                auto device = json["deviceConfig"].to<JsonObject>();
                deviceConfig.store(device, false);
                // TODO Remove redundant mentions of "ugly-duckling"
                json["app"] = "ugly-duckling";
                json["version"] = kernel.version;
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                json["state"] = static_cast<int>(initState);
                json["peripherals"].to<JsonArray>().set(peripheralsInitJson);
                json["sleepWhenIdle"] = kernel.sleepManager.sleepWhenIdle;
            },
            MqttDriver::Retention::NoRetain, MqttDriver::QoS::AtLeastOnce, ticks::max());

        Task::loop("telemetry", 8192, [this](Task& task) {
            publishTelemetry();
            // TODO Configure these telemetry intervals
            // Publishing interval
            const auto interval = 1min;
            // We always wait at least this much between telemetry updates
            const auto debounceInterval = 500ms;
            task.delayUntil(debounceInterval);
            // Allow other tasks to trigger telemetry updates
            telemetryPublishQueue.pollIn(task.ticksUntil(interval - debounceInterval));
        });

        kernel.getKernelReadyState().set();

        LOGI("Device ready in %.2f s (kernel version %s on %s instance '%s' with hostname '%s' and IP '%s', SSID '%s', current time is %lld)",
            millis() / 1000.0,
            kernel.version.c_str(),
            deviceConfig.model.get().c_str(),
            deviceConfig.instance.get().c_str(),
            deviceConfig.getHostname().c_str(),
            kernel.wifi.getIp().value_or("<no-ip>").c_str(),
            kernel.wifi.getSsid().value_or("<no-ssid>").c_str(),
            duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
    }

private:
    enum class InitState {
        Success = 0,
        PeripheralError = 1,
    };

    void publishTelemetry() {
        deviceTelemetryPublisher.publishTelemetry();
        peripheralManager.publishTelemetry();
    }

    String locationPrefix() {
        if (deviceConfig.location.hasValue()) {
            return deviceConfig.location.get() + "/";
        } else {
            return "";
        }
    }

    Queue<LogRecord> logRecords { "logs", 32 };
    ConfiguredKernel configuredKernel { logRecords };
    Kernel<TDeviceConfiguration>& kernel = configuredKernel.kernel;
    TDeviceDefinition& deviceDefinition = configuredKernel.deviceDefinition;
    TDeviceConfiguration& deviceConfig = deviceDefinition.config;

    shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot = kernel.mqtt.forRoot(locationPrefix() + "devices/ugly-duckling/" + deviceConfig.instance.get());
    PeripheralManager peripheralManager { kernel.i2c, deviceDefinition.pcnt, deviceDefinition.pwm, kernel.sleepManager, kernel.switches, mqttDeviceRoot };

    TelemetryCollector deviceTelemetryCollector;
    MqttTelemetryPublisher deviceTelemetryPublisher { mqttDeviceRoot, deviceTelemetryCollector };
    PingCommand pingCommand { [this]() {
        telemetryPublishQueue.offer(true);
    } };

    FileSystem& fs { kernel.fs };
    EchoCommand echoCommand;
    RestartCommand restartCommand;
    SleepCommand sleepCommand;
    FileListCommand fileListCommand { fs };
    FileReadCommand fileReadCommand { fs };
    FileWriteCommand fileWriteCommand { fs };
    FileRemoveCommand fileRemoveCommand { fs };
    HttpUpdateCommand httpUpdateCommand { [this](const String& url) {
        kernel.prepareUpdate(url);
    } };

    CopyQueue<bool> telemetryPublishQueue { "telemetry-publish", 1 };
};

}    // namespace farmhub::devices
