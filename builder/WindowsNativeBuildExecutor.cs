#nullable enable
using System.Diagnostics;
using System.Text;

namespace helengine.windows.builder;

/// <summary>
/// Executes the native Windows CMake build for the platform host.
/// </summary>
internal interface IWindowsNativeBuildExecutor {
    /// <summary>
    /// Builds the Windows player from the platform repository root into the supplied native build root.
    /// </summary>
    /// <param name="repositoryRoot">Absolute path to the Windows player repository root.</param>
    /// <param name="buildRoot">Absolute path to the native build directory.</param>
    /// <param name="generatedCoreCppRootPath">Absolute path to the generated core C++ root.</param>
    /// <param name="cancellationToken">Cancellation token that can stop the native build.</param>
    /// <returns>Absolute path to the produced native executable.</returns>
    string Build(string repositoryRoot, string buildRoot, string generatedCoreCppRootPath, CancellationToken cancellationToken);
}

/// <summary>
/// Default CMake-backed Windows native build executor.
/// </summary>
internal sealed class WindowsNativeBuildExecutor : IWindowsNativeBuildExecutor {
    /// <summary>
    /// Environment variable that can override the Visual Studio developer command prompt path.
    /// </summary>
    const string VsDevCmdPathEnvironmentVariable = "HELENGINE_VSDEVCMD_PATH";

    /// <summary>
    /// Visual Studio Installer utility used to discover the active installation.
    /// </summary>
    const string VsWhereRelativePath = @"Microsoft Visual Studio\Installer\vswhere.exe";

    /// <summary>
    /// Shared singleton instance used by production builds.
    /// </summary>
    public static WindowsNativeBuildExecutor Instance { get; } = new WindowsNativeBuildExecutor();

    /// <summary>
    /// Builds the Windows player with CMake and returns the produced executable path.
    /// </summary>
    /// <param name="repositoryRoot">Absolute path to the Windows player repository root.</param>
    /// <param name="buildRoot">Absolute path to the native build directory.</param>
    /// <param name="generatedCoreCppRootPath">Absolute path to the generated core C++ root.</param>
    /// <param name="cancellationToken">Cancellation token that can stop the native build.</param>
    /// <returns>Absolute path to the produced native executable.</returns>
    public string Build(string repositoryRoot, string buildRoot, string generatedCoreCppRootPath, CancellationToken cancellationToken) {
        if (string.IsNullOrWhiteSpace(repositoryRoot)) {
            throw new ArgumentException("Repository root must be provided.", nameof(repositoryRoot));
        }
        if (string.IsNullOrWhiteSpace(buildRoot)) {
            throw new ArgumentException("Build root must be provided.", nameof(buildRoot));
        }
        if (string.IsNullOrWhiteSpace(generatedCoreCppRootPath)) {
            throw new ArgumentException("Generated core root must be provided.", nameof(generatedCoreCppRootPath));
        }

        Directory.CreateDirectory(buildRoot);

        string vsDevCmdPath = ResolveVsDevCmdPath();

        RunProcess(
            "cmd.exe",
            BuildConfigureArguments(repositoryRoot, buildRoot, generatedCoreCppRootPath, vsDevCmdPath),
            repositoryRoot,
            Path.Combine(buildRoot, "native-configure.log"),
            cancellationToken);

        RunProcess(
            "cmd.exe",
            BuildNativeBuildArguments(buildRoot, vsDevCmdPath),
            buildRoot,
            Path.Combine(buildRoot, "native-build.log"),
            cancellationToken);

        string[] executableCandidates = Directory.GetFiles(buildRoot, "helengine_windows.exe", SearchOption.AllDirectories);
        if (executableCandidates.Length == 0) {
            throw new InvalidOperationException($"Native Windows build completed, but no helengine_windows.exe was produced under '{buildRoot}'.");
        }

        return executableCandidates[0];
    }

    /// <summary>
    /// Builds the CMake configure command line for the current Windows player source tree.
    /// </summary>
    static string BuildConfigureArguments(string repositoryRoot, string buildRoot, string generatedCoreCppRootPath, string vsDevCmdPath) {
        return string.Join(" ", [
            "/c",
            $"call \"{vsDevCmdPath}\" && cmake -S \"{repositoryRoot}\" -B \"{buildRoot}\" -G Ninja -DHELENGINE_WINDOWS_INCLUDE_GENERATED_CORE=ON -DHELENGINE_CORE_CPP_ROOT=\"{generatedCoreCppRootPath}\" -DHELENGINE_WINDOWS_RENDER_BACKEND=DirectX11"
        ]);
    }

    /// <summary>
    /// Builds the native CMake build command line for the configured build tree.
    /// </summary>
    static string BuildNativeBuildArguments(string buildRoot, string vsDevCmdPath) {
        return string.Join(" ", [
            "/c",
            $"call \"{vsDevCmdPath}\" && cmake --build \"{buildRoot}\" --config Release"
        ]);
    }

