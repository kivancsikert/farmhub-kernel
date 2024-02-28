#pragma once

#include <chrono>
#include <memory>

#include <Arduino.h>
#include <esp_pm.h>

#ifndef FARMHUB_LOG_LEVEL
#ifdef FARMHUB_DEBUG
#define FARMHUB_LOG_LEVEL LOG_LEVEL_VERBOSE
#else
#define FARMHUB_LOG_LEVEL LOG_LEVEL_INFO
#endif
#endif

#include <Print.h>

#include <ArduinoLog.h>

#include <kernel/Command.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;
using std::shared_ptr;
using namespace farmhub::kernel;

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
typedef farmhub::devices::UglyDucklingMk4 TDeviceDefinition;
typedef farmhub::devices::Mk4Config TDeviceConfiguration;

#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
typedef farmhub::devices::UglyDucklingMk5 TDeviceDefinition;
typedef farmhub::devices::Mk5Config TDeviceConfiguration;
#define HAS_BATTERY

#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
typedef farmhub::devices::UglyDucklingMk6 TDeviceDefinition;
typedef farmhub::devices::Mk6Config TDeviceConfiguration;
#define HAS_BATTERY

#else
#error "No device defined"
#endif

// FIXME Why do we need to define these manually?
#if CONFIG_IDF_TARGET_ESP32
typedef esp_pm_config_esp32_t esp_pm_config_t;
#define DEFAULT_CPU_FREQ_MHZ CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ
#elif CONFIG_IDF_TARGET_ESP32S2
typedef esp_pm_config_esp32s2_t esp_pm_config_t;
#define DEFAULT_CPU_FREQ_MHZ CONFIG_ESP32S2_DEFAULT_CPU_FREQ_MHZ
#elif CONFIG_IDF_TARGET_ESP32S3
typedef esp_pm_config_esp32s3_t esp_pm_config_t;
#define DEFAULT_CPU_FREQ_MHZ CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ
#endif

namespace farmhub::devices {

#ifdef FARMHUB_DEBUG
class ConsolePrinter : public Print {
public:
    ConsolePrinter() {
        static const String spinner = "|/-\\";
        static const int spinnerLength = spinner.length();
        Task::loop("console", 3072, 1, [this](Task& task) {
            String status;

            counter = (counter + 1) % spinnerLength;
            status += "[" + spinner.substring(counter, counter + 1) + "] ";

            status += "\033[33m" + String(VERSION) + "\033[0m";

            status += ", IP: \033[33m" + WiFi.localIP().toString() + "\033[0m";
            status += "/" + wifiStatus();

            status += ", uptime: \033[33m" + String(float(millis()) / 1000.0f, 1) + "\033[0m s";
            time_t now;
            struct tm timeinfo;
            time(&now);
            localtime_r(&now, &timeinfo);

            char buffer[64];    // Ensure buffer is large enough for the formatted string
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
            status += ", UTC: \033[33m" + String(buffer) + "\033[0m";

            status += ", heap: \033[33m" + String(float(ESP.getFreeHeap()) / 1024.0f, 2) + "\033[0m kB";

            BatteryDriver* battery = this->battery.load();
            if (battery != nullptr) {
                status += ", battery: \033[33m" + String(battery->getVoltage(), 2) + "\033[0m V";
            }

            Serial.print("\033[1G\033[0K");

            consoleQueue.drain([](String line) {
                Serial.println(line);
            });

            Serial.print(status);
            task.delayUntil(milliseconds(100));
        });
    }

    void registerBattery(BatteryDriver& battery) {
        this->battery = &battery;
    }

    size_t write(uint8_t character) override {
        Task::consoleBuffer()->concat((char) character);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        Task::consoleBuffer()->concat(buffer, size);
        return size;
    }

