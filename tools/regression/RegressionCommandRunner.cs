namespace helengine.windows.regression;

using System.Globalization;
using System.Text;

/// <summary>
/// Dispatches the regression tool's command-line interface: image comparison, golden recording,
/// stability checking, blank-frame detection, TRX failing-set comparison, TRX executed-count
/// recording and checking, host-fingerprint checking and comparison, and idle-scenario checking. Every
/// command prints one result line (plus, for compare-failing, check-fingerprint and compare-fingerprint,
/// one line per finding; check-idle prints one FAIL line per finding instead of its PASS line) and
/// returns the documented exit code. Any I/O or format error is reported as a
/// single "ERROR &lt;message&gt;" line with exit code 2.
/// </summary>
public sealed class RegressionCommandRunner {
    /// <summary>
    /// Runs a single regression command.
    /// </summary>
    /// <param name="args">Command-line arguments; args[0] selects the command.</param>
    /// <param name="output">Writer that receives the command's result line(s).</param>
    /// <returns>The command's exit code.</returns>
    public int Run(string[] args, TextWriter output) {
        try {
            if (args.Length == 0) {
                return PrintUsage(output);
            }

            return args[0] switch {
                "compare" => RunCompare(args, output),
                "record-golden" => RunRecordGolden(args, output),
                "stable" => RunStable(args, output),
                "blank-check" => RunBlankCheck(args, output),
                "trx-failing" => RunTrxFailing(args, output),
                "compare-failing" => RunCompareFailing(args, output),
                "record-executed" => RunRecordExecuted(args, output),
                "check-executed" => RunCheckExecuted(args, output),
                "check-fingerprint" => RunCheckFingerprint(args, output),
                "compare-fingerprint" => RunCompareFingerprint(args, output),
                "check-idle" => RunCheckIdle(args, output),
                _ => PrintUsage(output)
            };
        } catch (Exception exception) {
            output.WriteLine($"ERROR {exception.Message}");
            return 2;
        }
    }

    /// <summary>
    /// Runs "compare capture.bmp golden.png diff.png": compares the capture against the golden and
    /// writes the diff image only when the comparison fails.
    /// </summary>
    static int RunCompare(string[] args, TextWriter output) {
        if (args.Length != 4) {
            throw new ArgumentException("compare requires <capture.bmp> <golden.png> <diff.png>");
        }

        RegressionImage actual = BmpImageReader.Read(args[1]);
        RegressionImage expected = PngImageStore.Load(args[2]);
        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        if (!comparison.SizesMatch) {
            output.WriteLine($"FAIL size {actual.Width}x{actual.Height} vs {expected.Width}x{expected.Height}");
            return 1;
        }

        if (comparison.Passed) {
            output.WriteLine($"PASS {comparison.DifferingFraction.ToString(CultureInfo.InvariantCulture)}");
            return 0;
        }

        PngImageStore.Save(comparison.DiffImage, args[3]);
        output.WriteLine($"FAIL {comparison.DifferingFraction.ToString(CultureInfo.InvariantCulture)} {args[3]}");
        return 1;
    }

    /// <summary>
    /// Runs "record-golden capture.bmp golden.png": converts a capture into a golden PNG.
    /// </summary>
    static int RunRecordGolden(string[] args, TextWriter output) {
        if (args.Length != 3) {
            throw new ArgumentException("record-golden requires <capture.bmp> <golden.png>");
        }

        RegressionImage capture = BmpImageReader.Read(args[1]);
        PngImageStore.Save(capture, args[2]);
        output.WriteLine($"RECORDED {args[2]}");
        return 0;
    }

    /// <summary>
    /// Runs "stable captureA.bmp captureB.bmp": reports whether two captures of the same frame are
    /// pixel-stable within the regression tolerance.
    /// </summary>
    static int RunStable(string[] args, TextWriter output) {
        if (args.Length != 3) {
            throw new ArgumentException("stable requires <captureA.bmp> <captureB.bmp>");
        }

        RegressionImage captureA = BmpImageReader.Read(args[1]);
        RegressionImage captureB = BmpImageReader.Read(args[2]);
        ImageComparison comparison = ImageComparer.Compare(captureA, captureB);

        if (comparison.Passed) {
            output.WriteLine("STABLE");
            return 0;
        }

        output.WriteLine($"UNSTABLE {comparison.DifferingFraction.ToString(CultureInfo.InvariantCulture)}");
        return 1;
    }

