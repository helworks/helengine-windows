#pragma once

namespace helengine::windows {
    /// Retains omissions throughout one output draw so later shader completion cannot authorize an incomplete frame.
    class Win32FrameReadiness {
        /// Becomes false on any omitted required pass and can recover only at the beginning of another draw.
        bool Complete=false;
    public:
        /// Starts an independent output draw with its current common-pipeline readiness.
        void Begin(bool pipelineReady) { Complete=pipelineReady; }
        /// Accumulates actual pass completion without reviving a frame that already omitted content.
        void ObservePass(bool complete) { Complete=Complete && complete; }
        /// Reports completion of all required passes observed since this output draw began.
        bool IsComplete() const { return Complete; }
    };
}