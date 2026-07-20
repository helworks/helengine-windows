#pragma once

#include <Windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AudioAsset.hpp"
#include "AudioPlaybackRequest.hpp"
#include "IAudioBackend.hpp"

namespace helengine::windows {
    /// Implements the generated audio backend contract over native Windows wave-out playback.
    class Win32AudioBackend final : public IAudioBackend {
    public:
        /// Creates one empty Windows audio backend with default bus state.
        Win32AudioBackend();

        /// Stops and releases every active native playback voice.
        ~Win32AudioBackend();

        /// Returns whether one backend voice is still active.
        bool IsPlaying(int32_t voiceId) override;

        /// Starts playback of one PCM audio asset and returns its backend-owned voice identifier.
        int32_t Play(::AudioAsset* asset, ::AudioPlaybackRequest* request) override;

        /// Applies one linear gain value to a named mixer bus.
        void SetBusGain(std::string busId, float gain) override;

        /// Pauses or resumes one named mixer bus.
        void SetBusPaused(std::string busId, bool paused) override;

        /// Stops one active backend voice.
        void Stop(int32_t voiceId) override;

        /// Advances backend-owned maintenance work such as loop restarts and completed-voice cleanup.
        void Update() override;

    private:
        /// Stores one active wave-out voice and its routing state.
        struct VoiceState {
            HWAVEOUT DeviceHandle;
            WAVEHDR Header;
            std::vector<std::uint8_t> EncodedBytes;
            std::string BusId;
            float BaseGain;
            bool Loop;
            bool Queued;
            bool Paused;
            bool Completed;

            VoiceState();
        };

        /// Receives wave-out callbacks and marks completed voices for the next backend update tick.
        static void CALLBACK HandleWaveOutCallback(
            HWAVEOUT waveOutHandle,
            UINT message,
            DWORD_PTR instance,
            DWORD_PTR param1,
            DWORD_PTR param2);

        /// Marks one voice as completed after the wave-out device signals buffer completion.
        void MarkVoiceCompleted(int32_t voiceId);

        /// Normalizes one bus identifier into the backend's case-insensitive map key format.
        static std::string NormalizeBusId(const std::string& busId);

        /// Clamps one runtime gain value into the backend-supported linear gain range.
        static float ClampGain(float gain);

        /// Builds the PCM wave format used to open one voice device for the supplied asset.
        static WAVEFORMATEX BuildWaveFormat(AudioAsset& asset);

        /// Applies the current bus volume and paused state to one voice.
        void ApplyVoiceState(VoiceState& voice);

        /// Applies the current bus volume to one voice.
        void ApplyVoiceVolume(VoiceState& voice);

        /// Applies the current bus paused state to one voice.
        void ApplyVoicePlaybackState(VoiceState& voice);

        /// Restarts one completed looping voice and returns whether the restart succeeded.
        bool RestartVoice(int32_t voiceId);

        /// Stops the supplied voice and releases its native resources.
        void DisposeVoice(VoiceState& voice);

        /// Tracks active voices by backend-owned voice identifier.
        std::unordered_map<int32_t, VoiceState> VoicesById;

        /// Tracks configured linear gains by normalized bus identifier.
        std::unordered_map<std::string, float> BusGainsById;

        /// Tracks paused-state flags by normalized bus identifier.
        std::unordered_map<std::string, bool> BusPausedById;

        /// Serializes access to active voice state and callback-driven completion changes.
        std::mutex VoicesMutex;

        /// Stores the next backend-owned voice identifier.
        int32_t NextVoiceId;
    };
}
