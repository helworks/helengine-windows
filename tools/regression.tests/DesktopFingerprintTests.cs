namespace helengine.windows.regression.tests {
    /// <summary>
    /// Verifies real desktop fingerprints retain shell diagnostics and fail comparisons when their attachment changes.
    /// </summary>
    public sealed class DesktopFingerprintTests {
        /// <summary>
        /// A complete opaque desktop view fingerprint with the shell parent and physical bounds emitted by the host.
        /// </summary>
        const string SampleLine = "HOST_FINGERPRINT format=87 alpha=3 swapEffect=0 buffers=1 scaling=0 style=0x5E000000 exStyle=0x080800A0 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=off idleFrames=0 activeFrames=30 windowMode=desktop window=wallpaper dpi=96 dpiAwareness=permonitorv2 windowRect=default elapsedMs=483 desktopParent=Progman desktopRect=0,0,640,360";

        /// <summary>
        /// Desktop log parsing preserves its attachment details and identical healthy runs compare successfully.
        /// </summary>
        [Fact]
        public void Parse_and_compare_identical_desktop_runs_preserve_shell_fields() {
            HostFingerprint fingerprint = HostFingerprint.Parse(SampleLine);
            Assert.Equal("desktop", fingerprint.WindowMode);
            Assert.Equal("Progman", fingerprint.Fields["desktopParent"]);
            Assert.Equal("0,0,640,360", fingerprint.Fields["desktopRect"]);
            Assert.True(HostFingerprintComparer.Compare("axis_test", fingerprint, fingerprint).Passed);
        }

        /// <summary>
        /// A desktop line lacking required shell diagnostics is rejected instead of being read with assumed attachment.
        /// </summary>
        /// <param name="missingToken">Required shell field removed from the complete line.</param>
        [Theory]
        [InlineData(" desktopParent=Progman")]
        [InlineData(" desktopRect=0,0,640,360")]
        public void Parse_rejects_missing_desktop_diagnostics(string missingToken) {
            Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace(missingToken, string.Empty)));
        }

        /// <summary>
        /// Shell diagnostics on an ordinary window are rejected so a mode change cannot leave misleading metadata.
        /// </summary>
        [Fact]
        public void Parse_rejects_desktop_fields_on_ordinary_views() {
            Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace("windowMode=desktop", "windowMode=normal")));
        }

        /// <summary>
        /// Changing either shell parent or physical coverage makes the recorded desktop comparison fail by field name.
        /// </summary>
        /// <param name="recordedToken">Original desktop field token.</param>
        /// <param name="actualToken">Different field token in the current run.</param>
        /// <param name="fieldName">Diagnostic field that must be reported as changed.</param>
        [Theory]
        [InlineData("desktopParent=Progman", "desktopParent=WorkerW", "desktopParent")]
        [InlineData("desktopRect=0,0,640,360", "desktopRect=-640,0,0,360", "desktopRect")]
        public void Compare_rejects_changed_desktop_attachment(string recordedToken, string actualToken, string fieldName) {
            HostFingerprint recorded = HostFingerprint.Parse(SampleLine);
            HostFingerprint actual = HostFingerprint.Parse(SampleLine.Replace(recordedToken, actualToken));
            HostFingerprintComparison comparison = HostFingerprintComparer.Compare("axis_test", recorded, actual);
            Assert.False(comparison.Passed);
            Assert.Contains(comparison.Failures, failure => failure.Contains(" " + fieldName + " ", StringComparison.Ordinal));
        }

        /// <summary>
        /// Comparing an ordinary record with a desktop run reports the mode change without reading absent shell fields.
        /// </summary>
        [Fact]
        public void Compare_reports_desktop_mode_change_without_missing_field_failure() {
            HostFingerprint normal = HostFingerprint.Parse(HostFingerprintTests.SampleLine);
            HostFingerprint desktop = HostFingerprint.Parse(SampleLine);
            HostFingerprintComparison comparison = HostFingerprintComparer.Compare("axis_test", normal, desktop);
            Assert.False(comparison.Passed);
            Assert.Contains("fingerprint axis_test windowMode recorded=normal actual=desktop", comparison.Failures);
        }
    }
}
