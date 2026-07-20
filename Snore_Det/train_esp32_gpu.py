"""CUDA training and static ONNX export for the ESP32-S3 model."""

import argparse
import pickle
import random
from pathlib import Path

import numpy as np
import onnx
import torch
from sklearn.model_selection import train_test_split
from torch import nn
from torch.optim import AdamW
from torch.optim.lr_scheduler import CosineAnnealingLR
from torch.utils.data import DataLoader, Dataset
from tqdm import tqdm

from net.esp32_model import Esp32SnoringNet


class FeatureDataset(Dataset):
    def __init__(self, files: list[Path], labels: list[int]) -> None:
        self.files = files
        self.labels = labels

    def __len__(self) -> int:
        return len(self.files)

    def __getitem__(self, index: int) -> tuple[torch.Tensor, torch.Tensor]:
        with self.files[index].open("rb") as feature_file:
            feature = np.asarray(pickle.load(feature_file), dtype=np.float32)

        if feature.shape != (64, 64):
            raise ValueError(f"Expected a 64x64 feature, got {feature.shape}: {self.files[index]}")

        return torch.from_numpy(feature).unsqueeze(0), torch.tensor(self.labels[index])


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=repo_root / "Snoring_Dataset" / "fea")
    parser.add_argument("--output-dir", type=Path, default=repo_root / "artifacts" / "esp32")
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--max-samples", type=int, default=0)
    parser.add_argument("--learning-rate", type=float, default=3e-4)
    parser.add_argument("--seed", type=int, default=42)
    return parser.parse_args()


def discover_features(data_root: Path) -> tuple[list[Path], list[int]]:
    files: list[Path] = []
    labels: list[int] = []
    for label in (0, 1):
        class_files = sorted((data_root / str(label)).glob("*.fea"))
        files.extend(class_files)
        labels.extend([label] * len(class_files))

    if len(files) == 0:
        raise FileNotFoundError(f"No .fea files found under {data_root}")
    if len(set(labels)) != 2:
        raise ValueError("Both class directories (0 and 1) must contain feature files")
    return files, labels


@torch.no_grad()
def evaluate(model: nn.Module, loader: DataLoader, device: torch.device) -> float:
    model.eval()
    correct = 0
    total = 0
    for features, labels in loader:
        logits = model(features.to(device, non_blocking=True))
        correct += (logits.argmax(dim=1).cpu() == labels).sum().item()
        total += labels.numel()
    return correct / total


def export_onnx(model: nn.Module, output_path: Path) -> None:
    model = model.cpu().eval()
    example = torch.zeros(1, 1, 64, 64, dtype=torch.float32)
    torch.onnx.export(
        model,
        example,
        output_path,
        input_names=["log_mel"],
        output_names=["logits"],
        opset_version=13,
        do_constant_folding=True,
    )
    onnx.checker.check_model(onnx.load(output_path))


def main() -> None:
    args = parse_args()
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is required for this training entry point")

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.cuda.manual_seed_all(args.seed)
    torch.backends.cudnn.benchmark = True
    device = torch.device("cuda:0")

    files, labels = discover_features(args.data_root)
    if args.max_samples > 0 and args.max_samples < len(files):
        indices = list(range(len(files)))
        random.Random(args.seed).shuffle(indices)
        indices = indices[:args.max_samples]
        files = [files[index] for index in indices]
        labels = [labels[index] for index in indices]

    train_files, valid_files, train_labels, valid_labels = train_test_split(
        files,
        labels,
        test_size=0.2,
        random_state=args.seed,
        stratify=labels,
    )
    pin_memory = True
    train_loader = DataLoader(
        FeatureDataset(train_files, train_labels),
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=args.workers,
        pin_memory=pin_memory,
        persistent_workers=args.workers > 0,
    )
    valid_loader = DataLoader(
        FeatureDataset(valid_files, valid_labels),
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.workers,
        pin_memory=pin_memory,
        persistent_workers=args.workers > 0,
    )

    model = Esp32SnoringNet().to(device)
    optimizer = AdamW(model.parameters(), lr=args.learning_rate, weight_decay=1e-4)
    scheduler = CosineAnnealingLR(optimizer, T_max=args.epochs, eta_min=args.learning_rate * 0.05)
    criterion = nn.CrossEntropyLoss()
    scaler = torch.cuda.amp.GradScaler(enabled=True)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    checkpoint_path = args.output_dir / "snoring_esp32_float.pt"
    best_accuracy = -1.0
    for epoch in range(1, args.epochs + 1):
        model.train()
        loss_total = 0.0
        progress = tqdm(train_loader, desc=f"epoch {epoch}/{args.epochs}")
        for features, targets in progress:
            features = features.to(device, non_blocking=True)
            targets = targets.to(device, non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            with torch.cuda.amp.autocast(enabled=True):
                logits = model(features)
                loss = criterion(logits, targets)
            scaler.scale(loss).backward()
            scaler.step(optimizer)
            scaler.update()
            loss_total += loss.item()
            progress.set_postfix(loss=f"{loss.item():.4f}")

        scheduler.step()
        accuracy = evaluate(model, valid_loader, device)
        print(f"epoch={epoch} loss={loss_total / len(train_loader):.4f} valid_accuracy={accuracy:.4f}")
        if accuracy > best_accuracy:
            best_accuracy = accuracy
            torch.save(
                {
                    "model_state": model.state_dict(),
                    "accuracy": accuracy,
                    "epoch": epoch,
                    "input_shape": [1, 1, 64, 64],
                },
                checkpoint_path,
            )

    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["model_state"])
    onnx_path = args.output_dir / "snoring_esp32_float.onnx"
    export_onnx(model, onnx_path)
    print(f"best_accuracy={checkpoint['accuracy']:.4f}")
    print(f"checkpoint={checkpoint_path}")
    print(f"onnx={onnx_path}")


if __name__ == "__main__":
    main()
