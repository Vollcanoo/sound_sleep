import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import '../models/sleep_record.dart';
import '../theme/app_theme.dart';

/// 睡眠时段图：展示压力段（在床）和起身间隙
class SleepStageChart extends StatelessWidget {
  final DateTime bedTime;
  final DateTime wakeTime;
  final List<PressureSegment> pressureSegments;
  final int getUpCount;

  const SleepStageChart({
    super.key,
    required this.bedTime,
    required this.wakeTime,
    this.pressureSegments = const [],
    this.getUpCount = 0,
  });

  @override
  Widget build(BuildContext context) {
    final totalMinutes = wakeTime.difference(bedTime).inMinutes;
    final bedLabel = DateFormat('HH:mm').format(bedTime);
    final wakeLabel = DateFormat('HH:mm').format(wakeTime);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // 时间标签行
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            _TimeLabel(icon: Icons.bedtime, label: '入睡', time: bedLabel),
            _TimeLabel(
              icon: Icons.wb_sunny_outlined,
              label: '起床',
              time: wakeLabel,
            ),
          ],
        ),
        const SizedBox(height: 16),
        // 压力可视化条：蓝色=在床，灰色间隙=起身
        if (pressureSegments.isNotEmpty && totalMinutes > 0)
          _buildSegmentedBar(totalMinutes)
        else
          _buildSimpleBar(),
        const SizedBox(height: 10),
        // 图例
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            _LegendDot(color: AppTheme.primaryBlue, label: '在床'),
            if (getUpCount > 0) ...[
              const SizedBox(width: 20),
              _LegendDot(
                color: const Color(0xFFFF9800),
                label: '起身 $getUpCount次',
              ),
            ],
          ],
        ),
        const SizedBox(height: 8),
        Center(
          child: Text(
            '总时长 ${totalMinutes ~/ 60}小时${totalMinutes % 60}分钟',
            style: TextStyle(
              fontSize: 13,
              color: Colors.grey.shade600,
              fontWeight: FontWeight.w500,
            ),
          ),
        ),
      ],
    );
  }

  /// 有压力分段数据时：绘制蓝色段 + 橙色间隙
  Widget _buildSegmentedBar(int totalMinutes) {
    final children = <Widget>[];
    for (int i = 0; i < pressureSegments.length; i++) {
      final seg = pressureSegments[i];
      final segMinutes = seg.durationMinutes;
      final flex = (segMinutes * 100 / totalMinutes).round().clamp(1, 100);

      // 在床段（蓝色渐变）
      children.add(Flexible(
        flex: flex,
        child: Container(
          height: 28,
          decoration: BoxDecoration(
            gradient: LinearGradient(
              colors: [
                AppTheme.darkBlue,
                AppTheme.primaryBlue,
                AppTheme.lightBlue,
              ],
            ),
            borderRadius: BorderRadius.horizontal(
              left: i == 0
                  ? const Radius.circular(14)
                  : Radius.zero,
              right: i == pressureSegments.length - 1
                  ? const Radius.circular(14)
                  : Radius.zero,
            ),
          ),
        ),
      ));

      // 如果不是最后一段，在两段之间插入起身间隙
      if (i < pressureSegments.length - 1) {
        final nextSeg = pressureSegments[i + 1];
        final gapMinutes =
            nextSeg.startTime.difference(seg.endTime).inMinutes;
        final gapFlex =
            (gapMinutes * 100 / totalMinutes).round().clamp(1, 100);

        children.add(Flexible(
          flex: gapFlex,
          child: Container(
            height: 28,
            margin: const EdgeInsets.symmetric(horizontal: 1),
            decoration: BoxDecoration(
              color: const Color(0xFFFF9800).withValues(alpha: 0.3),
              border: Border.all(
                color: const Color(0xFFFF9800).withValues(alpha: 0.6),
                width: 1,
              ),
            ),
            child: gapMinutes >= 8
                ? Center(
                    child: Icon(
                      Icons.directions_walk,
                      size: 14,
                      color: Colors.orange.shade700,
                    ),
                  )
                : null,
          ),
        ));
      }
    }

    return Row(children: children);
  }

  /// 无压力分段数据时：简单渐变条
  Widget _buildSimpleBar() {
    return Container(
      height: 28,
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(14),
        gradient: const LinearGradient(
          colors: [
            Color(0xFF0D47A1),
            Color(0xFF1565C0),
            Color(0xFF1976D2),
            Color(0xFF42A5F5),
            Color(0xFF64B5F6),
            Color(0xFFFFB74D),
          ],
        ),
      ),
    );
  }
}

class _TimeLabel extends StatelessWidget {
  final IconData icon;
  final String label;
  final String time;

  const _TimeLabel({
    required this.icon,
    required this.label,
    required this.time,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 18, color: AppTheme.primaryBlue),
        const SizedBox(width: 6),
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              label,
              style: TextStyle(fontSize: 11, color: Colors.grey.shade500),
            ),
            Text(
              time,
              style: const TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.bold,
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _LegendDot extends StatelessWidget {
  final Color color;
  final String label;

  const _LegendDot({required this.color, required this.label});

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
          label,
          style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
        ),
      ],
    );
  }
}
