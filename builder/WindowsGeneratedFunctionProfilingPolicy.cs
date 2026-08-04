namespace helengine.windows.builder;

/// <summary>
/// Defines the Windows profiler code-generation settings and the deliberately coarse physics scope selection.
/// </summary>
public static class WindowsGeneratedFunctionProfilingPolicy {
    /// <summary>
    /// Identifies the Boolean code-generation setting that enables Tracy instrumentation.
    /// </summary>
    public const string EnabledSettingId = "codegen-generated-function-profiling";

    /// <summary>
    /// Identifies the semicolon-delimited maintained-symbol prefix allowlist consumed by the C++ generator.
    /// </summary>
    public const string MaintainedSymbolPrefixesSettingId = "codegen-generated-function-profiling-maintained-symbol-prefixes";

    /// <summary>
    /// Selects engine and BEPU pipeline boundaries while excluding hot accessors, SIMD helpers, and constraint leaf functions.
    /// </summary>
    public const string CoarseMaintainedSymbolPrefixes =
        "helengine.Core.UpdatePhysics(" +
        ";helengine.BepuPhysicsWorld3D.Step(" +
        ";helengine.BepuPhysicsWorld3D.CountAwakeDynamicBodies(" +
        ";helengine.BepuPhysicsWorld3D.CollectTriggerEvents(" +
        ";helengine.BepuPhysicsWorld3D.SynchronizeBodiesBackToEntities(" +
        ";BepuPhysics.Simulation.Timestep(" +
        ";BepuPhysics.DefaultTimestepper.Timestep(" +
        ";BepuPhysics.Simulation.PredictBoundingBoxes(" +
        ";BepuPhysics.PoseIntegrator.PredictBoundingBoxes(float, BepuUtilities.Memory.BufferPool," +
        ";BepuPhysics.Simulation.CollisionDetection(" +
        ";BepuPhysics.CollisionDetection.BroadPhase.Update(" +
        ";BepuPhysics.CollisionDetection.BroadPhase.Update2(" +
        ";BepuPhysics.CollisionDetection.CollidableOverlapFinder.DispatchOverlaps(" +
        ";BepuPhysics.Trees.Tree.GetSelfOverlaps(" +
        ";BepuPhysics.CollisionDetection.CollisionBatcher.Flush(" +
        ";BepuPhysics.CollisionDetection.NarrowPhase.Flush(" +
        ";BepuPhysics.Simulation.Solve(" +
        ";BepuPhysics.Solver.PrepareConstraintIntegrationResponsibilities(" +
        ";BepuPhysics.Solver.Solve(" +
        ";BepuPhysics.PoseIntegrator.IntegrateAfterSubstepping(" +
        ";BepuPhysics.Simulation.IncrementallyOptimizeDataStructures(";
}
