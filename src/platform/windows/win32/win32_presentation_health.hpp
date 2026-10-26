#pragma once
#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include <system_error>

namespace helengine::windows {
    /// Publishes actual frame progress separately from frames whose scene can grant external input ownership.
    class Win32PresentationHealth {
    public:
        /// Names successful presentation progress, including frames produced during incremental scene loading.
        static constexpr const wchar_t* PresentedTickProperty=L"HelEngine.PresentedFrameTick";
        /// Names successful frames whose startup scene is completely materialized; missing or zero never grants input.
        static constexpr const wchar_t* ReadyTickProperty=L"HelEngine.ReadyFrameTick";
        /// Records only actual S_OK presentations, publishing scene readiness independently from loading-frame health.
        static void RecordPresentation(HWND window,HRESULT result,bool sceneReady,bool renderReady) {
            if(result!=S_OK) return;
            if(window==nullptr) throw std::invalid_argument("Presentation progress requires its actual output window.");
            static_assert(sizeof(UINT_PTR)>=sizeof(std::uint64_t),"Presentation timestamps require the Windows x64 host.");
            auto encoded=static_cast<UINT_PTR>(GetTickCount64()+1);
            const bool ready=sceneReady && renderReady;
            if(!ready) PublishTick(window,ReadyTickProperty,0);
            PublishTick(window,PresentedTickProperty,encoded);
            if(ready) PublishTick(window,ReadyTickProperty,encoded);
        }
    private:
        /// Reports a failed metadata write rather than allowing a host to infer presentation or readiness that was not published.
        static void PublishTick(HWND window,const wchar_t* property,UINT_PTR encoded) {
            if(!SetPropW(window,property,reinterpret_cast<HANDLE>(encoded)))
                throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Publish presented-frame state");
        }
    };
}