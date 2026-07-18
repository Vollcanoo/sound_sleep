/// 姿态类型，对应 ESP32-S3 Posture_Recognition 输出
enum PostureType {
  noHead,     // NO_HEAD — 无人在枕
  moving,     // MOVING — 翻身中
  leftSide,   // LEFT_SIDE
  rightSide,  // RIGHT_SIDE
  supine,     // SUPINE — 仰卧
  prone,      // PRONE — 俯卧
  uncertain;  // UNCERTAIN

  /// 从 ESP32 CSV 的 posture 字段解析
  static PostureType fromString(String s) {
    switch (s.trim().toUpperCase()) {
      case 'NO_HEAD':
        return PostureType.noHead;
      case 'MOVING':
        return PostureType.moving;
      case 'LEFT_SIDE':
        return PostureType.leftSide;
      case 'RIGHT_SIDE':
        return PostureType.rightSide;
      case 'SUPINE':
        return PostureType.supine;
      case 'PRONE':
        return PostureType.prone;
      case 'UNCERTAIN':
      default:
        return PostureType.uncertain;
    }
  }

  /// 中文显示标签
  String get label {
    switch (this) {
      case PostureType.noHead:
        return '无人';
      case PostureType.moving:
        return '翻身';
      case PostureType.leftSide:
        return '左侧卧';
      case PostureType.rightSide:
        return '右侧卧';
      case PostureType.supine:
        return '仰卧';
      case PostureType.prone:
        return '俯卧';
      case PostureType.uncertain:
        return '未知';
    }
  }

  /// UI 图标建议
  String get emoji {
    switch (this) {
      case PostureType.noHead:
        return '🚫';
      case PostureType.moving:
        return '🔄';
      case PostureType.leftSide:
        return '⬅️';
      case PostureType.rightSide:
        return '➡️';
      case PostureType.supine:
        return '🔼';
      case PostureType.prone:
        return '🔽';
      case PostureType.uncertain:
        return '❓';
    }
  }
}

/// 单条姿态读数，来自 ESP32 压力传感器（1Hz 采样）
///
/// CSV 列顺序：
///   raw_left, raw_center, raw_right,
///   median_pressure_left, median_pressure_center, median_pressure_right,
///   total_pressure,
///   left_ratio, center_ratio, right_ratio,
///   x_center_cm, y_center_cm,
///   moving, posture, confidence
class PostureReading {
  final DateTime timestamp;
  final int rawLeft, rawCenter, rawRight;
  final double medianLeft, medianCenter, medianRight;
  final double totalPressure;
  final double leftRatio, centerRatio, rightRatio;
  final double xCenterCm, yCenterCm;
  final bool isMoving;
  final PostureType posture;
  final double confidence;

  PostureReading({
    required this.timestamp,
    required this.rawLeft,
    required this.rawCenter,
    required this.rawRight,
    required this.medianLeft,
    required this.medianCenter,
    required this.medianRight,
    required this.totalPressure,
    required this.leftRatio,
    required this.centerRatio,
    required this.rightRatio,
    required this.xCenterCm,
    required this.yCenterCm,
    required this.isMoving,
    required this.posture,
    required this.confidence,
  });

  /// 从一行 ESP32 CSV 输出解析
  ///
  /// [timestamp] 可选，不传则使用当前时间。
  /// CSV 中 moving 字段为 "1"/"true" 表示正在移动。
  factory PostureReading.fromCsv(String csvLine, {DateTime? timestamp}) {
    final parts = csvLine.split(',');
    if (parts.length < 15) {
      throw FormatException('CSV 列数不足，期望 15 列，实际 ${parts.length}');
    }
    return PostureReading(
      timestamp: timestamp ?? DateTime.now(),
      rawLeft: int.tryParse(parts[0].trim()) ?? 0,
      rawCenter: int.tryParse(parts[1].trim()) ?? 0,
      rawRight: int.tryParse(parts[2].trim()) ?? 0,
      medianLeft: double.tryParse(parts[3].trim()) ?? 0.0,
      medianCenter: double.tryParse(parts[4].trim()) ?? 0.0,
      medianRight: double.tryParse(parts[5].trim()) ?? 0.0,
      totalPressure: double.tryParse(parts[6].trim()) ?? 0.0,
      leftRatio: double.tryParse(parts[7].trim()) ?? 0.0,
      centerRatio: double.tryParse(parts[8].trim()) ?? 0.0,
      rightRatio: double.tryParse(parts[9].trim()) ?? 0.0,
      xCenterCm: double.tryParse(parts[10].trim()) ?? 0.0,
      yCenterCm: double.tryParse(parts[11].trim()) ?? 0.0,
      isMoving: parts[12].trim() == '1' || parts[12].trim().toLowerCase() == 'true',
      posture: PostureType.fromString(parts[13].trim()),
      confidence: double.tryParse(parts[14].trim()) ?? 0.0,
    );
  }

  /// 是否有人在床上（totalPressure > 阈值）
  bool get isOnBed => totalPressure > 180;
}

/// 一段姿态保持不变的区间
class PostureSegment {
  final DateTime startTime;
  final DateTime endTime;
  final PostureType posture;
  final double avgConfidence;

  PostureSegment({
    required this.startTime,
    required this.endTime,
    required this.posture,
    required this.avgConfidence,
  });

  int get durationMinutes => endTime.difference(startTime).inMinutes;
}
