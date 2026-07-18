import 'dart:convert';
import 'dart:math';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import '../models/snore_result.dart';
import '../models/sleep_record.dart';

/// 从云端获取打鼾检测结果的服务。
/// 云端运行 Snore_Det ML 管线，输出 snore_result.json。
class SnoreApiService {
  /// 云端 API 地址 — 部署后替换为真实地址
  /// 例如: https://your-server.com/api/snore
  static const String _baseUrl = '';

  /// 获取指定睡眠会话的打鼾检测结果
  ///
  /// [patientId] — 用户 ID
  /// [date] — 睡眠日期
  ///
  /// 返回 null 表示请求失败。
  /// 如果 [_baseUrl] 为空，使用本地模拟数据作为 fallback。
  Future<SnoreResult?> fetchSnoreResult({
    required String patientId,
    required DateTime date,
  }) async {
    // 未配置 API 时使用模拟数据
    if (_baseUrl.isEmpty) {
      return _mockSnoreResult(
        patientId: patientId,
        bedTime: DateTime(date.year, date.month, date.day, 22, 30),
        wakeTime: DateTime(date.year, date.month, date.day + 1, 7, 0),
      );
    }

    try {
      final dateStr =
          '${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}';
      final url = Uri.parse('$_baseUrl/results/$patientId/$dateStr');

      final response = await http
          .get(url, headers: {'Accept': 'application/json'})
          .timeout(const Duration(seconds: 15));

      if (response.statusCode == 200) {
        final json = jsonDecode(response.body) as Map<String, dynamic>;
        return SnoreResult.fromJson(json);
      } else if (response.statusCode == 404) {
        debugPrint('未找到打鼾检测结果: $patientId / $dateStr');
        return null;
      } else {
        debugPrint('打鼾 API 错误: ${response.statusCode} ${response.body}');
        return null;
      }
    } catch (e) {
      debugPrint('打鼾 API 请求失败: $e');
      return null;
    }
  }

  /// 将 SnoreResult 的事件列表转换为应用的 SnoringEvent 列表。
  ///
  /// [recordingStartTime] 为本次录音开始时间，
  /// event_start_seconds / event_end_seconds 相对于该时间的偏移。
  /// mean_probability (0-1) 映射到 avgDecibel (30-70 dB)。
  List<SnoringEvent> convertToSnoringEvents(
    SnoreResult result,
    DateTime recordingStartTime,
  ) {
    return result.events.map((event) {
      final startTime = recordingStartTime.add(
        Duration(milliseconds: (event.eventStartSeconds * 1000).round()),
      );
      final endTime = recordingStartTime.add(
        Duration(milliseconds: (event.eventEndSeconds * 1000).round()),
      );

      // mean_probability 0→30dB, 1→70dB
      final avgDecibel = 30.0 + event.meanProbability * 40.0;

      return SnoringEvent(
        startTime: startTime,
        endTime: endTime,
        avgDecibel: double.parse(avgDecibel.toStringAsFixed(1)),
      );
    }).toList();
  }

