import 'sleep_record.dart';

/// 解析 Snore_Det ML 管线输出的 snore_result.json
class SnoreResult {
  final String schemaVersion;
  final String generatedAt;
  final String modelType;
  final String patientId;
  final double sleepHours;
  final SnoreSummary summary;
  final List<SnoreEvent> events;

  SnoreResult({
    required this.schemaVersion,
    required this.generatedAt,
    required this.modelType,
    required this.patientId,
    required this.sleepHours,
    required this.summary,
    required this.events,
  });

  /// 从 snore_result.json 解析
  factory SnoreResult.fromJson(Map<String, dynamic> json) {
    final model = json['model'] as Map<String, dynamic>? ?? {};
    final input = json['input'] as Map<String, dynamic>? ?? {};

    return SnoreResult(
      schemaVersion: json['schema_version'] as String? ?? '',
      generatedAt: json['generated_at_utc'] as String? ?? '',
      modelType: model['type'] as String? ?? '',
      patientId: input['patient_id'] as String? ?? '',
      sleepHours: (input['sleep_hours'] as num?)?.toDouble() ?? 0.0,
      summary: SnoreSummary.fromJson(
        json['summary'] as Map<String, dynamic>? ?? {},
      ),
      events: (json['events'] as List<dynamic>?)
              ?.map((e) => SnoreEvent.fromJson(e as Map<String, dynamic>))
              .toList() ??
          [],
    );
  }
}

/// snore_result.json → summary 部分
class SnoreSummary {
  final bool snoreDetected;
  final int eventCount;
  final double positiveDurationSeconds;
  final double positiveDurationMinutes;
  final double longestEventSeconds;
  final double meanEventSeconds;
  final double meanPositiveProbability;
  final double maxProbability;
  final double snoreEventsPerHour;
  final double snoreMinutesPerHour;

  SnoreSummary({
    required this.snoreDetected,
    required this.eventCount,
    required this.positiveDurationSeconds,
    required this.positiveDurationMinutes,
    required this.longestEventSeconds,
    required this.meanEventSeconds,
    required this.meanPositiveProbability,
    required this.maxProbability,
    required this.snoreEventsPerHour,
    required this.snoreMinutesPerHour,
  });

  factory SnoreSummary.fromJson(Map<String, dynamic> json) {
    return SnoreSummary(
      snoreDetected: json['snore_detected'] as bool? ?? false,
      eventCount: json['event_count'] as int? ?? 0,
      positiveDurationSeconds:
          (json['positive_duration_seconds'] as num?)?.toDouble() ?? 0.0,
      positiveDurationMinutes:
          (json['positive_duration_minutes'] as num?)?.toDouble() ?? 0.0,
      longestEventSeconds:
          (json['longest_event_seconds'] as num?)?.toDouble() ?? 0.0,
      meanEventSeconds:
          (json['mean_event_seconds'] as num?)?.toDouble() ?? 0.0,
      meanPositiveProbability:
          (json['mean_positive_probability'] as num?)?.toDouble() ?? 0.0,
      maxProbability:
          (json['max_probability'] as num?)?.toDouble() ?? 0.0,
      snoreEventsPerHour:
          (json['snore_events_per_hour'] as num?)?.toDouble() ?? 0.0,
      snoreMinutesPerHour:
          (json['snore_minutes_per_hour'] as num?)?.toDouble() ?? 0.0,
    );
  }
}

/// snore_result.json → events[] 中的单个打鼾事件
class SnoreEvent {
  final double eventStartSeconds;
  final double eventEndSeconds;
  final double durationSeconds;
  final int windowCount;
  final double meanProbability;
  final double maxProbability;

  SnoreEvent({
    required this.eventStartSeconds,
    required this.eventEndSeconds,
    required this.durationSeconds,
    required this.windowCount,
    required this.meanProbability,
    required this.maxProbability,
  });

  factory SnoreEvent.fromJson(Map<String, dynamic> json) {
    return SnoreEvent(
      eventStartSeconds:
          (json['event_start_seconds'] as num?)?.toDouble() ?? 0.0,
      eventEndSeconds:
          (json['event_end_seconds'] as num?)?.toDouble() ?? 0.0,
      durationSeconds:
          (json['duration_seconds'] as num?)?.toDouble() ?? 0.0,
      windowCount: json['window_count'] as int? ?? 0,
      meanProbability:
          (json['mean_probability'] as num?)?.toDouble() ?? 0.0,
      maxProbability:
          (json['max_probability'] as num?)?.toDouble() ?? 0.0,
    );
  }

  /// 将 ML 事件转换为应用的 SnoringEvent 模型
  ///
  /// [recordingStart] 是本次录音的开始时间，
  /// event_start_seconds / event_end_seconds 为相对于该时间的偏移秒数。
  SnoringEvent toSnoringEvent(DateTime recordingStart) {
    return SnoringEvent(
      startTime: recordingStart.add(
        Duration(milliseconds: (eventStartSeconds * 1000).round()),
      ),
      endTime: recordingStart.add(
        Duration(milliseconds: (eventEndSeconds * 1000).round()),
      ),
      avgDecibel: 0.0, // ML 管线不提供分贝值
      avgProbability: meanProbability,
    );
  }
}
