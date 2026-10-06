namespace HelEngine.Windows.Tests {
    /// <summary>
    /// Manipulates only windows created by the acceptance player's process; no cursor or display settings are changed.
    /// </summary>
    public static class WindowLifecycleProbe {
        /// <summary>
        /// Native rectangle coordinates used to measure a test player's client area.
        /// </summary>
        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
        public struct NativeRectangle {
            /// <summary>Left rectangle coordinate.</summary>
            public int Left;
            /// <summary>Top rectangle coordinate.</summary>
            public int Top;
            /// <summary>Right rectangle coordinate.</summary>
            public int Right;
            /// <summary>Bottom rectangle coordinate.</summary>
            public int Bottom;
        }

        /// <summary>Receives one native handle during process-specific enumeration.</summary>
        delegate bool EnumerationCallback(System.IntPtr handle, System.IntPtr data);

        /// <summary>Enumerates top-level windows without reading their pixels.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool EnumWindows(EnumerationCallback callback, System.IntPtr data);

        /// <summary>Retrieves the process owning a candidate window.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern uint GetWindowThreadProcessId(System.IntPtr handle, out uint processId);

        /// <summary>Retrieves a player window's caption for matching its configured tag.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        static extern int GetWindowText(System.IntPtr handle, System.Text.StringBuilder text, int maximum);

        /// <summary>Filters native helper windows that are not visible player views.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool IsWindowVisible(System.IntPtr handle);

        /// <summary>Posts lifecycle messages to a test-owned window.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool PostMessage(System.IntPtr handle, uint message, System.IntPtr wParam, System.IntPtr lParam);

        /// <summary>Changes a test window's minimized state.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool ShowWindow(System.IntPtr handle, int command);

        /// <summary>Measures the current client rectangle for an actual native window.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool GetClientRect(System.IntPtr handle, out NativeRectangle rectangle);

        /// <summary>Measures the current outer rectangle including the window frame.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool GetWindowRect(System.IntPtr handle, out NativeRectangle rectangle);

        /// <summary>Applies an actual native resize without activation or movement.</summary>
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        static extern bool SetWindowPos(System.IntPtr handle, System.IntPtr after, int left, int top, int width, int height, uint flags);

        /// <summary>Finds top-level handles owned exclusively by the specified acceptance process.</summary>
        /// <param name="processId">Process started by the acceptance launcher.</param>
        /// <returns>Captions and handles belonging to that process.</returns>
        public static System.Collections.Generic.Dictionary<string, System.IntPtr> FindWindows(int processId) {
            var result = new System.Collections.Generic.Dictionary<string, System.IntPtr>();
            EnumWindows(delegate(System.IntPtr handle, System.IntPtr data) {
                uint owner;
                GetWindowThreadProcessId(handle, out owner);
                if (owner == (uint)processId && IsWindowVisible(handle)) {
                    var caption = new System.Text.StringBuilder(256);
                    GetWindowText(handle, caption, caption.Capacity);
                    result[caption.ToString()] = handle;
                }
                return true;
            }, System.IntPtr.Zero);
            return result;
        }

        /// <summary>Minimizes only the supplied test-owned window.</summary>
        /// <param name="handle">Test player handle to minimize.</param>
        public static void Minimize(System.IntPtr handle) { ShowWindow(handle, 6); }

        /// <summary>Restores only the supplied test-owned window without activating it.</summary>
        /// <param name="handle">Test player handle to restore.</param>
        public static void Restore(System.IntPtr handle) { ShowWindow(handle, 4); }

        /// <summary>Closes only the supplied test-owned window through its ordinary message handler.</summary>
        /// <param name="handle">Test player handle to close.</param>
        public static void Close(System.IntPtr handle) {
            if (!PostMessage(handle, 0x0010, System.IntPtr.Zero, System.IntPtr.Zero)) {
                throw new System.InvalidOperationException("Could not close the acceptance window.");
            }
        }

        /// <summary>Resizes a test window to an exact client pixel size using its actual frame dimensions.</summary>
        /// <param name="handle">Test-owned native window.</param>
        /// <param name="width">Requested client width.</param>
        /// <param name="height">Requested client height.</param>
        public static void Resize(System.IntPtr handle, int width, int height) {
            NativeRectangle client;
            NativeRectangle outer;
            if (!GetClientRect(handle, out client) || !GetWindowRect(handle, out outer)) {
                throw new System.InvalidOperationException("Cannot measure the acceptance window.");
            }
            int frameWidth = outer.Right - outer.Left - (client.Right - client.Left);
            int frameHeight = outer.Bottom - outer.Top - (client.Bottom - client.Top);
            if (!SetWindowPos(handle, System.IntPtr.Zero, 0, 0, width + frameWidth, height + frameHeight, 0x0016)) {
                throw new System.InvalidOperationException("Could not resize the acceptance window.");
            }
        }
    }
}
