#include "platform/windows/win32/win32_audio_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <utility>

namespace helengine::windows {
    /// Initializes one empty voice state container.
    Win32AudioBackend::VoiceState::VoiceState()
        : DeviceHandle(nullptr),
          Header(),
          EncodedBytes(),
          BusId("master"),
          BaseGain(1.0f),
          Loop(false),
          Queued(false),
          Paused(false),
          Completed(false) {
        ZeroMemory(&Header, sizeof(WAVEHDR));
    }

    /// Creates one empty Windows audio backend with default bus state.
    Win32AudioBackend::Win32AudioBackend()
        : VoicesById(),
          BusGainsById(),
          BusPausedById(),
          VoicesMutex(),
          NextVoiceId(1) {
        BusGainsById.emplace("master", 1.0f);
        BusGainsById.emplace("music", 1.0f);
        BusGainsById.emplace("sfx", 1.0f);

        BusPausedById.emplace("master", false);
        BusPausedById.emplace("music", false);
        BusPausedById.emplace("sfx", false);
    }

    /// Stops and releases every active native playback voice.
    Win32AudioBackend::~Win32AudioBackend() {
        std::vector<int32_t> voiceIds;
        {
            std::scoped_lock<std::mutex> lock(VoicesMutex);
            voiceIds.reserve(VoicesById.size());
            for (const auto& pair : VoicesById) {
                voiceIds.push_back(pair.first);
            }
        }

        for (int32_t voiceId : voiceIds) {
            Stop(voiceId);
        }
    }

    /// Returns whether one backend voice is still active.
    bool Win32AudioBackend::IsPlaying(int32_t voiceId) {
        std::scoped_lock<std::mutex> lock(VoicesMutex);
        auto iterator = VoicesById.find(voiceId);
        if (iterator == VoicesById.end()) {
            return false;
        }

        const VoiceState& voice = iterator->second;
        return voice.Queued || voice.Paused || (voice.Loop && voice.Completed);
    }

    /// Starts playback of one PCM audio asset and returns its backend-owned voice identifier.
    int32_t Win32AudioBackend::Play(::AudioAsset* asset, ::AudioPlaybackRequest* request) {
        if (asset == nullptr) {
            return -1;
        }

        if (asset->get_SampleRate() <= 0 || asset->get_Channels() <= 0) {
            return -1;
        }

        std::string encodingFamilyId = asset->get_EncodingFamilyId();
        if (!encodingFamilyId.empty() && encodingFamilyId.rfind("pcm", 0) != 0) {
            return -1;
        }

        Array<uint8_t>* encodedBytes = asset->get_EncodedBytes();
        if (encodedBytes == nullptr || encodedBytes->Length <= 0) {
            return -1;
        }

        VoiceState voice;
        voice.EncodedBytes.assign(encodedBytes->begin(), encodedBytes->end());
        voice.BusId = NormalizeBusId(
            request != nullptr && !request->get_BusId().empty()
                ? request->get_BusId()
                : asset->get_DefaultBusId());
        voice.BaseGain = ClampGain(request != nullptr ? request->get_Gain() : 1.0f);
        voice.Loop = request != nullptr ? request->get_Loop() : asset->get_DefaultLoop();

        WAVEFORMATEX format = BuildWaveFormat(*asset);
        MMRESULT result = ::waveOutOpen(
            &voice.DeviceHandle,
            WAVE_MAPPER,
            &format,
            reinterpret_cast<DWORD_PTR>(HandleWaveOutCallback),
            reinterpret_cast<DWORD_PTR>(this),
            CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
            return -1;
        }

        voice.Header.lpData = reinterpret_cast<LPSTR>(voice.EncodedBytes.data());
        voice.Header.dwBufferLength = static_cast<DWORD>(voice.EncodedBytes.size());

        int32_t voiceId = 0;
        {
            std::scoped_lock<std::mutex> lock(VoicesMutex);
            voiceId = NextVoiceId++;
            voice.Header.dwUser = static_cast<DWORD_PTR>(voiceId);
            VoicesById.emplace(voiceId, std::move(voice));
        }

        {
            std::scoped_lock<std::mutex> lock(VoicesMutex);
            auto iterator = VoicesById.find(voiceId);
            if (iterator == VoicesById.end()) {
                return -1;
            }

            VoiceState& storedVoice = iterator->second;
            result = ::waveOutPrepareHeader(storedVoice.DeviceHandle, &storedVoice.Header, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                VoiceState failedVoice = std::move(storedVoice);
                VoicesById.erase(iterator);
                DisposeVoice(failedVoice);
                return -1;
            }

            ApplyVoiceVolume(storedVoice);
            result = ::waveOutWrite(storedVoice.DeviceHandle, &storedVoice.Header, sizeof(WAVEHDR));
            if (result != MMSYSERR_NOERROR) {
                VoiceState failedVoice = std::move(storedVoice);
                VoicesById.erase(iterator);
                DisposeVoice(failedVoice);
                return -1;
            }

            storedVoice.Queued = true;
            storedVoice.Completed = false;
            ApplyVoicePlaybackState(storedVoice);
        }

        return voiceId;
    }

