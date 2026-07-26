import 'dart:math';

import '../models/sleep_record.dart';
import '../models/posture_event.dart';
import '../models/snore_result.dart';

/// 将真实传感器数据转换为 SleepRecord
///
/// 数据流：
/// 1. PostureReading 流（来自 ESP32 BLE，1Hz）→ 检测上床/醒来/起身转换
/// 2. SnoreResult（来自云端 API）→ 转换为 SnoringEvent 列表
/// 3. 合并为完整的 SleepRecord
class SleepRecordGenerator {
  /// 压力阈值：判断"人在床上"
  /// 与 ESP32 Posture_Recognition 一致：total_pressure > 180 = 头在枕上
  static const double onBedThreshold = 180.0;

  /// 最小间隔（秒）：短于此值的压力消失视为翻身/噪声，不计为起身
  static const int minGetUpGapSeconds = 60;

  /// 从累积的姿态读数和打鼾结果生成 SleepRecord
  ///
  /// [readings] - BleDataService 累积的 PostureReading 列表（1Hz 数据）
  /// [snoreResult] - 可选的 SnoreApiService 返回的 SnoreResult
  /// [userId] - 当前用户 ID
  static SleepRecord generate({
    required List<PostureReading> readings,
    required String userId,
    SnoreResult? snoreResult,
    List<SnoringEvent>? realtimeSnoringEvents,
  }) {
    if (readings.isEmpty) {
      throw ArgumentError('readings 不能为空');
    }

    // 按时间排序，确保顺序正确
    final sorted = List<PostureReading>.from(readings)
      ..sort((a, b) => a.timestamp.compareTo(b.timestamp));

    // 1. 提取压力段（人在床上的连续时间片段）
    final pressureSegments = _extractPressureSegments(sorted);

    // 2. 确定上床/起床时间
    final DateTime bedTime;
    final DateTime wakeTime;
    if (pressureSegments.isNotEmpty) {
      bedTime = pressureSegments.first.startTime;
      wakeTime = pressureSegments.last.endTime;
    } else {
      // 没有检测到在床时段，使用首尾读数时间
      bedTime = sorted.first.timestamp;
      wakeTime = sorted.last.timestamp;
    }

    // 3. 夜间起身次数 = 压力段之间的间隙数
    final getUpCount = pressureSegments.isEmpty
        ? 0
        : pressureSegments.length - 1;

    // 4. 提取姿态段
    final postureSegments = _extractPostureSegments(sorted);

    // 5. 转换打鼾事件
    final snoringEvents =
        realtimeSnoringEvents ?? _convertSnoreEvents(snoreResult, bedTime);

    // 6. 计算离床总时长
    final totalDurationMin = wakeTime.difference(bedTime).inMinutes;
    final actualSleepMin = pressureSegments.fold(
      0,
      (sum, seg) => sum + seg.durationMinutes,
    );
    final awayMin = totalDurationMin - actualSleepMin;

    // 7. 计算睡眠评分
    final sleepScore = _calculateSleepScore(
      durationMinutes: totalDurationMin,
      getUpCount: getUpCount,
      awayMinutes: awayMin,
      snoringEvents: snoringEvents,
      postureSegments: postureSegments,
    );

    // 8. 组装 SleepRecord
    final date = DateTime(bedTime.year, bedTime.month, bedTime.day);
    return SleepRecord(
      id: 'sleep_${bedTime.millisecondsSinceEpoch}',
      userId: userId,
      date: date,
      bedTime: bedTime,
      wakeTime: wakeTime,
      sleepScore: sleepScore,
      snoringEvents: snoringEvents,
      pressureSegments: pressureSegments,
      postureSegments: postureSegments,
      getUpCount: getUpCount,
    );
  }

  /// 从读数中提取压力段
  ///
  /// 一个压力段 = totalPressure > 阈值的连续时间区间。
  /// 间隙短于 [minGetUpGapSeconds] 的合并（翻身/噪声）。
  static List<PressureSegment> _extractPressureSegments(
    List<PostureReading> readings,
  ) {
    if (readings.isEmpty) return [];

    // 第一步：找出所有连续在床区间
    final rawSegments = <PressureSegment>[];
    DateTime? segStart;

    for (final r in readings) {
      if (r.isOnBed) {
        segStart ??= r.timestamp;
      } else {
        if (segStart != null) {
          rawSegments.add(
            PressureSegment(startTime: segStart, endTime: r.timestamp),
          );
          segStart = null;
        }
      }
    }
    // 最后一段仍在床上
    if (segStart != null) {
      rawSegments.add(
        PressureSegment(startTime: segStart, endTime: readings.last.timestamp),
      );
    }

    if (rawSegments.isEmpty) return [];

    // 第二步：合并间隙短于 minGetUpGapSeconds 的相邻段
    final merged = <PressureSegment>[rawSegments.first];
    for (int i = 1; i < rawSegments.length; i++) {
      final gap = rawSegments[i].startTime
          .difference(merged.last.endTime)
          .inSeconds;
      if (gap < minGetUpGapSeconds) {
        // 合并：扩展上一段的结束时间
        final prev = merged.removeLast();
        merged.add(
          PressureSegment(
            startTime: prev.startTime,
            endTime: rawSegments[i].endTime,
          ),
        );
      } else {
        merged.add(rawSegments[i]);
      }
    }

    return merged;
  }

