import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import '../models/sleep_record.dart';

/// AI 睡眠分析服务
/// 将睡眠数据发送给云端 LLM，获取个性化分析和建议
class AiAnalysisService {
  /// 云端 API 地址 — 部署后替换为真实地址
  /// 例如: https://your-server.com/api/sleep/analyze
  static const String _apiUrl = '';

  /// API 密钥 — 生产环境中应从安全存储读取
  static const String _apiKey = '';

  /// 请求 AI 分析。
  /// 如果配置了真实 API，会发送 HTTP 请求；
  /// 否则使用本地模拟分析（基于规则生成）。
  Future<AiAnalysis> analyze(SleepRecord record) async {
    if (_apiUrl.isNotEmpty && _apiKey.isNotEmpty) {
      return _analyzeRemote(record);
    }
    return _analyzeLocal(record);
  }

  /// ── 真实云端分析 ──
  /// 发送睡眠数据到你的后端，由后端调用 LLM API
  Future<AiAnalysis> _analyzeRemote(SleepRecord record) async {
    try {
      final payload = {
        'sleep_data': {
          'date': record.date.toIso8601String(),
          'bed_time': record.bedTime.toIso8601String(),
          'wake_time': record.wakeTime.toIso8601String(),
          'duration_minutes': record.durationMinutes,
          'actual_sleep_minutes': record.actualSleepMinutes,
          'sleep_score': record.sleepScore,
          'get_up_count': record.getUpCount,
          'away_minutes': record.awayMinutes,
          'snoring_count': record.snoringEvents.length,
          'snoring_total_minutes': record.snoringTotalMinutes,
          'snoring_max_decibel': record.snoringMaxDecibel,
        },
      };

      final response = await http
          .post(
            Uri.parse(_apiUrl),
            headers: {
              'Content-Type': 'application/json',
              'Authorization': 'Bearer $_apiKey',
            },
            body: jsonEncode(payload),
          )
          .timeout(const Duration(seconds: 30));

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body) as Map<String, dynamic>;
        return AiAnalysis(
          summary: data['summary'] as String? ?? '分析完成',
          insights: List<String>.from(data['insights'] ?? []),
          suggestions: List<String>.from(data['suggestions'] ?? []),
          analyzedAt: DateTime.now(),
          model: data['model'] as String?,
        );
      } else {
        debugPrint('AI API error: ${response.statusCode} ${response.body}');
        return _analyzeLocal(record);
      }
    } catch (e) {
      debugPrint('AI API request failed: $e');
      return _analyzeLocal(record);
    }
  }

  /// ── 本地规则分析（LLM 不可用时的 fallback）──
  /// 根据睡眠数据用简单规则生成分析，模拟 LLM 的输出格式
  Future<AiAnalysis> _analyzeLocal(SleepRecord record) async {
    // 模拟网络延迟
    await Future.delayed(const Duration(milliseconds: 1200));

    final insights = <String>[];
    final suggestions = <String>[];

    // ── 时长分析 ──
    final hours = record.durationMinutes / 60.0;
    if (hours >= 7 && hours <= 9) {
      insights.add('睡眠时长${hours.toStringAsFixed(1)}小时，处于推荐的7-9小时范围内，非常理想。');
    } else if (hours < 7) {
      insights.add('睡眠时长仅${hours.toStringAsFixed(1)}小时，低于推荐的7小时最低标准。');
      suggestions.add('建议提前30分钟上床，减少睡前使用电子设备的时间。');
    } else {
      insights.add('睡眠时长达${hours.toStringAsFixed(1)}小时，超过9小时可能导致白天困倦。');
      suggestions.add('可以尝试设定固定的起床时间，保持规律的作息节奏。');
    }

    // ── 入睡时间分析 ──
    final bedHour = record.bedTime.hour;
    if (bedHour >= 22 && bedHour < 24) {
      insights.add('入睡时间在22-24点之间，符合人体生物钟节律。');
    } else if (bedHour >= 0 && bedHour < 2) {
      insights.add('入睡时间较晚（凌晨$bedHour点后），可能影响深度睡眠比例。');
      suggestions.add('尽量在23点前入睡，以获得更多的深度睡眠时间。');
    }

    // ── 起身分析 ──
    if (record.getUpCount == 0) {
      insights.add('整夜未起身，睡眠连续性极佳。');
    } else if (record.getUpCount <= 1) {
      insights.add('夜间起身${record.getUpCount}次，属于正常范围。');
    } else {
      insights.add('夜间起身${record.getUpCount}次，频繁起身会中断睡眠周期。');
      suggestions.add('睡前2小时减少饮水量，有助于减少夜间起身次数。');
    }

    // ── 打鼾分析 ──
    if (record.snoringEvents.isEmpty) {
      insights.add('未检测到打鼾，呼吸状况良好。');
    } else {
      final snoringPct =
          (record.snoringTotalMinutes / record.durationMinutes * 100);
      insights.add(
        '检测到${record.snoringEvents.length}次打鼾，'
        '总计${record.snoringTotalMinutes}分钟'
        '（占睡眠时长${snoringPct.toStringAsFixed(1)}%）。',
      );
      if (record.snoringMaxDecibel > 55) {
        insights.add(
          '最高打鼾分贝达${record.snoringMaxDecibel.toStringAsFixed(0)}dB，属于中重度打鼾。',
        );
        suggestions.add('建议侧卧睡眠，避免仰卧。如持续严重打鼾，建议咨询医生排查睡眠呼吸暂停。');
      }
      if (snoringPct > 10) {
        suggestions.add('睡前避免饮酒和服用安眠药物，这些会加重打鼾。');
      }
    }

    // ── 综合评分 ──
    if (record.sleepScore >= 90) {
      insights.add('综合评分${record.sleepScore}分，睡眠质量优秀！继续保持良好的作息习惯。');
    } else if (record.sleepScore >= 75) {
      insights.add('综合评分${record.sleepScore}分，睡眠质量良好，还有提升空间。');
    } else if (record.sleepScore >= 60) {
      insights.add('综合评分${record.sleepScore}分，睡眠质量一般，需要关注改善。');
    } else {
      insights.add('综合评分${record.sleepScore}分，睡眠质量较差，建议重点改善作息。');
    }

    // 通用建议
    if (suggestions.isEmpty) {
      suggestions.add('保持当前良好的睡眠习惯，坚持规律作息。');
    }
    suggestions.add('保持卧室温度在18-22°C，湿度在40-60%，有助于提升睡眠质量。');

    // ── 生成总结 ──
    final summary = record.sleepScore >= 75
        ? '昨晚睡眠质量${record.sleepQualityLabel}，'
            '睡了${hours.toStringAsFixed(1)}小时，'
            '${record.getUpCount == 0 ? "整夜连续" : "起身${record.getUpCount}次"}，'
            '${record.snoringEvents.isEmpty ? "无打鼾" : "打鼾${record.snoringTotalMinutes}分钟"}。'
        : '昨晚睡眠需要改善：'
            '${hours < 7 ? "睡眠不足${hours.toStringAsFixed(1)}小时" : "时长${hours.toStringAsFixed(1)}小时"}，'
            '${record.getUpCount > 1 ? "起身${record.getUpCount}次较频繁" : ""}，'
            '建议关注下方改善建议。';

    return AiAnalysis(
      summary: summary,
      insights: insights,
      suggestions: suggestions,
      analyzedAt: DateTime.now(),
      model: '本地规则分析（未配置云端 LLM）',
    );
  }
}