    void printLine(int level) {
        String* buffer = Task::consoleBuffer();
        if (buffer->isEmpty()) {
            return;
        }
        size_t pos = (buffer->startsWith("\r") || buffer->startsWith("\n")) ? 1 : 0;
        size_t len = buffer->endsWith("\n") ? buffer->length() - 1 : buffer->length();
        String copy = "[\033[0;31m" + String(pcTaskGetName(nullptr)) + "\033[0m/\033[0;32m" + String(xPortGetCoreID()) + "\033[0m] " + buffer->substring(pos, pos + len);
        buffer->clear();
        if (!consoleQueue.offer(copy)) {
            Serial.println(copy);
        }
    }

private:
    static String wifiStatus() {
        switch (WiFi.status()) {
            case WL_NO_SHIELD:
                return "\033[0;31mno shield\033[0m";
            case WL_IDLE_STATUS:
                return "\033[0;33midle\033[0m";
            case WL_NO_SSID_AVAIL:
                return "\033[0;31mno SSID\033[0m";
            case WL_SCAN_COMPLETED:
                return "\033[0;33mscan completed\033[0m";
            case WL_CONNECTED:
                return "\033[0;32mOK\033[0m";
            case WL_CONNECT_FAILED:
                return "\033[0;31mfailed\033[0m";
            case WL_CONNECTION_LOST:
                return "\033[0;31mconnection lost\033[0m";
            case WL_DISCONNECTED:
                return "\033[0;33mdisconnected\033[0m";
            default:
                return "\033[0;31munknown\033[0m";
        }
    }

    int counter;
    std::atomic<BatteryDriver*> battery { nullptr };

    Queue<String> consoleQueue { "console", 128 };
};

ConsolePrinter consolePrinter;

void printLogLine(Print* printer, int level) {
    consolePrinter.printLine(level);
}
#endif

class ConsoleProvider {
public:
    ConsoleProvider() {
        Serial.begin(115200);
#ifdef FARMHUB_DEBUG
        Log.begin(FARMHUB_LOG_LEVEL, &consolePrinter);
        Log.setSuffix(printLogLine);
#else
        Log.begin(FARMHUB_LOG_LEVEL, &Serial);
#endif
        Log.infoln(F("  ______                   _    _       _"));
        Log.infoln(F(" |  ____|                 | |  | |     | |"));
        Log.infoln(F(" | |__ __ _ _ __ _ __ ___ | |__| |_   _| |__"));
        Log.infoln(F(" |  __/ _` | '__| '_ ` _ \\|  __  | | | | '_ \\"));
        Log.infoln(F(" | | | (_| | |  | | | | | | |  | | |_| | |_) |"));
        Log.infoln(F(" |_|  \\__,_|_|  |_| |_| |_|_|  |_|\\__,_|_.__/ %s"), VERSION);
        Log.infoln("");
    }
};

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

class ConfiguredKernel : ConsoleProvider {
public:
    TDeviceDefinition deviceDefinition;
    Kernel<TDeviceConfiguration> kernel { deviceDefinition.config, deviceDefinition.mqttConfig, deviceDefinition.statusLed };
};

class Device {
public:
    Device() {


#ifdef HAS_BATTERY

#ifdef FARMHUB_DEBUG

        consolePrinter.registerBattery(deviceDefinition.batteryDriver);
#endif
        deviceTelemetryCollector.registerProvider("battery", deviceDefinition.batteryDriver);
#endif

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
        deviceTelemetryCollector.registerProvider("memory", memoryTelemetryProvider);
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

        // We want RTC to be in sync before we start setting up peripherals
        kernel.getRtcInSyncState().awaitSet();

        auto builtInPeripheralsCofig = deviceDefinition.getBuiltInPeripherals();
        Log.traceln("Loading configuration for %d built-in peripherals",
            builtInPeripheralsCofig.size());
        for (auto& perpheralConfig : builtInPeripheralsCofig) {
            peripheralManager.createPeripheral(perpheralConfig);
        }

        auto& peripheralsConfig = deviceConfig.peripherals.get();
        Log.infoln("Loading configuration for %d user-configured peripherals",
            peripheralsConfig.size());
        for (auto& perpheralConfig : peripheralsConfig) {
            peripheralManager.createPeripheral(perpheralConfig.get());
        }

        bool sleepWhenIdle = enableSleepWhenIdle();

        mqttDeviceRoot->publish(
            "init",
            [&](JsonObject& json) {
                // TODO Remove redundant mentions of "ugly-duckling"
                json["type"] = "ugly-duckling";
                json["model"] = deviceConfig.model.get();
                json["instance"] = deviceConfig.instance.get();
                json["mac"] = getMacAddress();
                auto device = json.createNestedObject("deviceConfig");
                deviceConfig.store(device, false);
                // TODO Remove redundant mentions of "ugly-duckling"
                json["app"] = "ugly-duckling";
                json["version"] = kernel.version;
                json["wakeup"] = esp_sleep_get_wakeup_cause();
                json["bootCount"] = bootCount++;
                json["time"] = time(nullptr);
                json["sleepWhenIdle"] = sleepWhenIdle;
            },
            MqttDriver::Retention::NoRetain, MqttDriver::QoS::AtLeastOnce, ticks::max(), 8192);

        Task::loop("telemetry", 8192, [this](Task& task) {
            publishTelemetry();
            // TODO Configure telemetry heartbeat interval
            task.delayUntil(milliseconds(60000));
        });

        kernel.getKernelReadyState().set();
    }

private:
    void publishTelemetry() {
        deviceTelemetryPublisher.publishTelemetry();
        peripheralManager.publishTelemetry();
    }