  /// 从读数中提取姿态段
  ///
  /// 将连续相同姿态的读数归组为一个 PostureSegment，
  /// 计算该段的平均置信度。
  static List<PostureSegment> _extractPostureSegments(
    List<PostureReading> readings,
  ) {
    if (readings.isEmpty) return [];

    final segments = <PostureSegment>[];
    var currentPosture = readings.first.posture;
    var segStart = readings.first.timestamp;
    var confidenceSum = readings.first.confidence;
    var count = 1;

    for (int i = 1; i < readings.length; i++) {
      final r = readings[i];
      if (r.posture == currentPosture) {
        confidenceSum += r.confidence;
        count++;
      } else {
        // 姿态发生变化，结束当前段
        segments.add(
          PostureSegment(
            startTime: segStart,
            endTime: r.timestamp,
            posture: currentPosture,
            avgConfidence: confidenceSum / count,
          ),
        );
        currentPosture = r.posture;
        segStart = r.timestamp;
        confidenceSum = r.confidence;
        count = 1;
      }
    }

    // 最后一段
    segments.add(
      PostureSegment(
        startTime: segStart,
        endTime: readings.last.timestamp,
        posture: currentPosture,
        avgConfidence: confidenceSum / count,
      ),
    );

    return segments;
  }

  /// 基于真实传感器数据计算睡眠评分（0-100）
  ///
  /// 评分因素：时长、起身次数、打鼾严重程度、姿态稳定性
  /// 从 100 分起，按规则扣分，最终夹紧到 30-98。
  static int _calculateSleepScore({
    required int durationMinutes,
    required int getUpCount,
    required int awayMinutes,
    required List<SnoringEvent> snoringEvents,
    required List<PostureSegment> postureSegments,
  }) {
    double score = 100;

    // 睡眠时长评分（满分 30 分，理想 7-9 小时）
    final hours = durationMinutes / 60.0;
    double durationScore;
    if (hours >= 7 && hours <= 9) {
      durationScore = 30;
    } else if (hours < 7) {
      durationScore = max(0, 30 - (7 - hours) * 6);
    } else {
      durationScore = max(0, 30 - (hours - 9) * 4);
    }

    // 起身次数评分（满分 20 分）
    final getUpScore = max(0.0, 20 - getUpCount * 6.0);

    // 离床时长评分（满分 15 分）
    final awayRatio = durationMinutes > 0 ? awayMinutes / durationMinutes : 0.0;
    final awayScore = max(0.0, 15 - awayRatio * 50);

    // 打鼾评分（满分 20 分）
    double snoreScore = 20;
    if (snoringEvents.isNotEmpty) {
      final totalSnoreMin = snoringEvents.fold<double>(
          0, (sum, e) => sum + e.durationMinutes);
      final snoreRatio = durationMinutes > 0 ? totalSnoreMin / durationMinutes : 0.0;
      snoreScore = max(0, 20 - snoreRatio * 60);

      final probEvents = snoringEvents.where((e) => e.avgProbability != null);
      if (probEvents.isNotEmpty) {
        final meanProb = probEvents.fold(0.0, (sum, e) => sum + e.avgProbability!) /
            probEvents.length;
        if (meanProb > 0.7) {
          snoreScore = snoreScore * 0.7;
        } else if (meanProb > 0.5) {
          snoreScore = snoreScore * 0.85;
        }
      }
    }

    // 姿态稳定性评分（满分 15 分）
    double postureScore = 15;
    if (postureSegments.isNotEmpty && durationMinutes > 0) {
      final changesPerHour = postureSegments.length / hours;
      postureScore = max(0, 15 - changesPerHour * 1.5);
    }

    score = durationScore + getUpScore + awayScore + snoreScore + postureScore;
    return score.round().clamp(30, 98);
  }

  /// 将 SnoreResult 中的事件转换为 SnoringEvent 列表
  ///
  /// SnoreEvent 中的 eventStartSeconds / eventEndSeconds 是相对于
  /// 录音开始的偏移秒数，需要加上 [recordingStart] 转为绝对时间。
  static List<SnoringEvent> _convertSnoreEvents(
    SnoreResult? snoreResult,
    DateTime recordingStart,
  ) {
    if (snoreResult == null || snoreResult.events.isEmpty) return [];

    return snoreResult.events
        .map((e) => e.toSnoringEvent(recordingStart))
        .toList();
  }
}
