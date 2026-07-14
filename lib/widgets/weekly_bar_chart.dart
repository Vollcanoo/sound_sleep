import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import '../models/sleep_record.dart';
import '../theme/app_theme.dart';

class WeeklyBarChart extends StatelessWidget {
  final List<SleepRecord> records;

  const WeeklyBarChart({super.key, required this.records});

  @override
  Widget build(BuildContext context) {
    final data = _buildData();
    return SizedBox(
      height: 200,
      child: BarChart(
        BarChartData(
          alignment: BarChartAlignment.spaceAround,
          maxY: 12,
          barTouchData: BarTouchData(
            touchTooltipData: BarTouchTooltipData(
              getTooltipItem: (group, groupIndex, rod, rodIndex) {
                return BarTooltipItem(
                  '${rod.toY.toStringAsFixed(1)}h',
                  const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
                );
              },
            ),
          ),
          titlesData: FlTitlesData(
            show: true,
            bottomTitles: AxisTitles(
              sideTitles: SideTitles(
                showTitles: true,
                getTitlesWidget: (value, meta) {
                  final index = value.toInt();
                  if (index < 0 || index >= data.length) return const SizedBox.shrink();
                  return Padding(
                    padding: const EdgeInsets.only(top: 8),
                    child: Text(
                      data[index].label,
                      style: TextStyle(fontSize: 11, color: Colors.grey.shade600),
                    ),
                  );
                },
                reservedSize: 30,
              ),
            ),
            leftTitles: AxisTitles(
              sideTitles: SideTitles(
                showTitles: true,
                reservedSize: 32,
                getTitlesWidget: (value, meta) {
                  if (value % 3 != 0) return const SizedBox.shrink();
                  return Text(
                    '${value.toInt()}h',
                    style: TextStyle(fontSize: 11, color: Colors.grey.shade500),
                  );
                },
              ),
            ),
            topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
            rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          ),
          borderData: FlBorderData(show: false),
          gridData: FlGridData(
            show: true,
            drawVerticalLine: false,
            horizontalInterval: 3,
            getDrawingHorizontalLine: (value) => FlLine(
              color: Colors.grey.shade200,
              strokeWidth: 1,
            ),
          ),
          barGroups: data.asMap().entries.map((entry) {
            return BarChartGroupData(
              x: entry.key,
              barRods: [
                BarChartRodData(
                  toY: entry.value.hours,
                  color: entry.value.hours >= 7
                      ? AppTheme.primaryBlue
                      : AppTheme.lightBlue.withValues(alpha: 0.6),
                  width: 20,
                  borderRadius: const BorderRadius.vertical(top: Radius.circular(6)),
                ),
              ],
            );
          }).toList(),
        ),
      ),
    );
  }

  List<_BarData> _buildData() {
    final now = DateTime.now();
    final result = <_BarData>[];
    for (int i = 6; i >= 0; i--) {
      final date = DateTime(now.year, now.month, now.day).subtract(Duration(days: i));
      final record = records.where((r) =>
          r.date.year == date.year &&
          r.date.month == date.month &&
          r.date.day == date.day).firstOrNull;
      final hours = record != null ? record.durationMinutes / 60.0 : 0.0;
      final label = DateFormat('E', 'zh_CN').format(date);
      result.add(_BarData(label: label.length > 2 ? label.substring(0, 2) : label, hours: double.parse(hours.toStringAsFixed(1))));
    }
    return result;
  }
}

class _BarData {
  final String label;
  final double hours;
  _BarData({required this.label, required this.hours});
}
