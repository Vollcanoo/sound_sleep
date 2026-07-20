import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import '../models/sleep_record.dart';
import '../models/posture_event.dart';
import '../config/secrets.dart';

/// AI 睡眠分析服务
/// 使用火山引擎 VolcEngine（deepseek-v4-pro）分析睡眠数据，
/// 与 ESP32 sleep_llm 模块使用相同的 API 和模型。
class AiAnalysisService {
  /// VolcEngine API 地址（与 sleep_llm ESP32 模块一致）
  static const String _apiUrl =
      'https://ark.cn-beijing.volces.com/api/v3/chat/completions';

  /// VolcEngine API 密钥 — 从 lib/config/secrets.dart 读取
  /// 首次使用请复制 lib/config/secrets.dart.example → lib/config/secrets.dart 并填入 key
  static const String _apiKey = volcEngineApiKey;

  /// 模型名称（与 sleep_llm ESP32 模块一致）
  static const String _model = 'deepseek-v4-pro-260425';

  /// 请求 AI 分析。
  /// 如果配置了 API 密钥，会调用 VolcEngine；
  /// 否则使用本地规则分析作为 fallback。
  Future<AiAnalysis> analyze(SleepRecord record) async {
    if (_apiKey.isNotEmpty) {
      return _analyzeRemote(record);
    }
    return _analyzeLocal(record);
  }

  /// ── 火山引擎云端分析 ──
  /// 使用与 ESP32 sleep_llm 模块相同的 API 格式
  Future<AiAnalysis> _analyzeRemote(SleepRecord record) async {
    try {
      final userContent = _buildUserPrompt(record);

      final payload = {
        'model': _model,
        'temperature': 0.3,
        'max_tokens': 500,
        'messages': [
          {'role': 'system', 'content': _systemPrompt},
          {'role': 'user', 'content': userContent},
        ],
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
        final choices = data['choices'] as List<dynamic>?;
        if (choices != null && choices.isNotEmpty) {
          final message = choices[0]['message'] as Map<String, dynamic>?;
          final content = message?['content'] as String? ?? '';
          return _parseReport(content);
        }
        debugPrint('VolcEngine 返回空 choices');
        return _analyzeLocal(record);
      } else {
        debugPrint('VolcEngine API error: ${response.statusCode} ${response.body}');
        return _analyzeLocal(record);
      }
    } catch (e) {
      debugPrint('VolcEngine API request failed: $e');
      return _analyzeLocal(record);
    }
  }

  /// 系统提示词 — 与 sleep_llm 模块的 prompt 对齐
  static const String _systemPrompt =
      '你是专业的睡眠健康分析师。根据用户的传感器综合数据，'
      '从睡眠时长、入睡时间、起身次数、打鼾状况、睡姿分布等维度进行分析，'
      '给出个性化的睡眠质量评价和改善建议。\n\n'
      '请严格按照以下格式输出：\n'
      '【总结】一句话概括睡眠质量\n'
      '【分析】\n- 要点1\n- 要点2\n- ...\n'
      '【建议】\n- 建议1\n- 建议2\n- ...\n\n'
      '注意：分析要基于数据，建议要具体可操作，语言简洁。';

  /// 构建用户提示词，包含完整传感器数据
  String _buildUserPrompt(SleepRecord record) {
    final hours = record.durationMinutes / 60.0;
    final bedTimeStr =
        '${record.bedTime.hour.toString().padLeft(2, '0')}:${record.bedTime.minute.toString().padLeft(2, '0')}';
    final wakeTimeStr =
        '${record.wakeTime.hour.toString().padLeft(2, '0')}:${record.wakeTime.minute.toString().padLeft(2, '0')}';

    // 打鼾数据
    final snoringInfo = record.snoringEvents.isEmpty
        ? '无打鼾'
        : '打鼾${record.snoringEvents.length}次，'
            '总计${record.snoringTotalMinutes}分钟，'
            '最高${record.snoringMaxDecibel.toStringAsFixed(0)}dB';

    // 打鼾概率信息（如有）
    String snoringProbInfo = '';
    final eventsWithProb = record.snoringEvents
        .where((e) => e.avgProbability != null)
        .toList();
    if (eventsWithProb.isNotEmpty) {
      final avgProb = eventsWithProb
              .map((e) => e.avgProbability!)
              .reduce((a, b) => a + b) /
          eventsWithProb.length;
      snoringProbInfo =
          '，平均置信度${(avgProb * 100).toStringAsFixed(1)}%';
    }

    // 姿态数据
    final postureDist = record.postureDistribution;
    final postureStr = postureDist.isEmpty
        ? '无姿态数据'
        : postureDist.entries
            .map((e) => '${e.key.label}${e.value}分钟')
            .join('、');
    final dominantPosture = record.dominantPostureLabel;

    // 姿态段摘要
    final segCount = record.postureSegments.length;
    String postureSegInfo = '';
    if (segCount > 0) {
      final turnCount = record.postureSegments
          .where((s) => s.posture == PostureType.moving)
          .length;
      postureSegInfo = '，共$segCount个姿态段（翻身$turnCount次）';
    }

    return '传感器综合数据：'
        '睡眠时长${hours.toStringAsFixed(1)}小时，'
        '入睡$bedTimeStr，起床$wakeTimeStr，'
        '起身${record.getUpCount}次，'
        '$snoringInfo$snoringProbInfo，'
        '睡姿分布：$postureStr，'
        '主要睡姿：$dominantPosture$postureSegInfo，'
        '综合评分${record.sleepScore}分。';
  }

