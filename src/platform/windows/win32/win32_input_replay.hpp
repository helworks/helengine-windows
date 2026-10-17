#pragma once
#include <istream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace helengine::windows {
    /// Supplies explicit per-frame keyboard snapshots for isolated capture runs, without sending input to other windows.
    class Win32InputReplay {
    public:
        /// Starts on the ordinary keyboard path with an empty recording.
        Win32InputReplay() = default;
        /// Snapshot pointers belong to this instance and must never be copied to another owner.
        Win32InputReplay(const Win32InputReplay&) = delete;
        /// Prevents assignment from retaining pointers into a different recording's map.
        Win32InputReplay& operator=(const Win32InputReplay&) = delete;
        /// Parses increasing zero-based frame numbers followed by zero to sixteen Win32 key codes; comments start with '#'.
        void Load(std::istream& input) {
            if (Enabled) { throw std::invalid_argument("An input replay cannot be loaded twice."); }
            std::string line;
            int previous = -1;
            while (std::getline(input, line)) {
                if (line.find_first_not_of(" \t\r") == std::string::npos || line[line.find_first_not_of(" \t\r")] == '#') { continue; }
                std::istringstream values(line);
                int frame = -1;
                if (!(values >> frame) || frame <= previous || frame > 1000000 || Events.size() >= 4096) {
                    throw std::invalid_argument("Replay frames must increase within [0,1000000], with at most 4096 snapshots.");
                }
                std::vector<int> keys;
                int key = 0;
                while (values >> key) {
                    if (key < 1 || key > 255 || keys.size() >= 16) { throw std::invalid_argument("Replay keys must be Win32 codes 1..255, with at most 16 held keys."); }
                    for (int held : keys) { if (held == key) { throw std::invalid_argument("Replay keys must not repeat within a snapshot."); } }
                    keys.push_back(key);
                }
                if (!values.eof()) { throw std::invalid_argument("Replay snapshots contain an invalid key token."); }
                Events.emplace(frame, std::move(keys));
                previous = frame;
            }
            if (input.bad() || Events.empty()) { throw std::invalid_argument("Input replay must contain at least one readable snapshot."); }
            Enabled = true;
        }

        /// Indicates whether an explicitly supplied recording owns keyboard input in this process.
        bool IsEnabled() const { return Enabled; }

        /// Advances one input capture, retaining held keys until the next snapshot and allocating nothing during playback.
        const std::vector<int>& NextKeys() {
            auto next = Events.find(Frame++);
            if (next != Events.end()) { Current = &next->second; }
            return *Current;
        }

    private:
        /// Bounded snapshots owned only by a configured desktop capture process.
        std::map<int, std::vector<int>> Events;
        /// Empty keyboard used before the first explicitly scheduled snapshot.
        const std::vector<int> Empty;
        /// Borrowed snapshot within the stable event map, avoiding per-frame key copies.
        const std::vector<int>* Current = &Empty;
        /// Zero-based count of captured input frames.
        int Frame = 0;
        /// Keeps the ordinary keyboard path active unless a recording was explicitly loaded.
        bool Enabled = false;
    };
}