    /// <summary>
    /// Resolves the Visual Studio developer command prompt path from the current machine.
    /// </summary>
    /// <returns>Absolute path to VsDevCmd.bat.</returns>
    static string ResolveVsDevCmdPath() {
        string? overridePath = Environment.GetEnvironmentVariable(VsDevCmdPathEnvironmentVariable);
        if (!string.IsNullOrWhiteSpace(overridePath) && File.Exists(overridePath)) {
            return Path.GetFullPath(overridePath);
        }

        string? vsInstallDir = Environment.GetEnvironmentVariable("VSINSTALLDIR");
        if (!string.IsNullOrWhiteSpace(vsInstallDir)) {
            string candidatePath = Path.Combine(vsInstallDir, @"Common7\Tools\VsDevCmd.bat");
            if (File.Exists(candidatePath)) {
                return Path.GetFullPath(candidatePath);
            }
        }

        string? vsWherePath = ResolveVsWherePath();
        if (!string.IsNullOrWhiteSpace(vsWherePath) && File.Exists(vsWherePath)) {
            string resolvedPath = QueryVsWhereForVsDevCmd(vsWherePath);
            if (!string.IsNullOrWhiteSpace(resolvedPath) && File.Exists(resolvedPath)) {
                return Path.GetFullPath(resolvedPath);
            }
        }

        string[] fallbackInstallRoots = [
            @"C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
            @"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
            @"C:\Program Files\Microsoft Visual Studio\2022\Community",
            @"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community",
            @"C:\Program Files\Microsoft Visual Studio\2022\Professional",
            @"C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional",
            @"C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
            @"C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise"
        ];

        for (int index = 0; index < fallbackInstallRoots.Length; index++) {
            string candidatePath = Path.Combine(fallbackInstallRoots[index], @"Common7\Tools\VsDevCmd.bat");
            if (File.Exists(candidatePath)) {
                return Path.GetFullPath(candidatePath);
            }
        }

        throw new InvalidOperationException(
            $"Unable to locate Visual Studio developer command prompt. Set '{VsDevCmdPathEnvironmentVariable}' or install Visual Studio Build Tools.");
    }

    /// <summary>
    /// Resolves the Visual Studio Installer helper path on the current machine.
    /// </summary>
    /// <returns>Absolute path to vswhere.exe when available; otherwise null.</returns>
    static string? ResolveVsWherePath() {
        string? programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        if (string.IsNullOrWhiteSpace(programFilesX86)) {
            return null;
        }

        string candidatePath = Path.Combine(programFilesX86, VsWhereRelativePath);
        return File.Exists(candidatePath) ? candidatePath : null;
    }

    /// <summary>
    /// Uses vswhere to discover the active Visual Studio installation's developer command prompt.
    /// </summary>
    /// <param name="vsWherePath">Absolute path to vswhere.exe.</param>
    /// <returns>Absolute path to VsDevCmd.bat when discovery succeeds; otherwise null.</returns>
    static string? QueryVsWhereForVsDevCmd(string vsWherePath) {
        ProcessStartInfo startInfo = new ProcessStartInfo {
            FileName = vsWherePath,
            Arguments = "-latest -products * -requires Microsoft.Component.MSBuild -property installationPath",
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };

        using Process process = Process.Start(startInfo) ?? throw new InvalidOperationException($"Failed to start '{vsWherePath}'.");
        string output = process.StandardOutput.ReadToEnd();
        string error = process.StandardError.ReadToEnd();
        process.WaitForExit();

        if (process.ExitCode != 0) {
            return null;
        }

        string installationPath = output.Trim();
        if (string.IsNullOrWhiteSpace(installationPath) && !string.IsNullOrWhiteSpace(error)) {
            installationPath = error.Trim();
        }

        if (string.IsNullOrWhiteSpace(installationPath)) {
            return null;
        }

        string candidatePath = Path.Combine(installationPath, @"Common7\Tools\VsDevCmd.bat");
        return File.Exists(candidatePath) ? candidatePath : null;
    }

    /// <summary>
    /// Runs one process and throws when it fails.
    /// </summary>
    static void RunProcess(string fileName, string arguments, string workingDirectory, string logPath, CancellationToken cancellationToken) {
        if (string.IsNullOrWhiteSpace(logPath)) {
            throw new ArgumentException("Log path must be provided.", nameof(logPath));
        }

        StringBuilder logBuilder = new();
        ProcessStartInfo startInfo = new ProcessStartInfo {
            FileName = fileName,
            Arguments = arguments,
            WorkingDirectory = workingDirectory,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };

        using Process process = Process.Start(startInfo) ?? throw new InvalidOperationException($"Failed to start '{fileName}'.");
        process.OutputDataReceived += (_, eventArgs) => {
            if (!string.IsNullOrEmpty(eventArgs.Data)) {
                logBuilder.AppendLine(eventArgs.Data);
            }
        };
        process.ErrorDataReceived += (_, eventArgs) => {
            if (!string.IsNullOrEmpty(eventArgs.Data)) {
                logBuilder.AppendLine(eventArgs.Data);
            }
        };
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        while (!process.HasExited) {
            cancellationToken.ThrowIfCancellationRequested();
            process.WaitForExit(100);
        }

        process.WaitForExit();
        Directory.CreateDirectory(Path.GetDirectoryName(logPath) ?? workingDirectory);
        File.WriteAllText(logPath, logBuilder.ToString());

        if (process.ExitCode != 0) {
            throw new InvalidOperationException($"Process '{fileName} {arguments}' failed with exit code {process.ExitCode}. See '{logPath}'.");
        }
    }
}
