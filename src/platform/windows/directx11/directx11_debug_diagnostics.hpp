#pragma once
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"

namespace helengine::windows {
    /// Collects opt-in Direct3D validation messages without changing rendering or device-loss policy.
    class DirectX11DebugDiagnostics {
        /// Avoids repeating validation messages every frame while retaining the first concrete offending call.
        inline static std::unordered_set<int> ReportedIds;
        /// Enables diagnostic overhead only for explicitly requested investigation runs.
        inline static bool Enabled = false;
    public:
        /// Reads the process-local diagnostic switch when creating the shared device.
        static UINT CreationFlags() {
            wchar_t value[8] {};
            Enabled = GetEnvironmentVariableW(L"HELENGINE_D3D11_DEBUG", value, 8) == 1 && value[0] == L'1';
            return D3D11_CREATE_DEVICE_BGRA_SUPPORT | (Enabled ? D3D11_CREATE_DEVICE_DEBUG : 0);
        }
        /// Bounds queued messages and excludes informational chatter while preserving warnings and errors.
        static void Configure(ID3D11Device* device) {
            if (!Enabled) { return; }
            Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
            if (FAILED(device->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf())))) {
                throw std::runtime_error("Requested Direct3D debug layer did not expose its validation queue.");
            }
            D3D11_MESSAGE_SEVERITY ignored[] {D3D11_MESSAGE_SEVERITY_INFO, D3D11_MESSAGE_SEVERITY_MESSAGE};
            D3D11_INFO_QUEUE_FILTER filter {};
            filter.DenyList.NumSeverities = 2; filter.DenyList.pSeverityList = ignored;
            if (FAILED(queue->SetMessageCountLimit(512)) || FAILED(queue->AddStorageFilterEntries(&filter))) {
                throw std::runtime_error("Unable to configure Direct3D validation diagnostics.");
            }
            ReportedIds.clear();
            RuntimeRenderDiagnostics::WriteHostEvent("d3d11.validation", "enabled=1 warnings_and_errors=1");
        }
        /// Writes each validation message ID once and releases the bounded queue after each overlay frame.
        static void Drain(ID3D11Device* device) {
            if (!Enabled) { return; }
            Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
            if (FAILED(device->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf())))) {
                throw std::runtime_error("Direct3D validation queue became unavailable.");
            }
            const UINT64 count = queue->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 index = 0; index < count; ++index) {
                SIZE_T size = 0;
                if (FAILED(queue->GetMessage(index, nullptr, &size))) { throw std::runtime_error("Unable to size Direct3D validation message."); }
                std::vector<unsigned char> storage(size);
                auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
                if (FAILED(queue->GetMessage(index, message, &size))) { throw std::runtime_error("Unable to read Direct3D validation message."); }
                if (!ReportedIds.insert(static_cast<int>(message->ID)).second) { continue; }
                std::ostringstream text;
                text << "severity=" << message->Severity << " id=" << message->ID << " " << message->pDescription;
                RuntimeRenderDiagnostics::WriteHostEvent("d3d11.validation", text.str());
            }
            queue->ClearStoredMessages();
        }
    };
}
