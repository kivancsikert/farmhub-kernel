#pragma once

#include <esp_pm.h>

#include <kernel/Concurrent.hpp>

// FIXME Why do we need to define these manually?
#if CONFIG_IDF_TARGET_ESP32
#error "ESP32 is not supported"
#elif CONFIG_IDF_TARGET_ESP32S2
#define MAX_CPU_FREQ_MHZ CONFIG_ESP32S2_DEFAULT_CPU_FREQ_MHZ
#define MIN_CPU_FREQ_MHZ 80
#elif CONFIG_IDF_TARGET_ESP32S3
#define MAX_CPU_FREQ_MHZ CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ
#define MIN_CPU_FREQ_MHZ 40
#endif

namespace farmhub::kernel {

class PowerManagementLockGuard;

class PowerManager {
public:
    PowerManager(bool requestedSleepWhenIdle)
        : sleepWhenIdle(shouldSleepWhenIdle(requestedSleepWhenIdle)) {

        LOGTV(Tag::PM, "Configuring power management, CPU max/min at %d/%d MHz, light sleep is %s",
            MAX_CPU_FREQ_MHZ, MIN_CPU_FREQ_MHZ, sleepWhenIdle ? "enabled" : "disabled");
        esp_pm_config_t pm_config = {
            .max_freq_mhz = MAX_CPU_FREQ_MHZ,
            .min_freq_mhz = MIN_CPU_FREQ_MHZ,
            .light_sleep_enable = sleepWhenIdle,
        };
        ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

#ifdef CONFIG_PM_LIGHT_SLEEP_CALLBACKS
        esp_pm_sleep_cbs_register_config_t cbs_conf = {
            .enter_cb = nullptr,
            .exit_cb = [](int64_t timeSleptInUs, void* arg) {
                auto self = static_cast<PowerManager*>(arg);
                self->lightSleepTime += microseconds(timeSleptInUs);
                self->lightSleepCount++;
                return ESP_OK;
            },
            .enter_cb_user_arg = nullptr,
            .exit_cb_user_arg = this,
            .enter_cb_prior = 0,
            .exit_cb_prior = 0,
        };
        ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&cbs_conf));
#endif

        //         Task::loop("power-manager", 4096, [this](Task& task) {
        //             esp_pm_dump_locks(stdout);
        // #ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        //             static char buffer[2048];
        //             vTaskGetRunTimeStats(buffer);
        //             printf("Task Name\tState\tPrio\tStack\tNum\n");
        //             printf("%s\n", buffer);
        // #endif
        //             Task::delay(10s);
        //         });
    }
    const bool sleepWhenIdle;

#ifdef CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    milliseconds getLightSleepTime() {
        return duration_cast<milliseconds>(lightSleepTime);
    }

    int getLightSleepCount() {
        return lightSleepCount;
    }
#endif

private:
    static bool shouldSleepWhenIdle(bool requestedSleepWhenIdle) {
        if (requestedSleepWhenIdle) {
#if FARMHUB_DEBUG
            LOGTW(Tag::PM, "Light sleep is disabled in debug mode");
            return false;
#elif WOKWI
            // See https://github.com/wokwi/wokwi-features/issues/922
            LOGTW(Tag::PM, "Light sleep is disabled when running under Wokwi");
            return false;
#elif not(CONFIG_PM_ENABLE)
            LOGTI(Tag::PM, "Power management is disabled because CONFIG_PM_ENABLE is not set");
            return false;
#elif not(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
            LOGTI(Tag::PM, "Light sleep is disabled because CONFIG_FREERTOS_USE_TICKLESS_IDLE is not set");
            return false;
#else
            LOGTI(Tag::PM, "Light sleep is enabled");
            return true;
#endif
        } else {
            LOGTI(Tag::PM, "Light sleep is disabled");
            return false;
        }
    }

#ifdef CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    microseconds lightSleepTime = microseconds::zero();
    int lightSleepCount = 0;
#endif
};

class PowerManagementLock {
public:
    PowerManagementLock(const std::string& name, esp_pm_lock_type_t type) : name(name) {
        ESP_ERROR_CHECK(esp_pm_lock_create(type, 0, name.c_str(), &lock));
    }

    ~PowerManagementLock() {
        ESP_ERROR_CHECK(esp_pm_lock_delete(lock));
    }

    // Delete copy constructor and assignment operator to prevent copying
    PowerManagementLock(const PowerManagementLock&) = delete;
    PowerManagementLock& operator=(const PowerManagementLock&) = delete;

private:
    const std::string name;
    esp_pm_lock_handle_t lock;

    friend class PowerManagementLockGuard;
};

class PowerManagementLockGuard {
public:
    PowerManagementLockGuard(PowerManagementLock& lock)
        : lock(lock) {
        ESP_ERROR_CHECK(esp_pm_lock_acquire(lock.lock));
    }

    ~PowerManagementLockGuard() {
        ESP_ERROR_CHECK(esp_pm_lock_release(lock.lock));
    }

    // Delete copy constructor and assignment operator to prevent copying
    PowerManagementLockGuard(const PowerManagementLockGuard&) = delete;
    PowerManagementLockGuard& operator=(const PowerManagementLockGuard&) = delete;

private:
    PowerManagementLock& lock;
};

}    // namespace farmhub::kernel
