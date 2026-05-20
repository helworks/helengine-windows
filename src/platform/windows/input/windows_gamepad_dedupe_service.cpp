#include "platform/windows/input/windows_gamepad_dedupe_service.hpp"

#include <algorithm>
#include <vector>

namespace helengine::windows {
    namespace {
        /// Returns the fixed duplicate-suppression priority for one backend.
        int ResolveBackendPriority(WindowsGamepadBackendKind backendKind) {
            switch (backendKind) {
                case WindowsGamepadBackendKind::SteamInput:
                    return 3;

                case WindowsGamepadBackendKind::XInput:
                    return 2;

                default:
                    return 1;
            }
        }

        /// Returns whether the two backend identities clearly represent the same physical controller.
        bool IsClearlyDuplicate(const WindowsGamepadDeviceIdentity& left, const WindowsGamepadDeviceIdentity& right) {
            if (left.BackendKind == right.BackendKind && left.BackendDeviceId == right.BackendDeviceId) {
                return true;
            }

            bool hasStableSlot = left.SlotIndex != WindowsGamepadDeviceIdentity::InvalidSlotIndex
                && right.SlotIndex != WindowsGamepadDeviceIdentity::InvalidSlotIndex
                && left.SlotIndex == right.SlotIndex;
            if (!hasStableSlot) {
                return false;
            }

            bool isSteamAndXInputPair =
                (left.BackendKind == WindowsGamepadBackendKind::SteamInput && right.BackendKind == WindowsGamepadBackendKind::XInput)
                || (left.BackendKind == WindowsGamepadBackendKind::XInput && right.BackendKind == WindowsGamepadBackendKind::SteamInput);
            if (isSteamAndXInputPair) {
                return true;
            }

            bool hasMatchingVidPid = left.VendorId != 0
                && left.ProductId != 0
                && left.VendorId == right.VendorId
                && left.ProductId == right.ProductId;
            return hasMatchingVidPid;
        }
    }

    /// Merges the supplied backend candidates into one stable list of gamepads for the current frame.
    void WindowsGamepadDedupeService::MergeAndDedupe(
        const List<WindowsGamepadDeviceState>& candidates,
        List<InputGamepadState>& mergedGamepads) {
        mergedGamepads.Clear();
        if (candidates.empty()) {
            return;
        }

        std::vector<const WindowsGamepadDeviceState*> sortedCandidates;
        sortedCandidates.reserve(candidates.size());
        for (std::size_t index = 0; index < candidates.size(); index++) {
            sortedCandidates.push_back(&candidates[index]);
        }

        std::stable_sort(
            sortedCandidates.begin(),
            sortedCandidates.end(),
            [](const WindowsGamepadDeviceState* left, const WindowsGamepadDeviceState* right) {
                return ResolveBackendPriority(left->Identity.BackendKind) > ResolveBackendPriority(right->Identity.BackendKind);
            });

        std::vector<WindowsGamepadDeviceIdentity> acceptedIdentities;
        acceptedIdentities.reserve(sortedCandidates.size());
        for (std::size_t candidateIndex = 0; candidateIndex < sortedCandidates.size(); candidateIndex++) {
            const WindowsGamepadDeviceState& candidate = *sortedCandidates[candidateIndex];
            bool shouldSuppress = false;
            for (std::size_t acceptedIndex = 0; acceptedIndex < acceptedIdentities.size(); acceptedIndex++) {
                if (!IsClearlyDuplicate(candidate.Identity, acceptedIdentities[acceptedIndex])) {
                    continue;
                }

                shouldSuppress = true;
                break;
            }

            if (shouldSuppress) {
                continue;
            }

            acceptedIdentities.push_back(candidate.Identity);
            mergedGamepads.Add(candidate.Gamepad);
        }
    }
}