    bool enableSleepWhenIdle() {
        if (deviceConfig.sleepWhenIdle.get()) {
#if FARMHUB_DEBUG
            Log.warningln("Light sleep is disabled in debug mode");
            return false;
#else    // FARMHUB_DEBUG
#if not(CONFIG_PM_ENABLE)
            Log.warningln("Light sleep is disabled because CONFIG_PM_ENABLE is not set");
            return false;
#else    // CONFIG_PM_ENABLE

#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            Log.infoln("Light sleep is enabled");
            bool sleepWhenIdle = true;
#else
            Log.warningln("Light sleep is disabled because CONFIG_FREERTOS_USE_TICKLESS_IDLE is not set");
            bool sleepWhenIdle = false;
#endif    // CONFIG_FREERTOS_USE_TICKLESS_IDLE

            // Configure dynamic frequency scaling:
            // maximum and minimum frequencies are set in sdkconfig,
            // automatic light sleep is enabled if tickless idle support is enabled.
            esp_pm_config_t pm_config = {
                .max_freq_mhz = 240,
                .min_freq_mhz = 40,
                .light_sleep_enable = sleepWhenIdle
            };
            ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
            return sleepWhenIdle;
#endif    // CONFIG_PM_ENABLE
#endif    // FARMHUB_DEBUG
        } else {
            Log.infoln("Light sleep is disabled");
            return false;
        }
    }

    String locationPrefix() {
        if (deviceConfig.location.hasValue()) {
            return deviceConfig.location.get() + "/";
        } else {
            return "";
        }
    }

    ConfiguredKernel configuredKernel;
    Kernel<TDeviceConfiguration>& kernel = configuredKernel.kernel;
    TDeviceDefinition& deviceDefinition = configuredKernel.deviceDefinition;
    TDeviceConfiguration& deviceConfig = deviceDefinition.config;

    shared_ptr<MqttDriver::MqttRoot> mqttDeviceRoot = kernel.mqtt.forRoot(locationPrefix() + "devices/ugly-duckling/" + deviceConfig.instance.get());
    PeripheralManager peripheralManager { mqttDeviceRoot };

    TelemetryCollector deviceTelemetryCollector;
    MqttTelemetryPublisher deviceTelemetryPublisher { mqttDeviceRoot, deviceTelemetryCollector };
    PingCommand pingCommand { [this]() {
        publishTelemetry();
    } };

#if defined(FARMHUB_DEBUG) || defined(FARMHUB_REPORT_MEMORY)
    MemoryTelemetryProvider memoryTelemetryProvider;
#endif

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
};

}    // namespace farmhub::devices
