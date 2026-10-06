#include <stdexcept>
#include <iostream>
#include <string>
#include "platform/windows/win32/win32_additional_window_settings.hpp"
#include "platform/windows/win32/win32_command_line_options.hpp"

namespace helengine::windows {
    /// Exercises actual native configuration parsing without opening windows or graphics devices.
    class WindowConfigurationTests {
    public:
        /// Runs positive, malformed, duplicate and range-validation cases against the production parsers.
        static void Run() {
            Win32AdditionalWindowSettings normal = Win32AdditionalWindowSettings::Parse("preview,normal,-640,25,640,360");
            if (normal.GetTag() != "preview" || normal.GetMode() != Win32WindowMode::Normal || normal.GetLeft() != -640
                || normal.GetTop() != 25 || normal.GetWidth() != 640 || normal.GetHeight() != 360) {
                throw std::runtime_error("Normal window configuration was not preserved.");
            }
            Win32AdditionalWindowSettings overlay = Win32AdditionalWindowSettings::Parse("glass,overlay,0,0,800,600");
            if (overlay.GetMode() != Win32WindowMode::Overlay || overlay.GetWidth() != 800) {
                throw std::runtime_error("Overlay window configuration was not preserved.");
            }
            const char* invalid[] = { "", "main,normal,0,0,640,360", "../escape,normal,0,0,640,360",
                "a,other,0,0,640,360", "a,normal,0,0,0,360", "a,normal,0,0,-1,360",
                "a,normal,0,0,16385,360", "a,normal,0,0,640,0", "a,normal,0,0,640,360,extra",
                "a,normal,0,0,640", "a,normal,x,0,640,360", "a,normal,2147483648,0,640,360",
                "a,normal,0,0,640px,360", "a space,normal,0,0,640,360" };
            for (const char* value : invalid) { ExpectInvalid(value); }
            wchar_t executable[] = L"player.exe";
            wchar_t flag[] = L"--window";
            wchar_t first[] = L"preview,normal,20,20,640,360";
            wchar_t second[] = L"glass,overlay,0,0,640,360";
            wchar_t* arguments[] = { executable, flag, first, flag, second };
            Win32CommandLineOptions options = Win32CommandLineOptions::Parse(5, arguments);
            if (options.GetAdditionalWindows().size() != 2 || options.HasIgnoredArguments()) {
                throw std::runtime_error("Repeated --window flags must be parsed as two views.");
            }
            wchar_t* duplicate[] = { executable, flag, first, flag, first };
            bool rejected = false;
            try { Win32CommandLineOptions::Parse(5, duplicate); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) { throw std::runtime_error("Duplicate tags were accepted."); }
            wchar_t caseVariant[] = L"Preview,normal,0,0,640,360";
            wchar_t* caseDuplicate[] = { executable, flag, first, flag, caseVariant };
            rejected = false;
            try { Win32CommandLineOptions::Parse(5, caseDuplicate); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) { throw std::runtime_error("Tags differing only by case could overwrite capture files."); }
            wchar_t* missing[] = { executable, flag };
            rejected = false;
            try { Win32CommandLineOptions::Parse(2, missing); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) { throw std::runtime_error("Missing --window value was accepted."); }
            wchar_t* defaults[] = { executable };
            if (!Win32CommandLineOptions::Parse(1, defaults).GetAdditionalWindows().empty()) {
                throw std::runtime_error("The default path gained additional views.");
            }
        }
    private:
        /// Requires invalid secondary configurations to fail before any native window is created.
        static void ExpectInvalid(const std::string& value) {
            bool rejected = false;
            try { Win32AdditionalWindowSettings::Parse(value); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) { throw std::runtime_error("Invalid window configuration accepted: " + value); }
        }
    };
}

/// Runs native behavioral parsing checks and reports any failure as a nonzero exit.
int main() {
    try { helengine::windows::WindowConfigurationTests::Run(); std::cout << "PASS native window configuration tests\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
