#include "platform/windows/win32/win32_command_line_options.hpp"
#include <cassert>
#include <stdexcept>
#include <cstdio>
using helengine::windows::Win32CommandLineOptions;

/// Verifies an opt-in hidden test launch while ordinary launches keep their existing presentation.
int main() {
    wchar_t executable[] = L"player";
    wchar_t visibility[] = L"--window-visibility";
    wchar_t hidden[] = L"hidden";
    wchar_t visible[] = L"visible";
    wchar_t invalid[] = L"sometimes";
    wchar_t sceneFlag[] = L"--scene";
    wchar_t scene[] = L"cube_test";
    wchar_t framesFlag[] = L"--frames";
    wchar_t frames[] = L"3";
    wchar_t* normal[] = {executable};
    assert(Win32CommandLineOptions::Parse(1, normal).ShouldShowWindow());
    wchar_t* arguments[] = {executable, visibility, hidden, sceneFlag, scene, framesFlag, frames};
    auto parsed = Win32CommandLineOptions::Parse(7, arguments);
    assert(!parsed.ShouldShowWindow() && parsed.GetSceneId()=="cube_test" && parsed.GetFrameLimit()==3);
    arguments[2]=visible;
    assert(Win32CommandLineOptions::Parse(7, arguments).ShouldShowWindow());
    arguments[2]=invalid;
    bool rejected=false;
    try { Win32CommandLineOptions::Parse(7, arguments); } catch (const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    wchar_t* duplicate[] = {executable, visibility, hidden, visibility, visible};
    rejected=false;
    try { Win32CommandLineOptions::Parse(5, duplicate); } catch (const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    wchar_t* missing[] = {executable, visibility};
    rejected=false;
    try { Win32CommandLineOptions::Parse(2, missing); } catch (const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    std::puts("PASS default visible, hidden/visible overrides, scene/frame composition, invalid/duplicate/missing values");
}
