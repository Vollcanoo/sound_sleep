import 'posture_event.dart';

/// 打鼾事件
class SnoringEvent {
  final DateTime startTime;
  final DateTime endTime;
  final double avgDecibel;
  final double? avgProbability; // ML 管线输出的置信概率

  SnoringEvent({
    required this.startTime,
    required this.endTime,
    required this.avgDecibel,
    this.avgProbability,
  });

  int get durationMinutes => endTime.difference(startTime).inMinutes;
}

/// 压力传感器事件：记录一段连续的有压力时间
/// 从 startTime 到 endTime 之间传感器持续检测到压力（人在床上）
class PressureSegment {
  final DateTime startTime; // 感受到压力（上床/回床）
  final DateTime endTime;   // 压力消失（离床/起身）

  PressureSegment({
    required this.startTime,
    required this.endTime,
  });

  int get durationMinutes => endTime.difference(startTime).inMinutes;
}

/// AI 分析结果（由云端 LLM 返回）
class AiAnalysis {
  final String summary;             // 一句话总结
  final List<String> insights;      // 具体分析要点
  final List<String> suggestions;   // 改善建议
  final DateTime analyzedAt;        // 分析时间
  final String? model;              // 使用的模型名

  AiAnalysis({
    required this.summary,
    required this.insights,
    required this.suggestions,
    required this.analyzedAt,
    this.model,
  });
}

/// 一份完整的睡眠报告
/// 报告生成逻辑：
///   - 当压力传感器首次检测到压力 → 开始记录（bedTime）
///   - 中间压力短暂消失再恢复 → 计为一次"起身"（如上厕所）
///   - 当压力最终消失且不再恢复 → 报告结束（wakeTime）
///   - 整个从首次有压力到最终无压力的过程生成一份报告
class SleepRecord {
  final String id;
  final String userId;
  final DateTime date;
  final DateTime bedTime;   // 首次感受到压力
  final DateTime wakeTime;  // 最终压力消失
  final int sleepScore;     // 0-100
  final List<SnoringEvent> snoringEvents;
  final List<PressureSegment> pressureSegments; // 各段有压力的时间片段
  final List<PostureSegment> postureSegments;   // 各段姿态数据
  final int getUpCount;     // 夜间起身次数
  AiAnalysis? aiAnalysis;   // 云端 LLM 分析结果（可为 null，异步获取）

  SleepRecord({
    required this.id,
    required this.userId,
    required this.date,
    required this.bedTime,
    required this.wakeTime,
    required this.sleepScore,
    this.snoringEvents = const [],
    this.pressureSegments = const [],
    this.postureSegments = const [],
    this.getUpCount = 0,
    this.aiAnalysis,
  });

  /// 总在床时长（从上床到最终离床）
  int get durationMinutes => wakeTime.difference(bedTime).inMinutes;

  /// 实际睡眠时长（去掉起身离床的时间）
  int get actualSleepMinutes =>
      pressureSegments.fold(0, (sum, seg) => sum + seg.durationMinutes);

  String get durationFormatted {
    final hours = durationMinutes ~/ 60;
    final minutes = durationMinutes % 60;
    return '${hours}h ${minutes}m';
  }

  String get actualSleepFormatted {
    final total = actualSleepMinutes;
    final hours = total ~/ 60;
    final minutes = total % 60;
    return '${hours}h ${minutes}m';
  }

  /// 离床总时长（分钟）
  int get awayMinutes => durationMinutes - actualSleepMinutes;

  int get snoringTotalMinutes =>
      snoringEvents.fold(0, (sum, e) => sum + e.durationMinutes);

  double get snoringMaxDecibel => snoringEvents.isEmpty
      ? 0.0
      : snoringEvents.map((e) => e.avgDecibel).reduce((a, b) => a > b ? a : b);

  String get sleepQualityLabel {
    if (sleepScore >= 90) return '优秀';
    if (sleepScore >= 75) return '良好';
    if (sleepScore >= 60) return '一般';
    return '较差';
  }

  /// 各姿态的累计时长（分钟）
  Map<PostureType, int> get postureDistribution {
    final dist = <PostureType, int>{};
    for (final seg in postureSegments) {
      dist[seg.posture] = (dist[seg.posture] ?? 0) + seg.durationMinutes;
    }
    return dist;
  }

  /// 占比最大的姿态（中文标签）
  String get dominantPostureLabel {
    if (postureSegments.isEmpty) return '无数据';
    final dist = postureDistribution;
    final dominant = dist.entries.reduce(
      (a, b) => a.value >= b.value ? a : b,
    );
    return dominant.key.label;
  }
}
