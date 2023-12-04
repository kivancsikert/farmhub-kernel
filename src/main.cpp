#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Application.hpp>
#include <Task.hpp>

using namespace farmhub::device::drivers;

class BlinkLedTask : public LoopTask {
public:
    BlinkLedTask(gpio_num_t ledPin, int interval)
        : LoopTask("Blink LED", 1000, 1)
        , ledPin(ledPin)
        , interval(interval) {
        pinMode(ledPin, OUTPUT);
    }

protected:
    void loop() override {
        digitalWrite(ledPin, HIGH);
        delayUntil(interval);
        digitalWrite(ledPin, LOW);
        delayUntil(interval);
    }

private:
    const gpio_num_t ledPin;
    const int interval;
};

class ConsolePrinter : IntermittentLoopTask {
public:
    ConsolePrinter()
        : IntermittentLoopTask("Console printer", 32768, 1) {
    }

protected:
    int loopAndDelay() override {
        Serial.print("\033[1G\033[0K");
        Serial.print("Uptime: \033[33m" + String(millis()) + "\033[0m ms");
        Serial.print(", wifi: \033[33m" + wifiStatus() + "\033[0m");

        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        Serial.printf(", now: \033[33m%d\033[0m", now);
        Serial.print(&timeinfo, ", local time: \033[33m%A, %B %d %Y %H:%M:%S\033[0m");

        return 100;
    }

private:
    static String wifiStatus() {
        switch (WiFi.status()) {
            case WL_NO_SHIELD:
                return "NO SHIELD";
            case WL_IDLE_STATUS:
                return "IDLE STATUS";
            case WL_NO_SSID_AVAIL:
                return "NO SSID AVAIL";
            case WL_SCAN_COMPLETED:
                return "SCAN COMPLETED";
            case WL_CONNECTED:
                return "CONNECTED";
            case WL_CONNECT_FAILED:
                return "CONNECT FAILED";
            case WL_CONNECTION_LOST:
                return "CONNECTION LOST";
            case WL_DISCONNECTED:
                return "DISCONNECTED";
            default:
                return "UNKNOWN";
        }
    }
};

class BlinkerApplication : public Application {

public:
    BlinkerApplication(const String& hostname)
        : Application(hostname) {
    }

private:
    BlinkLedTask blinkLedTask1 { GPIO_NUM_2, 2500 };
    BlinkLedTask blinkLedTask2 { GPIO_NUM_4, 1500 };
    ConsolePrinter consolePrinter;
};

BlinkerApplication* application;

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up...");

    application = new BlinkerApplication("test-mk6-3");
}

void loop() {
}
