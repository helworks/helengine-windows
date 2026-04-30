#include "platform/windows/directx/directx_feature_bootstrap.hpp"

int main() {
    helengine::windows::DirectXFeatureBootstrap::RegisterEnabledFeatures();
    return 0;
}