    /// <summary>
    /// Runs "blank-check capture.bmp": reports whether a capture is a blank (stuck) frame.
    /// </summary>
    static int RunBlankCheck(string[] args, TextWriter output) {
        if (args.Length != 2) {
            throw new ArgumentException("blank-check requires <capture.bmp>");
        }

        RegressionImage image = BmpImageReader.Read(args[1]);
        if (BlankFrameDetector.IsBlank(image)) {
            output.WriteLine("BLANK");
            return 1;
        }

        output.WriteLine("NOT_BLANK");
        return 0;
    }

    /// <summary>
    /// Runs "trx-failing result.trx out.txt": writes the sorted failed test names, one per line
    /// with "\n" line endings, to the output file.
    /// </summary>
    static int RunTrxFailing(string[] args, TextWriter output) {
        if (args.Length != 3) {
            throw new ArgumentException("trx-failing requires <result.trx> <out.txt>");
        }

        IReadOnlyList<string> failing = TrxFailingTestReader.Read(args[1]);
        StringBuilder builder = new();
        foreach (string name in failing) {
            builder.Append(name).Append('\n');
        }

        File.WriteAllText(args[2], builder.ToString());
        output.WriteLine($"WROTE {failing.Count}");
        return 0;
    }

    /// <summary>
    /// Runs "compare-failing baseline.txt current.txt [flaky.txt]": compares two failing-name lists
    /// and reports whether any new failures were introduced. When the optional known-flaky list is
    /// given, a new failure on that list prints "WARN flaky &lt;name&gt;" and does not fail the command.
    /// </summary>
    static int RunCompareFailing(string[] args, TextWriter output) {
        if (args.Length != 3 && args.Length != 4) {
            throw new ArgumentException("compare-failing requires <baseline.txt> <current.txt> [<flaky.txt>]");
        }

        IReadOnlyList<string> baseline = ReadFailingNames(args[1]);
        IReadOnlyList<string> current = ReadFailingNames(args[2]);
        IReadOnlyList<string> flaky = args.Length == 4 ? ReadFailingNames(args[3]) : Array.Empty<string>();
        FailingSetComparison comparison = FailingSetComparer.Compare(baseline, current, flaky);

        if (comparison.Passed) {
            output.WriteLine("PASS");
        } else {
            output.WriteLine("FAIL");
            foreach (string newName in comparison.NewFailures) {
                output.WriteLine($"NEW {newName}");
            }
        }

        foreach (string flakyName in comparison.FlakyFailures) {
            output.WriteLine($"WARN flaky {flakyName}");
        }

        foreach (string fixedName in comparison.FixedFailures) {
            output.WriteLine($"FIXED {fixedName}");
        }

        return comparison.Passed ? 0 : 1;
    }

    /// <summary>
    /// Runs "record-executed result.trx executed.txt": writes the run's executed-test count as a
    /// single integer followed by "\n". An Error or Aborted run prints "FAIL outcome &lt;outcome&gt;",
    /// writes nothing and returns 1, so an aborted run never becomes a baseline.
    /// </summary>
    static int RunRecordExecuted(string[] args, TextWriter output) {
        if (args.Length != 3) {
            throw new ArgumentException("record-executed requires <result.trx> <executed.txt>");
        }

        TrxResultSummary summary = TrxResultSummaryReader.Read(args[1]);
        if (!TrxExecutedCountChecker.IsRecordable(summary)) {
            output.WriteLine($"FAIL outcome {summary.Outcome}");
            return 1;
        }

        File.WriteAllText(args[2], summary.Executed.ToString(CultureInfo.InvariantCulture) + "\n");
        output.WriteLine($"RECORDED {summary.Executed}");
        return 0;
    }

    /// <summary>
    /// Runs "check-executed result.trx executed.txt": prints "PASS|WARN|FAIL &lt;detail&gt;" for the
    /// run's outcome and executed count against the recorded count, and returns 1 only on FAIL.
    /// </summary>
    static int RunCheckExecuted(string[] args, TextWriter output) {
        if (args.Length != 3) {
            throw new ArgumentException("check-executed requires <result.trx> <executed.txt>");
        }

        TrxResultSummary summary = TrxResultSummaryReader.Read(args[1]);
        string recordedText = File.ReadAllText(args[2]).Trim();
        if (!int.TryParse(recordedText, NumberStyles.None, CultureInfo.InvariantCulture, out int recordedExecuted)) {
            throw new InvalidDataException($"Executed-count baseline must hold one whole number, got '{recordedText}': {args[2]}");
        }

        TrxExecutedCountCheck check = TrxExecutedCountChecker.Check(summary, recordedExecuted);
        string status = check.Status switch {
            TrxExecutedCountCheckStatus.Pass => "PASS",
            TrxExecutedCountCheckStatus.Warn => "WARN",
            _ => "FAIL"
        };
        output.WriteLine($"{status} {check.Detail}");
        return check.Status == TrxExecutedCountCheckStatus.Fail ? 1 : 0;
    }

