/*
 * fpp-DMXBridge — set individual DMX channels from FPP command presets.
 *
 * Commands registered:
 *   "DMX Bridge - Set Channel"   args: Channel (1–65535), Value (0–255)
 *   "DMX Bridge - Clear All"     no args — zeroes every active override
 *
 * Overrides are applied via modifyChannelData() only while no playlist or
 * sequence is playing.  Starting a sequence hands full control back to it.
 * All overrides start at zero when the plugin loads.
 */

// jsoncpp must come before FPP headers (Plugin.h expects it already present)
#if __has_include(<jsoncpp/json/json.h>)
#include <jsoncpp/json/json.h>
#elif __has_include(<json/json.h>)
#include <json/json.h>
#endif

// FPP plugin API — commands/Commands.h declares CommandManager too
#include <Plugin.h>
#include <commands/Commands.h>
#include <log.h>

// Standard library
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Plugin channel limit: covers 128 universes × 512 channels.
// Increase if needed; must not exceed FPPD_MAX_CHANNELS (8M).
// ---------------------------------------------------------------------------
static constexpr int DMX_MAX_CHANNEL = 65536;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class DMXBridgePlugin;

// ---------------------------------------------------------------------------
// "Set Channel" command
// ---------------------------------------------------------------------------
class DMXSetChannelCommand : public Command {
public:
    explicit DMXSetChannelCommand(DMXBridgePlugin* plugin)
        : Command("DMX Bridge - Set Channel",
                  "Set a single DMX channel to a value (0-255). "
                  "Active only while no sequence/playlist is playing."),
          m_plugin(plugin) {
        args.emplace_back("Channel", "int", "Channel number (1-65535)");
        args.back().setRange(1, DMX_MAX_CHANNEL - 1);
        args.emplace_back("Value", "int", "Value (0-255)");
        args.back().setRange(0, 255);
    }

    std::unique_ptr<Result> run(const std::vector<std::string>& a) override;

private:
    DMXBridgePlugin* m_plugin;
};

// ---------------------------------------------------------------------------
// "Clear All" command
// ---------------------------------------------------------------------------
class DMXClearAllCommand : public Command {
public:
    explicit DMXClearAllCommand(DMXBridgePlugin* plugin)
        : Command("DMX Bridge - Clear All",
                  "Zero all DMX channel overrides set by this plugin."),
          m_plugin(plugin) {}

    std::unique_ptr<Result> run(const std::vector<std::string>& a) override;

private:
    DMXBridgePlugin* m_plugin;
};

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------
class DMXBridgePlugin : public FPPPlugin {
public:
    DMXBridgePlugin()
        : FPPPlugin("DMXBridge") {
        LogInfo(VB_PLUGIN, "DMXBridge: initialising\n");
        registerCommands();
    }

    ~DMXBridgePlugin() {
        unregisterCommands();
        LogInfo(VB_PLUGIN, "DMXBridge: shutdown\n");
    }

    // -----------------------------------------------------------------------
    // Channel data hook — called every output frame, after overlays.
    // Apply overrides only while no sequence is playing.
    // -----------------------------------------------------------------------
    void modifyChannelData(int ms, uint8_t* seqData) override {
        if (m_sequencePlaying.load(std::memory_order_relaxed))
            return;
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& [ch, val] : m_overrides)
            seqData[ch - 1] = val;   // ch is 1-based; seqData is 0-indexed
    }

    // -----------------------------------------------------------------------
    // Playlist hook — track whether a show is running.
    // -----------------------------------------------------------------------
    void playlistCallback(const Json::Value& playlist,
                          const std::string& action,
                          const std::string& section,
                          int item) override {
        if (action == "start" || action == "playing") {
            m_sequencePlaying.store(true, std::memory_order_relaxed);
        } else if (action == "stop") {
            m_sequencePlaying.store(false, std::memory_order_relaxed);
        }
    }

    // -----------------------------------------------------------------------
    // Called by commands
    // -----------------------------------------------------------------------
    void setChannel(int channel, uint8_t value) {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_overrides[channel] = value;
        }
        LogInfo(VB_PLUGIN, "DMXBridge: ch %d = %u\n", channel, (unsigned)value);
    }

    void clearAll() {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_overrides.clear();
        }
        LogInfo(VB_PLUGIN, "DMXBridge: cleared all channel overrides\n");
    }

private:
    std::atomic<bool>        m_sequencePlaying{false};
    std::mutex               m_mutex;
    std::map<int, uint8_t>   m_overrides;        // channel (1-based) → value
    std::vector<std::string> m_registeredCommands;

    void registerCommands() {
        auto* setCmd   = new DMXSetChannelCommand(this);
        auto* clearCmd = new DMXClearAllCommand(this);
        CommandManager::INSTANCE.addCommand(setCmd);
        CommandManager::INSTANCE.addCommand(clearCmd);
        m_registeredCommands = {setCmd->name, clearCmd->name};
        LogInfo(VB_PLUGIN, "DMXBridge: registered %zu commands\n",
                m_registeredCommands.size());
    }

    void unregisterCommands() {
        for (const auto& n : m_registeredCommands)
            CommandManager::INSTANCE.removeCommand(n);
        m_registeredCommands.clear();
    }
};

// ---------------------------------------------------------------------------
// Command run() implementations (need full DMXBridgePlugin definition)
// ---------------------------------------------------------------------------
std::unique_ptr<Command::Result>
DMXSetChannelCommand::run(const std::vector<std::string>& a) {
    if (a.size() < 2)
        return std::make_unique<ErrorResult>("Usage: channel value");
    try {
        int ch  = std::stoi(a[0]);
        int val = std::stoi(a[1]);
        if (ch < 1 || ch >= DMX_MAX_CHANNEL)
            return std::make_unique<ErrorResult>("Channel out of range (1-65535)");
        if (val < 0 || val > 255)
            return std::make_unique<ErrorResult>("Value out of range (0-255)");
        m_plugin->setChannel(ch, static_cast<uint8_t>(val));
        return std::make_unique<Result>("OK");
    } catch (...) {
        return std::make_unique<ErrorResult>("Invalid arguments");
    }
}

std::unique_ptr<Command::Result>
DMXClearAllCommand::run(const std::vector<std::string>& a) {
    m_plugin->clearAll();
    return std::make_unique<Result>("OK");
}

// ---------------------------------------------------------------------------
// Plugin entry point — must match FPPPlugins::Plugin* (*)() signature
// ---------------------------------------------------------------------------
extern "C" {

FPPPlugins::Plugin* createPlugin() {
    return new DMXBridgePlugin();
}

} // extern "C"
