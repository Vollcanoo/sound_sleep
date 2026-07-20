import 'package:flutter/material.dart';
import '../models/posture_event.dart';

/// 姿态分布可视化：水平分段条 + 图例
class PostureChart extends StatelessWidget {
  final Map<PostureType, int> distribution; // 每种姿态的分钟数
  final int totalMinutes;

  const PostureChart({
    super.key,
    required this.distribution,
    required this.totalMinutes,
  });

  static Color colorFor(PostureType type) {
    switch (type) {
      case PostureType.supine:
        return const Color(0xFF1565C0);
      case PostureType.leftSide:
        return const Color(0xFF43A047);
      case PostureType.rightSide:
        return const Color(0xFFFF9800);
      case PostureType.moving:
        return const Color(0xFF9E9E9E);
      case PostureType.noHead:
        return const Color(0xFFE53935);
    }
  }

  @override
  Widget build(BuildContext context) {
    if (distribution.isEmpty || totalMinutes <= 0) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 24),
          child: Column(
            children: [
              Icon(Icons.airline_seat_flat, size: 36, color: Colors.grey.shade300),
              const SizedBox(height: 8),
              Text(
                '暂无睡姿数据',
                style: TextStyle(fontSize: 14, color: Colors.grey.shade500),
              ),
            ],
          ),
        ),
      );
    }

    // 按时长降序排列
    final sorted = distribution.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // ── 水平分段条 ──
        ClipRRect(
          borderRadius: BorderRadius.circular(6),
          child: SizedBox(
            height: 24,
            child: Row(
              children: sorted.map((entry) {
                final fraction = entry.value / totalMinutes;
                if (fraction <= 0) return const SizedBox.shrink();
                return Expanded(
                  flex: (fraction * 1000).round().clamp(1, 1000),
                  child: Container(color: colorFor(entry.key)),
                );
              }).toList(),
            ),
          ),
        ),
        const SizedBox(height: 16),

        // ── 图例 ──
        Wrap(
          spacing: 16,
          runSpacing: 10,
          children: sorted.map((entry) {
            final pct = (entry.value / totalMinutes * 100).round();
            return _LegendItem(
              color: colorFor(entry.key),
              emoji: entry.key.emoji,
              label: entry.key.label,
              minutes: entry.value,
              percent: pct,
            );
          }).toList(),
        ),
      ],
    );
  }
}

class _LegendItem extends StatelessWidget {
  final Color color;
  final String emoji;
  final String label;
  final int minutes;
  final int percent;

  const _LegendItem({
    required this.color,
    required this.emoji,
    required this.label,
    required this.minutes,
    required this.percent,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 10,
          height: 10,
          decoration: BoxDecoration(
            color: color,
            borderRadius: BorderRadius.circular(3),
          ),
        ),
        const SizedBox(width: 4),
        Text(
          '$emoji $label',
          style: const TextStyle(fontSize: 13),
        ),
        const SizedBox(width: 4),
        Text(
          '$minutes分钟 ($percent%)',
          style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
        ),
      ],
    );
  }
}
