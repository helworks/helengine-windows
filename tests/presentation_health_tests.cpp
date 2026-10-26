#include "platform/windows/win32/win32_presentation_health.hpp"
#include <iostream>
#include "platform/windows/win32/win32_frame_readiness.hpp"

/// Exercises only a message-only test HWND: no visible window, hooks, GPU work or player process.
int main() {
    HWND window=CreateWindowExW(0,L"STATIC",L"OwnedPresentationHealthTest",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window) return 1;
    try {
        using helengine::windows::Win32PresentationHealth;
        helengine::windows::Win32FrameReadiness frame;
        frame.Begin(true); frame.ObservePass(false); frame.ObservePass(true);
        if(frame.IsComplete()) throw std::runtime_error("A later prepared pass concealed an earlier omitted pass.");
        frame.Begin(true); frame.ObservePass(true); frame.ObservePass(true);
        if(!frame.IsComplete()) throw std::runtime_error("The next complete frame failed to recover readiness.");
        frame.Begin(false); frame.ObservePass(true);
        if(frame.IsComplete()) throw std::runtime_error("An initially pending frame became ready without a new draw.");
        if(GetPropW(window,Win32PresentationHealth::PresentedTickProperty)) throw std::runtime_error("Unpresented HWND reported a frame.");
        Win32PresentationHealth::RecordPresentation(window,S_FALSE,false,false);
        if(GetPropW(window,Win32PresentationHealth::PresentedTickProperty)) throw std::runtime_error("A non-frame success status granted readiness.");
        Win32PresentationHealth::RecordPresentation(window,S_OK,false,true);
        if(!GetPropW(window,Win32PresentationHealth::PresentedTickProperty)) throw std::runtime_error("Loading failed to publish actual presentation progress.");
        if(GetPropW(window,Win32PresentationHealth::ReadyTickProperty)) throw std::runtime_error("A loading frame granted scene readiness.");
        Win32PresentationHealth::RecordPresentation(window,S_FALSE,true,true);
        if(GetPropW(window,Win32PresentationHealth::ReadyTickProperty)) throw std::runtime_error("Occlusion granted scene readiness.");
        Win32PresentationHealth::RecordPresentation(window,S_OK,true,false);
        if(GetPropW(window,Win32PresentationHealth::ReadyTickProperty)) throw std::runtime_error("A completed scene with pending shaders granted input readiness.");
        auto before=GetTickCount64();
        Win32PresentationHealth::RecordPresentation(window,S_OK,true,true);
        auto encoded=reinterpret_cast<UINT_PTR>(GetPropW(window,Win32PresentationHealth::PresentedTickProperty));
        if(encoded==0 || encoded-1<before || encoded-1>GetTickCount64()) throw std::runtime_error("Successful Present did not publish monotonic progress.");
        Win32PresentationHealth::RecordPresentation(window,E_FAIL,true,true);
        if(reinterpret_cast<UINT_PTR>(GetPropW(window,Win32PresentationHealth::PresentedTickProperty))!=encoded)
            throw std::runtime_error("A failed Present renewed progress.");
        auto ready=reinterpret_cast<UINT_PTR>(GetPropW(window,Win32PresentationHealth::ReadyTickProperty));
        if(ready!=encoded) throw std::runtime_error("Ready presentation did not publish the same actual frame timestamp.");
        Win32PresentationHealth::RecordPresentation(window,S_OK,false,true);
        if(GetPropW(window,Win32PresentationHealth::ReadyTickProperty)) throw std::runtime_error("Loading did not revoke input readiness.");
        DestroyWindow(window);
        std::cout<<"PASS message-only presentation progress: missing, non-frame, successful and failed Present.\n";
        return 0;
    } catch(const std::exception& error) { DestroyWindow(window); std::cerr<<error.what()<<'\n'; return 1; }
}
