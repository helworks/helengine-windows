#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
namespace helengine::windows {
/// <summary>Shares exact immutable payloads and retires their keys when the final material owner releases them.</summary>
template<class TResource> class DirectX11ConstantDataPool final {
    /// <summary>Outlives the pool facade while resources remain; final-owner retirement erases weak entries and breaks retained bookkeeping.</summary>
    struct State {
        /// <summary>Indexes exact payload bytes without extending GPU resource lifetime.</summary>
        std::unordered_map<std::string, std::weak_ptr<const TResource>> Resources;
        /// <summary>Counts logical live payload bytes, excluding driver overhead.</summary>
        size_t RetainedBytes = 0;
    };
    /// <summary>Keeps retirement bookkeeping alive until the last outstanding resource owner exits.</summary>
    std::shared_ptr<State> Shared = std::make_shared<State>();

    /// <summary>Removes only the retiring generation; a newer live entry after Clear must remain intact.</summary>
    static void Retire(const std::shared_ptr<State>& state, const std::string& key, const TResource* resource) {
        const auto found = state->Resources.find(key);
        if (found != state->Resources.end() && found->second.expired()) {
            state->RetainedBytes -= key.size();
            state->Resources.erase(found);
        }
        delete resource;
    }
public:
    /// <summary>Reuses exact contents or publishes a newly created resource without scanning unrelated entries.</summary>
    template<class TFactory> std::shared_ptr<const TResource> Get(std::span<const uint8_t> bytes, TFactory&& factory) {
        if (bytes.empty() || bytes.size() % 16 || bytes.size() > 65536) {
            throw std::invalid_argument("DirectX11 material constants require 16-byte alignment and at most 64 KiB.");
        }
        std::string key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto found = Shared->Resources.find(key);
        if (found != Shared->Resources.end()) {
            if (auto resource = found->second.lock()) return resource;
        }
        auto pending = std::make_unique<const TResource>(factory());
        auto retirement = [state = Shared, key](const TResource* resource) { Retire(state, key, resource); };
        std::shared_ptr<const TResource> resource(pending.release(), std::move(retirement));
        Shared->Resources.emplace(std::move(key), resource);
        Shared->RetainedBytes += bytes.size();
        return resource;
    }
    /// <summary>Forgets cached identities while outstanding caller-owned resources retain their own lifetime.</summary>
    void Clear() {
        Shared->Resources.clear();
        Shared->RetainedBytes = 0;
    }
    /// <summary>Reports the exact number of currently indexed live payloads.</summary>
    size_t GetCount() const { return Shared->Resources.size(); }
    /// <summary>Reports exact logical bytes indexed by the pool.</summary>
    size_t GetRetainedBytes() const { return Shared->RetainedBytes; }
};
}
