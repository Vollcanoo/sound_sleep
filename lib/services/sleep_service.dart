import 'dart:math';
import '../models/sleep_record.dart';

class SleepService {
  final List<SleepRecord> _records = [];
  final _random = Random(42); // Fixed seed for consistent data

  SleepService() {
    _generateMockData();
  }

  /// 模拟报告生成逻辑：
  /// 压力传感器检测到压力（人躺下） → 开始记录
  /// 中间压力短暂消失（起身上厕所等） → 计为一次 getUp
  /// 压力最终消失（人起床离开） → 结束记录，生成一份完整报告
  void _generateMockData() {
    final now = DateTime.now();
    for (int i = 0; i < 30; i++) {
      final date = DateTime(now.year, now.month, now.day)
          .subtract(Duration(days: i));

      // ── 模拟首次压力感应（上床时间）22:00 - 00:30 ──
      final bedHour = 22 + _random.nextInt(3);
      final bedMinute = _random.nextInt(60);
      final bedTime = DateTime(
        date.year, date.month, date.day,
        bedHour % 24, bedMinute,
      );
      final actualBedTime =
          bedHour >= 24 ? bedTime.add(const Duration(days: 1)) : bedTime;

      // ── 模拟最终压力消失（起床时间）6:00 - 8:30 ──
      final wakeHour = 6 + _random.nextInt(3);
      final wakeMinute = _random.nextInt(60);
      final wakeTime = DateTime(
        date.year, date.month, date.day + 1,
        wakeHour, wakeMinute,
      );

      // ── 模拟夜间起身事件 → 生成 pressure segments ──
      // getUpCount: 0-3 次
      final getUpCount = _random.nextInt(4);
      final pressureSegments = <PressureSegment>[];

      if (getUpCount == 0) {
        // 整夜无起身，一个完整的压力段
        pressureSegments.add(PressureSegment(
          startTime: actualBedTime,
          endTime: wakeTime,
        ));
      } else {
        // 有起身：在 bedTime 和 wakeTime 之间插入 getUpCount 个间隙
        final totalSleepMin = wakeTime.difference(actualBedTime).inMinutes;
        // 每次起身占用 3-15 分钟
        final getUpDurations = List.generate(
          getUpCount,
          (_) => 3 + _random.nextInt(13),
        );
        // 起身发生的时间点（距入睡后的分钟数），在总时长的 20%-90% 范围
        final getUpStartOffsets = <int>[];
        for (int g = 0; g < getUpCount; g++) {
          final earliest = (totalSleepMin * 0.2).round();
          final latest =
              (totalSleepMin * 0.9).round() - getUpDurations[g];
          if (latest <= earliest) continue;
          getUpStartOffsets.add(
            earliest + _random.nextInt(latest - earliest),
          );
        }
        getUpStartOffsets.sort();

        // 去重：两次起身间隔至少 30 分钟
        final filtered = <int>[];
        for (final offset in getUpStartOffsets) {
          if (filtered.isEmpty || offset - filtered.last > 30) {
            filtered.add(offset);
          }
        }

        // 根据起身间隙切割出压力段
        var segStart = actualBedTime;
        for (int g = 0; g < filtered.length; g++) {
          final gapStart = actualBedTime.add(
            Duration(minutes: filtered[g]),
          );
          final gapEnd = gapStart.add(
            Duration(minutes: getUpDurations[g]),
          );

          // 上一段有压力
          if (gapStart.isAfter(segStart)) {
            pressureSegments.add(PressureSegment(
              startTime: segStart,
              endTime: gapStart,
            ));
          }
          segStart = gapEnd;
        }
        // 最后一段有压力，到起床
        if (segStart.isBefore(wakeTime)) {
          pressureSegments.add(PressureSegment(
            startTime: segStart,
            endTime: wakeTime,
          ));
        }

        // 更新实际起身次数（可能过滤后减少了）
        // 直接用 pressureSegments.length - 1 来确保一致
      }

      final actualGetUpCount =
          pressureSegments.isEmpty ? 0 : pressureSegments.length - 1;

      // ── 睡眠评分：起身多/打鼾多 → 扣分 ──
      var sleepScore = 55 + _random.nextInt(44);
      sleepScore = (sleepScore - actualGetUpCount * 5).clamp(30, 98);

      // ── 打鼾事件 0-5 次 ──
      final snoringCount = _random.nextInt(6);
      final snoringEvents = <SnoringEvent>[];
      for (int j = 0; j < snoringCount; j++) {
        final sleepMin =
            wakeTime.difference(actualBedTime).inMinutes;
        if (sleepMin <= 120) break;
        final eventStart = actualBedTime.add(Duration(
          minutes: 60 + _random.nextInt((sleepMin - 120).clamp(1, 999)),
        ));
        final duration = 3 + _random.nextInt(18);
        final eventEnd = eventStart.add(Duration(minutes: duration));
        final avgDecibel = 30.0 + _random.nextDouble() * 40;

        snoringEvents.add(SnoringEvent(
          startTime: eventStart,
          endTime: eventEnd.isBefore(wakeTime) ? eventEnd : wakeTime,
          avgDecibel: double.parse(avgDecibel.toStringAsFixed(1)),
        ));
      }

      _records.add(SleepRecord(
        id: 'sleep_${date.millisecondsSinceEpoch}',
        userId: 'user_001',
        date: date,
        bedTime: actualBedTime,
        wakeTime: wakeTime,
        sleepScore: sleepScore,
        snoringEvents: snoringEvents,
        pressureSegments: pressureSegments,
        getUpCount: actualGetUpCount,
      ));
    }
  }

  List<SleepRecord> getRecords() => List.unmodifiable(_records);

  SleepRecord? getRecordByDate(DateTime date) {
    try {
      return _records.firstWhere(
        (r) =>
            r.date.year == date.year &&
            r.date.month == date.month &&
            r.date.day == date.day,
      );
    } catch (_) {
      return null;
    }
  }

  SleepRecord? getRecordById(String id) {
    try {
      return _records.firstWhere((r) => r.id == id);
    } catch (_) {
      return null;
    }
  }

  List<SleepRecord> getRecentRecords(int days) {
    final sorted = List<SleepRecord>.from(_records)
      ..sort((a, b) => b.date.compareTo(a.date));
    return sorted.take(days).toList();
  }

  SleepRecord? get latestRecord {
    if (_records.isEmpty) return null;
    final sorted = List<SleepRecord>.from(_records)
      ..sort((a, b) => b.date.compareTo(a.date));
    return sorted.first;
  }

  /// 添加一条真实传感器生成的睡眠记录
  void addRecord(SleepRecord record) {
    _records.add(record);
  }
}