  /// 本地模拟打鼾检测结果（API 未配置时的 fallback）。
  /// 生成符合 snore_result.json 模式的真实感数据。
  Future<SnoreResult> _mockSnoreResult({
    required String patientId,
    required DateTime bedTime,
    required DateTime wakeTime,
  }) async {
    // 模拟网络延迟
    await Future.delayed(const Duration(milliseconds: 600));

    final random = Random(42); // 固定种子，保证一致性
    final sleepHours =
        wakeTime.difference(bedTime).inMinutes / 60.0;
    final totalSeconds = wakeTime.difference(bedTime).inSeconds.toDouble();

    // 生成 4-10 个打鼾事件
    final eventCount = 4 + random.nextInt(7);
    final events = <SnoreEvent>[];

    // 打鼾多发生在入睡 1 小时后到醒前 1 小时
    final windowStart = 3600.0; // 入睡后 1h
    final windowEnd = totalSeconds - 3600.0; // 醒前 1h
    if (windowEnd <= windowStart) {
      // 睡眠太短，不生成事件
      return _buildEmptyResult(patientId, sleepHours);
    }

    // 均匀分布事件起始点，带随机偏移
    final interval = (windowEnd - windowStart) / (eventCount + 1);
    for (int i = 0; i < eventCount; i++) {
      final baseOffset = windowStart + interval * (i + 1);
      final jitter = (random.nextDouble() - 0.5) * interval * 0.4;
      final startSec = (baseOffset + jitter).clamp(windowStart, windowEnd);

      // 事件持续 8-90 秒
      final duration = 8.0 + random.nextDouble() * 82.0;
      final endSec = (startSec + duration).clamp(startSec, totalSeconds);

      // 概率在 0.5-0.98 范围
      final meanProb = 0.5 + random.nextDouble() * 0.48;
      final maxProb = (meanProb + random.nextDouble() * 0.15).clamp(0.0, 0.99);

      events.add(SnoreEvent(
        eventStartSeconds: double.parse(startSec.toStringAsFixed(2)),
        eventEndSeconds: double.parse(endSec.toStringAsFixed(2)),
        durationSeconds: double.parse((endSec - startSec).toStringAsFixed(2)),
        windowCount: ((endSec - startSec) / 2.0).ceil(), // ~2 秒窗口
        meanProbability: double.parse(meanProb.toStringAsFixed(4)),
        maxProbability: double.parse(maxProb.toStringAsFixed(4)),
      ));
    }

    // 计算汇总统计
    final totalPosDuration =
        events.fold(0.0, (sum, e) => sum + e.durationSeconds);
    final longestEvent = events
        .map((e) => e.durationSeconds)
        .reduce((a, b) => a > b ? a : b);
    final meanEventDuration = totalPosDuration / events.length;
    final meanProb =
        events.fold(0.0, (sum, e) => sum + e.meanProbability) /
            events.length;
    final maxProb =
        events.map((e) => e.maxProbability).reduce((a, b) => a > b ? a : b);

    return SnoreResult(
      schemaVersion: '1.0',
      generatedAt: DateTime.now().toUtc().toIso8601String(),
      modelType: 'mock_cnn_lstm',
      patientId: patientId,
      sleepHours: double.parse(sleepHours.toStringAsFixed(2)),
      summary: SnoreSummary(
        snoreDetected: true,
        eventCount: events.length,
        positiveDurationSeconds:
            double.parse(totalPosDuration.toStringAsFixed(2)),
        positiveDurationMinutes:
            double.parse((totalPosDuration / 60.0).toStringAsFixed(2)),
        longestEventSeconds:
            double.parse(longestEvent.toStringAsFixed(2)),
        meanEventSeconds:
            double.parse(meanEventDuration.toStringAsFixed(2)),
        meanPositiveProbability:
            double.parse(meanProb.toStringAsFixed(4)),
        maxProbability: double.parse(maxProb.toStringAsFixed(4)),
        snoreEventsPerHour:
            double.parse((events.length / sleepHours).toStringAsFixed(2)),
        snoreMinutesPerHour: double.parse(
            (totalPosDuration / 60.0 / sleepHours).toStringAsFixed(2)),
      ),
      events: events,
    );
  }

  /// 生成无打鼾事件的空结果
  SnoreResult _buildEmptyResult(String patientId, double sleepHours) {
    return SnoreResult(
      schemaVersion: '1.0',
      generatedAt: DateTime.now().toUtc().toIso8601String(),
      modelType: 'mock_cnn_lstm',
      patientId: patientId,
      sleepHours: double.parse(sleepHours.toStringAsFixed(2)),
      summary: SnoreSummary(
        snoreDetected: false,
        eventCount: 0,
        positiveDurationSeconds: 0.0,
        positiveDurationMinutes: 0.0,
        longestEventSeconds: 0.0,
        meanEventSeconds: 0.0,
        meanPositiveProbability: 0.0,
        maxProbability: 0.0,
        snoreEventsPerHour: 0.0,
        snoreMinutesPerHour: 0.0,
      ),
      events: [],
    );
  }
}
