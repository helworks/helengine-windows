namespace helengine.windows.regression;

using System.Globalization;

/// <summary>
/// The host-layer facts of one player run, as written by the player's HOST_FINGERPRINT startup-log line in --frames
/// mode: the swap-chain description, the window styles, the client size, the Present count and failures, the frame
/// count, whether the idle throttle was enabled and how many frames ran idle and active, and the wall-clock time from
/// the first to the last Present. Every field except the elapsed time is compared exactly between a record and a verify
/// run; the elapsed time is only a coarse pacing signal.
/// </summary>
public sealed class HostFingerprint {
    /// <summary>
    /// The marker that starts the player's fingerprint line.
    /// </summary>
    public const string Marker = "HOST_FINGERPRINT";

    /// <summary>
    /// The name of the wall-clock field, which is kept apart from the compared fields.
    /// </summary>
    public const string ElapsedFieldName = "elapsedMs";

    /// <summary>
    /// The names of the fields that are compared exactly, in the order the player writes them.
    /// </summary>
    public static readonly IReadOnlyList<string> ComparedFieldNames = new[] {
        "format", "alpha", "swapEffect", "buffers", "scaling", "style", "exStyle", "client", "presentCount", "presentFailures", "frames",
        "idleThrottle", "idleFrames", "activeFrames"
    };

    /// <summary>
    /// The compared fields whose values must be whole non-negative numbers, because rules compare them numerically.
    /// </summary>
    static readonly IReadOnlyList<string> CounterFieldNames = new[] { "presentCount", "presentFailures", "frames", "idleFrames", "activeFrames" };

    /// <summary>
    /// Initializes a fingerprint from its compared fields and its elapsed time.
    /// </summary>
    /// <param name="fields">The compared fields by name; must contain exactly <see cref="ComparedFieldNames"/>.</param>
    /// <param name="elapsedMilliseconds">Milliseconds from the first to the last Present of the run.</param>
    public HostFingerprint(IReadOnlyDictionary<string, string> fields, long elapsedMilliseconds) {
        Fields = fields;
        ElapsedMilliseconds = elapsedMilliseconds;
    }

    /// <summary>
    /// Gets the compared fields by name, exactly <see cref="ComparedFieldNames"/>, with the values as the player wrote them.
    /// </summary>
    public IReadOnlyDictionary<string, string> Fields { get; }

    /// <summary>
    /// Gets the milliseconds from the first to the last Present of the run.
    /// </summary>
    public long ElapsedMilliseconds { get; }

    /// <summary>
    /// Gets the number of Present calls the swap chain reports.
    /// </summary>
    public long PresentCount => long.Parse(Fields["presentCount"], CultureInfo.InvariantCulture);

    /// <summary>
    /// Gets the number of Present calls that returned a failing HRESULT.
    /// </summary>
    public long PresentFailures => long.Parse(Fields["presentFailures"], CultureInfo.InvariantCulture);

    /// <summary>
    /// Gets the number of frames the run rendered.
    /// </summary>
    public long Frames => long.Parse(Fields["frames"], CultureInfo.InvariantCulture);

    /// <summary>
    /// Gets whether the player's idle throttle was enabled for the run, as written by the player: "on" or "off".
    /// </summary>
    public string IdleThrottle => Fields["idleThrottle"];

    /// <summary>
    /// Gets the number of frames the run rendered while the idle throttle considered the window idle.
    /// </summary>
    public long IdleFrames => long.Parse(Fields["idleFrames"], CultureInfo.InvariantCulture);

    /// <summary>
    /// Parses a fingerprint from space-separated "name=value" tokens, optionally preceded by the HOST_FINGERPRINT
    /// marker, in any order. Every compared field and the elapsed time must appear exactly once; unknown, repeated or
    /// malformed tokens and non-numeric counters throw <see cref="FormatException"/>.
    /// </summary>
    /// <param name="line">The fingerprint line, as the player logged it or as rebuilt from a manifest.</param>
    /// <returns>The parsed fingerprint.</returns>
    public static HostFingerprint Parse(string line) {
        Dictionary<string, string> fields = new(StringComparer.Ordinal);
        string elapsedText = null;
        string[] tokens = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        for (int tokenIndex = 0; tokenIndex < tokens.Length; tokenIndex++) {
            string token = tokens[tokenIndex];
            if (tokenIndex == 0 && string.Equals(token, Marker, StringComparison.Ordinal)) {
                continue;
            }

            int separatorIndex = token.IndexOf('=');
            if (separatorIndex <= 0) {
                throw new FormatException($"Host fingerprint token '{token}' is not name=value.");
            }

            string name = token.Substring(0, separatorIndex);
            string value = token.Substring(separatorIndex + 1);
            if (string.Equals(name, ElapsedFieldName, StringComparison.Ordinal)) {
                if (elapsedText != null) {
                    throw new FormatException($"Host fingerprint field '{name}' appears more than once.");
                }

                elapsedText = value;
            } else if (!ComparedFieldNames.Contains(name)) {
                throw new FormatException($"Host fingerprint field '{name}' is unknown.");
            } else if (!fields.TryAdd(name, value)) {
                throw new FormatException($"Host fingerprint field '{name}' appears more than once.");
            }
        }

        foreach (string fieldName in ComparedFieldNames) {
            if (!fields.ContainsKey(fieldName)) {
                throw new FormatException($"Host fingerprint field '{fieldName}' is missing.");
            }
        }

        if (elapsedText == null) {
            throw new FormatException($"Host fingerprint field '{ElapsedFieldName}' is missing.");
        }

        foreach (string counterFieldName in CounterFieldNames) {
            RequireWholeNumber(counterFieldName, fields[counterFieldName]);
        }

        return new HostFingerprint(fields, RequireWholeNumber(ElapsedFieldName, elapsedText));
    }

    /// <summary>
    /// Returns the value as a whole non-negative number, or throws <see cref="FormatException"/> naming the field.
    /// </summary>
    static long RequireWholeNumber(string fieldName, string value) {
        if (!long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long number)) {
            throw new FormatException($"Host fingerprint field '{fieldName}' must be a whole number, got '{value}'.");
        }

        return number;
    }
}