  /// 解析 LLM 返回的报告文本
  AiAnalysis _parseReport(String reportText) {
    String summary = '';
    final insights = <String>[];
    final suggestions = <String>[];

    // 按标记段落拆分
    final lines = reportText.split('\n');
    String currentSection = '';

    for (final line in lines) {
      final trimmed = line.trim();
      if (trimmed.isEmpty) continue;

      if (trimmed.contains('【总结】')) {
        currentSection = 'summary';
        final content = trimmed.replaceAll('【总结】', '').trim();
        if (content.isNotEmpty) summary = content;
        continue;
      }
      if (trimmed.contains('【分析】')) {
        currentSection = 'insights';
        continue;
      }
      if (trimmed.contains('【建议】')) {
        currentSection = 'suggestions';
        continue;
      }

      final content = trimmed.replaceFirst(RegExp(r'^[-•·]\s*'), '').trim();
      if (content.isEmpty) continue;

      switch (currentSection) {
        case 'summary':
          // 总结可能跨多行，拼接
          summary = summary.isEmpty ? content : '$summary $content';
        case 'insights':
          insights.add(content);
        case 'suggestions':
          suggestions.add(content);
      }
    }

    // 如果解析失败（LLM 未按格式输出），整段作为 summary
    if (summary.isEmpty && insights.isEmpty && suggestions.isEmpty) {
      summary = reportText.length > 200
          ? '${reportText.substring(0, 200)}...'
          : reportText;
    }

    return AiAnalysis(
      summary: summary.isNotEmpty ? summary : '分析完成',
      insights: insights,
      suggestions: suggestions,
      analyzedAt: DateTime.now(),
      model: _model,
    );
  }

  /// ── 本地规则分析（VolcEngine 不可用时的 fallback）──
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

    // ── 姿态分析 ──
    final postureDist = record.postureDistribution;
    if (postureDist.isNotEmpty) {
      final dominant = record.dominantPostureLabel;
      insights.add('主要睡姿为$dominant，'
          '姿态分布：${postureDist.entries.map((e) => '${e.key.label}${e.value}分钟').join('、')}。');

      // 仰卧比例过高 + 有打鼾 → 建议侧卧
      final supineMinutes = postureDist[PostureType.supine] ?? 0;
      if (supineMinutes > record.durationMinutes * 0.6 &&
          record.snoringEvents.isNotEmpty) {
        suggestions.add('仰卧时间占比较高且有打鼾，建议尝试侧卧位睡眠以减轻打鼾。');
      }

      // 翻身次数分析
      final movingSegments = record.postureSegments
          .where((s) => s.posture == PostureType.moving)
          .length;
      if (movingSegments > 20) {
        insights.add('翻身次数较多（$movingSegments次），睡眠可能不够安稳。');
        suggestions.add('检查床垫舒适度和卧室温度，频繁翻身可能与不适有关。');
      } else if (movingSegments <= 5 && record.durationMinutes > 360) {
        insights.add('翻身次数较少，睡眠安稳。');
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
    final postureNote = postureDist.isNotEmpty
        ? '，主要睡姿${record.dominantPostureLabel}'
        : '';
    final summary = record.sleepScore >= 75
        ? '昨晚睡眠质量${record.sleepQualityLabel}，'
            '睡了${hours.toStringAsFixed(1)}小时，'
            '${record.getUpCount == 0 ? "整夜连续" : "起身${record.getUpCount}次"}，'
            '${record.snoringEvents.isEmpty ? "无打鼾" : "打鼾${record.snoringTotalMinutes}分钟"}'
            '$postureNote。'
        : '昨晚睡眠需要改善：'
            '${hours < 7 ? "睡眠不足${hours.toStringAsFixed(1)}小时" : "时长${hours.toStringAsFixed(1)}小时"}，'
            '${record.getUpCount > 1 ? "起身${record.getUpCount}次较频繁" : ""}'
            '$postureNote，'
            '建议关注下方改善建议。';

    return AiAnalysis(
      summary: summary,
      insights: insights,
      suggestions: suggestions,
      analyzedAt: DateTime.now(),
      model: '本地规则分析（未配置 VolcEngine API 密钥）',
    );
  }
}
