#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>

namespace helengine::windows {
    /// Discovers the active monitor's HDR and Windows SDR-white policy without changing display settings.
    class DirectX11OutputColorState {
    public:
        /// Identifies the physical monitor used for the query, including windows parented into Explorer.
        HMONITOR Monitor = nullptr;
        /// Selects FP16 linear scRGB only when the current display and device support HDR output.
        bool Hdr = false;
        /// Scales authored UI white to the monitor's Windows SDR reference white, in 80-nit scRGB units.
        float SdrWhiteScale = 1;
        /// Records monitor-reported peak luminance for diagnostics; this is not a measured brightness.
        float ReportedPeakNits = 0;
        /// Makes an SDR capability fallback visible instead of silently claiming native HDR presentation.
        std::string Reason = "display_not_hdr";

        /// Converts an authored SDR color channel to linear light before HDR blending or premultiplication.
        static float DecodeSrgb(float value) {
            double channel=value>0 ? value : 0;
            return static_cast<float>(channel<=0.04045 ? channel/12.92 : std::pow((channel+0.055)/1.055,2.4));
        }

        /// Resolves one HWND to its active output and validates the actual device's requested sample support.
        static DirectX11OutputColorState Query(HWND window, ID3D11Device* device, UINT samples) {
            if(!window || !device) throw std::invalid_argument("Output color discovery requires a window and graphics device.");
            DirectX11OutputColorState state;
            state.Monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
            if(!state.Monitor) throw std::runtime_error("Cannot resolve the presentation monitor.");
            Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
            Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create output color factory");
            for(UINT adapterIndex=0;;++adapterIndex) {
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                HRESULT adapterResult=factory->EnumAdapters1(adapterIndex,&adapter);
                if(adapterResult==DXGI_ERROR_NOT_FOUND) break;
                Check(adapterResult,"Enumerate output color adapter");
                for(UINT outputIndex=0;;++outputIndex) {
                    Microsoft::WRL::ComPtr<IDXGIOutput> output;
                    HRESULT outputResult=adapter->EnumOutputs(outputIndex,&output);
                    if(outputResult==DXGI_ERROR_NOT_FOUND) break;
                    Check(outputResult,"Enumerate output color display");
                    DXGI_OUTPUT_DESC description{};Check(output->GetDesc(&description),"Read output monitor");
                    if(description.Monitor!=state.Monitor) continue;
                    Microsoft::WRL::ComPtr<IDXGIOutput6> advanced;
                    HRESULT query=output.As(&advanced);
                    if(query==E_NOINTERFACE) {state.Reason="dxgi_output6_unavailable";return state;}
                    Check(query,"Query advanced output interface");
                    DXGI_OUTPUT_DESC1 detail{};Check(advanced->GetDesc1(&detail),"Read advanced output descriptor");
                    state.ReportedPeakNits=detail.MaxLuminance;
                    if(detail.ColorSpace!=DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) return state;
                    state.SdrWhiteScale=ReadSdrWhiteScale(description.DeviceName);
                    UINT support=0;Check(device->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT,&support),"Check scRGB render support");
                    const UINT required=D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_BLENDABLE;
                    if((support&required)!=required) {state.Reason="fp16_render_blend_unsupported";return state;}
                    if(samples>1) {
                        UINT levels=0;Check(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT,samples,&levels),"Check scRGB multisample support");
                        if(!levels || !(support&D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE)) {state.Reason="fp16_msaa_unsupported";return state;}
                    }
                    state.Hdr=true;state.Reason="hdr_active";return state;
                }
            }
            state.Reason="monitor_output_unavailable";return state;
        }

    private:
        /// Reads Windows reference white from the active CCD path matching the DXGI source name.
        static float ReadSdrWhiteScale(const wchar_t* sourceName) {
            for(unsigned attempt=0;attempt<3;++attempt) {
                UINT pathCount=0,modeCount=0;
                LONG result=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);
                if(result!=ERROR_SUCCESS) throw std::runtime_error("Read output display-config sizes failed: "+std::to_string(result));
                std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
                result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
                if(result==ERROR_INSUFFICIENT_BUFFER) continue;
                if(result!=ERROR_SUCCESS) throw std::runtime_error("Read active output display-config paths failed: "+std::to_string(result));
                for(UINT index=0;index<pathCount;++index) {
                    const auto& path=paths[index];DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                    source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
                    result=DisplayConfigGetDeviceInfo(&source.header);
                    if(result!=ERROR_SUCCESS) throw std::runtime_error("Read output display-config source failed: "+std::to_string(result));
                    if(wcscmp(source.viewGdiDeviceName,sourceName)!=0) continue;
                    DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
                    white.header={DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(white),path.targetInfo.adapterId,path.targetInfo.id};
                    result=DisplayConfigGetDeviceInfo(&white.header);
                    if(result!=ERROR_SUCCESS) throw std::runtime_error("Read Windows SDR reference white failed: "+std::to_string(result));
                    if(white.SDRWhiteLevel==0) throw std::runtime_error("Windows returned a zero SDR reference white level.");
                    return static_cast<float>(white.SDRWhiteLevel/1000.0);
                }
                throw std::runtime_error("The HDR monitor has no matching active Windows display path.");
            }
            throw std::runtime_error("Display configuration kept changing during HDR reference-white discovery.");
        }
        /// Keeps unexpected graphics API failures distinct from an explicitly unsupported HDR capability.
        static void Check(HRESULT result,const char* operation) {
            if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(static_cast<unsigned long>(result)));
        }
    };
}
