/*
 * fpp-DMXBridge — set individual DMX channels from FPP command presets.
 *
 * Commands registered:
 *   "DMX Bridge - Set Channel"   args: Channel (1–512), Value (0–255)
 *   "DMX Bridge - Clear All"     no args — zeroes every active override
 *
 * Priority (highest to lowest):
 *   1. Running sequence — always wins
 *   2. FPP Display Testing (channel tester) — overrides the plugin
 *   3. This plugin's active overrides
 *
 * Overrides are written in modifySequenceData(), which runs BEFORE the
 * channel tester applies its values.  The tester therefore overwrites
 * anything we wrote, and sequence data (loaded before modifySequenceData
 * is called) is preserved when IsSequenceRunning() is true.
 *
 * EnableChannelOutput() + StartForcingChannelOutput() are called when the
 * first override is set so the output thread keeps transmitting even with
 * no active sequence.  StopForcingChannelOutput() is called on clearAll().
 */

// jsoncpp must come before FPP headers (Plugin.h expects it already present)
#if __has_include(<jsoncpp/json/json.h>)
#include <jsoncpp/json/json.h>
#elif __has_include(<json/json.h>)
#include <json/json.h>
#endif

// FPP plugin API
#include <Plugin.h>
#include <commands/Commands.h>
#include <log.h>

// Channel output control and sequence state
#include <channeloutput/channeloutputthread.h>
#include <Sequence.h>

// Standard library
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DMX universe limit: one universe, 512 channels.
// ---------------------------------------------------------------------------
static constexpr int DMX_MAX_CHANNEL = 512;

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
                  "Channel 1-512 (one DMX universe). "
                  "Yields to running sequences and Display Testing."),
          m_plugin(plugin) {
        args.emplace_back("Channel", "int", "Channel number (1-512)");
        args.back().setRange(1, DMX_MAX_CHANNEL);
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
        if (m_forcingOutput.exchange(false))
            StopForcingChannelOutput();
        unregisterCommands();
        LogInfo(VB_PLUGIN, "DMXBridge: shutdown\n");
    }

    // -----------------------------------------------------------------------
    // Called after sequence data is loaded, BEFORE the channel tester runs.
    //
    // We apply overrides here so that:
    //   - Sequence data (already in seqData) wins when a sequence is playing
    //     (we return early via IsSequenceRunning).
    //   - The channel tester overwrites our values for channels it's testing,
    //     since it runs after this hook.
    //
    // Stale-buffer fix: readFrame() only writes channels present in the FSEQ
    // file.  Channels outside the sequence's range keep whatever value was in
    // m_seqData from the previous non-sequence frame — which may include our
    // override.  On the first frame of a new sequence we explicitly zero every
    // override channel so those channels start clean regardless of whether the
    // sequence file addresses them.
    // -----------------------------------------------------------------------
    void modifySequenceData(int ms, uint8_t* seqData) override {
        bool seqRunning = sequence && sequence->IsSequenceRunning();

        if (seqRunning) {
            if (!m_prevSeqRunning) {
                // Transition: idle → sequence.  Zero our channels so stale
                // override values don't bleed into the new sequence's first frame.
                std::lock_guard<std::mutex> lk(m_mutex);
                for (const auto& [ch, val] : m_overrides)
                    seqData[ch - 1] = 0;
            }
            m_prevSeqRunning = true;
            return;
        }

        m_prevSeqRunning = false;
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& [ch, val] : m_overrides)
            seqData[ch - 1] = val;   // ch is 1-based; seqData is 0-indexed
    }

    // -----------------------------------------------------------------------
    // Called by commands
    // -----------------------------------------------------------------------
    void setChannel(int channel, uint8_t value) {
        bool wasEmpty;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            wasEmpty = m_overrides.empty();
            m_overrides[channel] = value;
        }
        // First override: enable and force the output thread to keep running
        // so our values are transmitted even with no sequence playing.
        if (wasEmpty && !m_forcingOutput.exchange(true)) {
            EnableChannelOutput();
            StartForcingChannelOutput();
        }
        LogInfo(VB_PLUGIN, "DMXBridge: ch %d = %u\n", channel, (unsigned)value);
    }

    void clearAll() {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_overrides.clear();
        }
        if (m_forcingOutput.exchange(false))
            StopForcingChannelOutput();
        LogInfo(VB_PLUGIN, "DMXBridge: cleared all channel overrides\n");
    }

private:
    std::atomic<bool>        m_forcingOutput{false};
    bool                     m_prevSeqRunning{false};  // output-thread only, no sync needed
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
        if (ch < 1 || ch > DMX_MAX_CHANNEL)
            return std::make_unique<ErrorResult>("Channel out of range (1-512)");
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
