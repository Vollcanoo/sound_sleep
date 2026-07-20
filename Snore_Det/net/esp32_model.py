"""Small INT8-friendly network for ESP32-S3 deployment."""

import torch
from torch import nn


class DepthwiseSeparableBlock(nn.Module):
    """Use depthwise and pointwise convolutions supported by embedded runtimes."""

    def __init__(self, in_channels: int, out_channels: int) -> None:
        super().__init__()
        self.depthwise = nn.Conv2d(
            in_channels,
            in_channels,
            kernel_size=3,
            padding=1,
            groups=in_channels,
            bias=False,
        )
        self.pointwise = nn.Conv2d(in_channels, out_channels, kernel_size=1, bias=True)
        self.relu = nn.ReLU(inplace=False)
        self.pool = nn.MaxPool2d(kernel_size=2, stride=2)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.depthwise(x)
        x = self.pointwise(x)
        x = self.relu(x)
        return self.pool(x)


class Esp32SnoringNet(nn.Module):
    """Classify one 64x64 log-Mel feature map into snore or non-snore logits."""

    def __init__(self) -> None:
        super().__init__()
        self.stem = nn.Sequential(
            nn.Conv2d(1, 16, kernel_size=3, padding=1, bias=True),
            nn.ReLU(inplace=False),
            nn.MaxPool2d(kernel_size=2, stride=2),
        )
        self.block1 = DepthwiseSeparableBlock(16, 24)
        self.block2 = DepthwiseSeparableBlock(24, 32)
        self.block3 = DepthwiseSeparableBlock(32, 48)
        self.block4 = DepthwiseSeparableBlock(48, 64)
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(64 * 2 * 2, 64),
            nn.ReLU(inplace=False),
            nn.Linear(64, 2),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.stem(x)
        x = self.block1(x)
        x = self.block2(x)
        x = self.block3(x)
        x = self.block4(x)
        return self.classifier(x)