    /// Applies one linear gain value to a named mixer bus.
    void Win32AudioBackend::SetBusGain(std::string busId, float gain) {
        std::scoped_lock<std::mutex> lock(VoicesMutex);
        std::string normalizedBusId = NormalizeBusId(busId);
        BusGainsById[normalizedBusId] = ClampGain(gain);

        for (auto& pair : VoicesById) {
            VoiceState& voice = pair.second;
            if (voice.BusId == normalizedBusId) {
                ApplyVoiceVolume(voice);
            }
        }
    }

    /// Pauses or resumes one named mixer bus.
    void Win32AudioBackend::SetBusPaused(std::string busId, bool paused) {
        std::scoped_lock<std::mutex> lock(VoicesMutex);
        std::string normalizedBusId = NormalizeBusId(busId);
        BusPausedById[normalizedBusId] = paused;

        for (auto& pair : VoicesById) {
            VoiceState& voice = pair.second;
            if (voice.BusId == normalizedBusId) {
                ApplyVoicePlaybackState(voice);
            }
        }
    }

    /// Stops one active backend voice.
    void Win32AudioBackend::Stop(int32_t voiceId) {
        VoiceState voice;
        bool foundVoice = false;
        {
            std::scoped_lock<std::mutex> lock(VoicesMutex);
            auto iterator = VoicesById.find(voiceId);
            if (iterator == VoicesById.end()) {
                return;
            }

            voice = std::move(iterator->second);
            VoicesById.erase(iterator);
            foundVoice = true;
        }

        if (foundVoice) {
            DisposeVoice(voice);
        }
    }

    /// Advances backend-owned maintenance work such as loop restarts and completed-voice cleanup.
    void Win32AudioBackend::Update() {
        std::vector<int32_t> completedVoiceIds;
        {
            std::scoped_lock<std::mutex> lock(VoicesMutex);
            completedVoiceIds.reserve(VoicesById.size());
            for (const auto& pair : VoicesById) {
                if (pair.second.Completed) {
                    completedVoiceIds.push_back(pair.first);
                }
            }
        }

        for (int32_t voiceId : completedVoiceIds) {
            bool shouldRestart = false;
            {
                std::scoped_lock<std::mutex> lock(VoicesMutex);
                auto iterator = VoicesById.find(voiceId);
                if (iterator == VoicesById.end()) {
                    continue;
                }

                shouldRestart = iterator->second.Loop;
            }

            if (shouldRestart) {
                if (!RestartVoice(voiceId)) {
                    Stop(voiceId);
                }
                continue;
            }

            Stop(voiceId);
        }
    }

    /// Receives wave-out callbacks and marks completed voices for the next backend update tick.
    void CALLBACK Win32AudioBackend::HandleWaveOutCallback(
        HWAVEOUT waveOutHandle,
        UINT message,
        DWORD_PTR instance,
        DWORD_PTR param1,
        DWORD_PTR param2) {
        (void)waveOutHandle;
        (void)param2;

        if (message != WOM_DONE || instance == 0 || param1 == 0) {
            return;
        }

        Win32AudioBackend* backend = reinterpret_cast<Win32AudioBackend*>(instance);
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(param1);
        backend->MarkVoiceCompleted(static_cast<int32_t>(header->dwUser));
    }

    /// Marks one voice as completed after the wave-out device signals buffer completion.
    void Win32AudioBackend::MarkVoiceCompleted(int32_t voiceId) {
        std::scoped_lock<std::mutex> lock(VoicesMutex);
        auto iterator = VoicesById.find(voiceId);
        if (iterator == VoicesById.end()) {
            return;
        }

        iterator->second.Completed = true;
        iterator->second.Queued = false;
    }

