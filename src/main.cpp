#include "platform/windows/directx11/directx11_feature_bootstrap.hpp"

int main() {
    helengine::windows::DirectX11FeatureBootstrap::RegisterEnabledFeatures();
    return 0;
}
