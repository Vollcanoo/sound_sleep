"""Convert the GPU-trained ONNX model into an ESP-DL INT8 model for ESP32-S3."""

import argparse
import pickle
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader, Dataset


class CalibrationDataset(Dataset):
    def __init__(self, feature_root: Path, max_samples: int) -> None:
        files = sorted(feature_root.glob("0/*.fea")) + sorted(feature_root.glob("1/*.fea"))
        self.files = files[:max_samples]
        if not self.files:
            raise FileNotFoundError(f"No .fea files found in {feature_root}")

    def __len__(self) -> int:
        return len(self.files)

    def __getitem__(self, index: int) -> torch.Tensor:
        with self.files[index].open("rb") as feature_file:
            feature = np.asarray(pickle.load(feature_file), dtype=np.float32)
        if feature.shape != (64, 64):
            raise ValueError(f"Expected a 64x64 feature, got {feature.shape}: {self.files[index]}")
        return torch.from_numpy(feature).unsqueeze(0)


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--onnx",
        type=Path,
        default=repo_root / "artifacts" / "esp32" / "snoring_esp32_float.onnx",
    )
    parser.add_argument(
        "--feature-root",
        type=Path,
        default=Path.home() / "snoring_data" / "fea",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root / "artifacts" / "esp32" / "snoring_esp32_int8.espdl",
    )
    parser.add_argument("--calibration-samples", type=int, default=256)
    return parser.parse_args()


def main() -> None:
    from esp_ppq.api import espdl_quantize_onnx

    args = parse_args()
    if not args.onnx.is_file():
        raise FileNotFoundError(f"ONNX model not found: {args.onnx}")
    args.output.parent.mkdir(parents=True, exist_ok=True)

    dataset = CalibrationDataset(args.feature_root, args.calibration_samples)
    loader = DataLoader(dataset, batch_size=1, shuffle=False)

    def collate_fn(batch: torch.Tensor) -> torch.Tensor:
        return batch

    espdl_quantize_onnx(
        onnx_import_file=str(args.onnx),
        espdl_export_file=str(args.output),
        calib_dataloader=loader,
        calib_steps=min(len(dataset), args.calibration_samples),
        input_shape=[1, 1, 64, 64],
        inputs=None,
        target="esp32s3",
        num_of_bits=8,
        collate_fn=collate_fn,
        device="cuda" if torch.cuda.is_available() else "cpu",
        error_report=True,
        skip_export=False,
        export_test_values=True,
        verbose=1,
    )
    print(f"ESP-DL model: {args.output}")


if __name__ == "__main__":
    main()
