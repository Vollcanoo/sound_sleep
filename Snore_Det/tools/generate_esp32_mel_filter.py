"""Generate the librosa-compatible Mel filter table used by the ESP32 frontend."""

from pathlib import Path

import librosa


SAMPLE_RATE = 16_000
FFT_SIZE = 1_024
MEL_BINS = 64
FMIN = 20
FMAX = 8_000


def format_float(value: float) -> str:
    """Format a Python float as a valid C++ single-precision literal."""
    text = f"{value:.9g}"
    if "." not in text and "e" not in text and "E" not in text:
        text += ".0"
    return f"{text}F"


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    output_path = repo_root / "esp32s3" / "main" / "mel_filter_data.h"
    filters = librosa.filters.mel(
        sr=SAMPLE_RATE,
        n_fft=FFT_SIZE,
        n_mels=MEL_BINS,
        fmin=FMIN,
        fmax=FMAX,
    )

    lines = [
        "#pragma once",
        "",
        "namespace snore {",
        f"static constexpr float kMelFilter[{MEL_BINS}][{FFT_SIZE // 2 + 1}] = {{",
    ]
    for row in filters:
        values = ", ".join(format_float(value) for value in row)
        lines.append(f"    {{{values}}},")
    lines.extend(["};", "}  // namespace snore", ""])
    output_path.write_text("\n".join(lines), encoding="ascii")
    print(output_path)


if __name__ == "__main__":
    main()