    /// <summary>
    /// Runs "check-fingerprint scene line": checks one run's fingerprint on its own (at least one
    /// Present per frame, no Present failures). Prints "PASS", or "FAIL" followed by one
    /// "FAIL &lt;finding&gt;" line per failed check and returns 1.
    /// </summary>
    static int RunCheckFingerprint(string[] args, TextWriter output) {
        if (args.Length != 3) {
            throw new ArgumentException("check-fingerprint requires <scene> <fingerprint line>");
        }

        IReadOnlyList<string> failures = HostFingerprintComparer.CheckHealth(args[1], HostFingerprint.Parse(args[2]));
        output.WriteLine(failures.Count == 0 ? "PASS" : "FAIL");
        foreach (string failure in failures) {
            output.WriteLine($"FAIL {failure}");
        }

        return failures.Count == 0 ? 0 : 1;
    }

    /// <summary>
    /// Runs "compare-fingerprint scene recorded-line actual-line": compares a run's fingerprint
    /// with the recorded one. Prints "PASS" or "FAIL", then one "FAIL &lt;finding&gt;" line per failed
    /// check and one "WARN &lt;finding&gt;" line per pacing warning. Returns 1 only when a check failed;
    /// a pacing warning alone returns 0.
    /// </summary>
    static int RunCompareFingerprint(string[] args, TextWriter output) {
        if (args.Length != 4) {
            throw new ArgumentException("compare-fingerprint requires <scene> <recorded fingerprint line> <actual fingerprint line>");
        }

        HostFingerprintComparison comparison = HostFingerprintComparer.Compare(args[1], HostFingerprint.Parse(args[2]), HostFingerprint.Parse(args[3]));
        output.WriteLine(comparison.Passed ? "PASS" : "FAIL");
        foreach (string failure in comparison.Failures) {
            output.WriteLine($"FAIL {failure}");
        }

        foreach (string warning in comparison.Warnings) {
            output.WriteLine($"WARN {warning}");
        }

        return comparison.Passed ? 0 : 1;
    }

    /// <summary>
    /// Runs "check-idle line minIdleFrames minElapsedMs": checks an idle-scenario run's fingerprint (idle throttle on, no
    /// Present failures, at least minIdleFrames idle frames, at least minElapsedMs elapsed). Prints
    /// "PASS idleFrames=&lt;n&gt; elapsedMs=&lt;n&gt;" and returns 0, or prints one "FAIL &lt;reason&gt;" line per failed check
    /// and returns 1.
    /// </summary>
    static int RunCheckIdle(string[] args, TextWriter output) {
        if (args.Length != 4) {
            throw new ArgumentException("check-idle requires <fingerprint line> <minIdleFrames> <minElapsedMs>");
        }

        HostFingerprint fingerprint = HostFingerprint.Parse(args[1]);
        long minimumIdleFrames = ParseWholeNumberArgument("minIdleFrames", args[2]);
        long minimumElapsedMilliseconds = ParseWholeNumberArgument("minElapsedMs", args[3]);
        IReadOnlyList<string> failures = IdleScenarioChecker.Check(fingerprint, minimumIdleFrames, minimumElapsedMilliseconds);
        if (failures.Count == 0) {
            output.WriteLine($"PASS idleFrames={fingerprint.IdleFrames} elapsedMs={fingerprint.ElapsedMilliseconds}");
            return 0;
        }

        foreach (string failure in failures) {
            output.WriteLine($"FAIL {failure}");
        }

        return 1;
    }

    /// <summary>
    /// Parses a command-line argument that must be a whole non-negative number, throwing <see cref="FormatException"/>
    /// that names the argument otherwise.
    /// </summary>
    static long ParseWholeNumberArgument(string argumentName, string value) {
        if (!long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long number)) {
            throw new FormatException($"{argumentName} must be a whole number, got '{value}'.");
        }

        return number;
    }

    /// <summary>
    /// Reads non-empty test names from a failing-set file, tolerant of both "\n" and "\r\n" line
    /// endings.
    /// </summary>
    static IReadOnlyList<string> ReadFailingNames(string path) {
        return File.ReadAllLines(path).Where(line => !string.IsNullOrEmpty(line)).ToList();
    }

    /// <summary>
    /// Prints the CLI usage line for an empty or unrecognized command.
    /// </summary>
    static int PrintUsage(TextWriter output) {
        output.WriteLine("USAGE regression <compare|record-golden|stable|blank-check|trx-failing|compare-failing|record-executed|check-executed|check-fingerprint|compare-fingerprint|check-idle> ...");
        return 2;
    }
}
