class RealtimeSnoreReading {
  final DateTime timestamp;
  final double probability;
  final bool detected;
  final double windowSeconds;

  const RealtimeSnoreReading({
    required this.timestamp,
    required this.probability,
    required this.detected,
    required this.windowSeconds,
  });

  factory RealtimeSnoreReading.fromJson(
    Map<String, dynamic> json, {
    DateTime? timestamp,
  }) {
    return RealtimeSnoreReading(
      timestamp: timestamp ?? DateTime.now(),
      probability: (json['probability'] as num?)?.toDouble() ?? 0.0,
      detected: json['detected'] == true,
      windowSeconds: (json['window_seconds'] as num?)?.toDouble() ?? 0.0,
    );
  }
}
