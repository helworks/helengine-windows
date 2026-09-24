namespace helengine.windows.regression;

using System.Globalization;
using System.Text;

/// <summary>
/// Dispatches the regression tool's command-line interface: image comparison, golden recording,
/// stability checking, blank-frame detection and TRX failing-set comparison. Every command prints
/// exactly one result line (plus, for compare-failing, one line per new or fixed failure) and
/// returns the documented exit code. Any I/O or format error is reported as a single
/// "ERROR &lt;message&gt;" line with exit code 2.
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
    /// Runs "compare-failing baseline.txt current.txt": compares two failing-name lists and
    /// reports whether any new failures were introduced.
    /// </summary>
    static int RunCompareFailing(string[] args, TextWriter output) {
        if (args.Length != 3) {
            throw new ArgumentException("compare-failing requires <baseline.txt> <current.txt>");
        }

        IReadOnlyList<string> baseline = ReadFailingNames(args[1]);
        IReadOnlyList<string> current = ReadFailingNames(args[2]);
        FailingSetComparison comparison = FailingSetComparer.Compare(baseline, current);

        if (comparison.Passed) {
            output.WriteLine("PASS");
        } else {
            output.WriteLine("FAIL");
            foreach (string newName in comparison.NewFailures) {
                output.WriteLine($"NEW {newName}");
            }
        }

        foreach (string fixedName in comparison.FixedFailures) {
            output.WriteLine($"FIXED {fixedName}");
        }

        return comparison.Passed ? 0 : 1;
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
        output.WriteLine("USAGE regression <compare|record-golden|stable|blank-check|trx-failing|compare-failing> ...");
        return 2;
    }
}
