#pragma once

#include <opm/media/audio_output.h>
#include <opm/media/decoder.h>
#include <opm/media/renderer.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef ENABLE_AIRPLAY
#include <opm/airplay/airplay_server.h>
#endif
#ifdef ENABLE_MIRACAST
#include <opm/miracast/miracast_receiver.h>
#endif
#ifdef ENABLE_CAST
#include <opm/cast/cast_receiver.h>
#endif
#ifdef ENABLE_ANDROID
#include <opm/android/scrcpy_receiver.h>
#endif

namespace opm {

class App {
public:
    App();
    ~App();

    struct Config {
        std::string name = "1PhoneMirror";
        int window_width = 1280;
        int window_height = 720;
        bool enable_airplay = true;
        bool enable_miracast = true;
        bool enable_cast = true;
        bool enable_android = true;
        bool airplay_require_pin = false;
        bool cleanup_stale_instances = false;
        bool setup_firewall = false;
        bool cleanup_adb_on_shutdown = false;
        bool force_exit_on_timeout = false;

        // Android (scrcpy) — auto-detected at startup if these are set.
        std::string android_adb_path;          // path to adb.exe (empty = PATH)
        std::string android_scrcpy_jar;        // path to scrcpy-server.jar
        std::string android_device_serial;     // empty = auto-pick first
    };

    bool init(const Config& config);

    // Run the main loop (blocks until quit)
    int run();

    void shutdown();

#ifdef ENABLE_ANDROID
    // Pair via (ip, pair_port, pin) and start mirroring once the device
    // appears via mDNS auto-discovery. If `connect_port` is non-empty,
    // skips mDNS and connects directly to <ip>:<connect_port> (the value
    // shown on the phone's main Wireless debugging screen).
    // Returns a human-readable status.
    std::string android_pair_and_connect(const std::string& ip,
                                         const std::string& pair_port,
                                         const std::string& pin,
                                         const std::string& connect_port = "");
    // Forwards a mouse/keyboard event from the window to the active phone.
    void android_send_input(const media::Renderer::AndroidInput& in);

    // Mirror a phone plugged in with a USB cable (USB debugging on). Picks
    // the first USB device that is not already mirrored. Returns
    // "Mirroring <serial>" on success, otherwise a human-readable reason.
    std::string android_connect_usb();

    // QR-code pairing (Wireless debugging -> "Pair device with QR code").
    // The caller shows a QR encoding `WIFI:T:ADB;S:<name>;P:<password>;;`.
    // Once the phone scans it, it advertises an `_adb-tls-pairing._tcp`
    // mDNS service whose instance name is `name`; we `adb pair` to it with
    // `password`, then connect and start mirroring. Blocks (poll every
    // 500 ms, 3 min max) until paired, cancelled or timed out. Run it on a
    // worker thread. Returns "Mirroring <serial>" on success.
    std::string android_pair_via_qr(const std::string& name,
                                    const std::string& password,
                                    std::function<bool()> cancelled);
    // Disconnect a single Android session by serial ("ip:port"), or all
    // sessions if `serial` is empty.
    void android_disconnect(const std::string& serial = {});

    // One ScrcpyReceiver per connected device. Public so the helper in
    // src/android/android_dialog.cpp can construct/own them.
    struct AndroidSession {
        std::string                                   serial;
        std::string                                   model;
        std::unique_ptr<android::ScrcpyReceiver>      receiver;
    };
#endif

private:
    Config config_;

    media::Renderer renderer_;
    media::AudioOutput audio_;

    // Source priority: only one protocol streams at a time
    enum class Source { None, AirPlay, Miracast, Cast, Android };
    std::atomic<int> active_source_{static_cast<int>(Source::None)};

    // Connection order across kinds. Each entry is either an AirPlay
    // device id (e.g. "192.168.10.5") or the literal "android".
    // Used to render the bottom-bezel source dots in the order devices
    // actually connected.
    std::mutex                source_order_mutex_;
    std::vector<std::string>  source_order_;

#ifdef ENABLE_AIRPLAY
    airplay::AirPlayServer airplay_;
    // Tracks which AirPlay source ids we've already logged a usage-log
    // session_start for, so set_sources_callback() (which fires with the
    // FULL current list on every add/remove) can diff and log exactly one
    // session_start/session_end per iOS device — including when several
    // iPhones are connected at once. Keyed by source id, value is the
    // display name used at session_end time (server no longer has it once
    // the source is gone).
    std::mutex                          airplay_sources_mutex_;
    std::map<std::string, std::string>  airplay_logged_sources_;
#endif
#ifdef ENABLE_MIRACAST
    miracast::MiracastReceiver miracast_;
#endif
#ifdef ENABLE_CAST
    cast::CastReceiver cast_;
#endif
#ifdef ENABLE_ANDROID
    android::AdbController   adb_;
    // One ScrcpyReceiver per connected device. Each session owns its own
    // decoder, worker thread and reverse-tunnel local port, so multiple
    // Android phones can mirror simultaneously and the user picks which
    // one is on screen via the source dots in the lower bezel.
    std::mutex                                        scrcpy_mutex_;
    std::vector<std::unique_ptr<AndroidSession>>      scrcpy_sessions_;
    std::string                                       active_android_serial_;
    std::string android_jar_path_;

    // Implemented in src/android/android_dialog.cpp.
    bool start_android_session_(const std::string& serial,
                                std::string* out_error);
    // After a successful `adb pair`: find the phone's connect endpoint via
    // mDNS / `adb devices`, connect and start mirroring.
    std::string android_finish_connect_(const std::string& ip,
                                        const std::string& pair_addr);
    // Tear down any running session whose serial starts with `ip + ":"`.
    void android_drop_sessions_for_ip_(const std::string& ip);
#endif

    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_done_{false};
};

} // namespace opm