    /// Normalizes one bus identifier into the backend's case-insensitive map key format.
    std::string Win32AudioBackend::NormalizeBusId(const std::string& busId) {
        std::string normalizedBusId = busId.empty() ? "master" : busId;
        std::transform(
            normalizedBusId.begin(),
            normalizedBusId.end(),
            normalizedBusId.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        return normalizedBusId;
    }

    /// Clamps one runtime gain value into the backend-supported linear gain range.
    float Win32AudioBackend::ClampGain(float gain) {
        if (!std::isfinite(gain)) {
            return 1.0f;
        }

        return std::clamp(gain, 0.0f, 1.0f);
    }

    /// Builds the PCM wave format used to open one voice device for the supplied asset.
    WAVEFORMATEX Win32AudioBackend::BuildWaveFormat(AudioAsset& asset) {
        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(asset.get_Channels());
        format.nSamplesPerSec = static_cast<DWORD>(asset.get_SampleRate());
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>((format.nChannels * format.wBitsPerSample) / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        format.cbSize = 0;
        return format;
    }

    /// Applies the current bus volume and paused state to one voice.
    void Win32AudioBackend::ApplyVoiceState(VoiceState& voice) {
        ApplyVoiceVolume(voice);
        ApplyVoicePlaybackState(voice);
    }

    /// Applies the current bus volume to one voice.
    void Win32AudioBackend::ApplyVoiceVolume(VoiceState& voice) {
        if (voice.DeviceHandle == nullptr) {
            return;
        }

        float busGain = 1.0f;
        auto iterator = BusGainsById.find(voice.BusId);
        if (iterator != BusGainsById.end()) {
            busGain = iterator->second;
        }

        DWORD channelVolume = static_cast<DWORD>(std::lround(ClampGain(voice.BaseGain * busGain) * 65535.0f));
        DWORD packedVolume = channelVolume | (channelVolume << 16);
        ::waveOutSetVolume(voice.DeviceHandle, packedVolume);
    }

    /// Applies the current bus paused state to one voice.
    void Win32AudioBackend::ApplyVoicePlaybackState(VoiceState& voice) {
        bool shouldPause = false;
        auto iterator = BusPausedById.find(voice.BusId);
        if (iterator != BusPausedById.end()) {
            shouldPause = iterator->second;
        }

        if (!voice.Queued || voice.Completed || voice.DeviceHandle == nullptr) {
            voice.Paused = shouldPause;
            return;
        }

        if (shouldPause == voice.Paused) {
            return;
        }

        voice.Paused = shouldPause;
        if (shouldPause) {
            ::waveOutPause(voice.DeviceHandle);
            return;
        }

        ::waveOutRestart(voice.DeviceHandle);
    }

    /// Restarts one completed looping voice and returns whether the restart succeeded.
    bool Win32AudioBackend::RestartVoice(int32_t voiceId) {
        std::scoped_lock<std::mutex> lock(VoicesMutex);
        auto iterator = VoicesById.find(voiceId);
        if (iterator == VoicesById.end()) {
            return false;
        }

        VoiceState& voice = iterator->second;
        if (!voice.Completed || voice.DeviceHandle == nullptr) {
            return true;
        }

        voice.Header.dwFlags &= ~WHDR_DONE;
        voice.Completed = false;
        voice.Queued = true;
        voice.Paused = false;
        ApplyVoiceVolume(voice);

        MMRESULT result = ::waveOutWrite(voice.DeviceHandle, &voice.Header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            voice.Completed = true;
            voice.Queued = false;
            return false;
        }

        ApplyVoicePlaybackState(voice);
        return true;
    }

    /// Stops the supplied voice and releases its native resources.
    void Win32AudioBackend::DisposeVoice(VoiceState& voice) {
        if (voice.DeviceHandle != nullptr) {
            ::waveOutReset(voice.DeviceHandle);
            if ((voice.Header.dwFlags & WHDR_PREPARED) != 0) {
                ::waveOutUnprepareHeader(voice.DeviceHandle, &voice.Header, sizeof(WAVEHDR));
            }
            ::waveOutClose(voice.DeviceHandle);
            voice.DeviceHandle = nullptr;
        }

        ZeroMemory(&voice.Header, sizeof(WAVEHDR));
        voice.EncodedBytes.clear();
        voice.Queued = false;
        voice.Paused = false;
        voice.Completed = true;
    }
}
