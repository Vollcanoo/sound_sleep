import 'package:flutter/material.dart';
import 'dart:math';

class SleepScoreRing extends StatefulWidget {
  final int score; // 0-100
  final double size;
  final Color? ringColor;
  final Color? backgroundColor;
  final bool animate;

  const SleepScoreRing({
    super.key,
    required this.score,
    this.size = 120,
    this.ringColor,
    this.backgroundColor,
    this.animate = true,
  });

  @override
  State<SleepScoreRing> createState() => _SleepScoreRingState();
}

class _SleepScoreRingState extends State<SleepScoreRing>
    with SingleTickerProviderStateMixin {
  late AnimationController _controller;
  late Animation<double> _animation;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    );
    _animation = Tween<double>(begin: 0, end: widget.score / 100)
        .animate(CurvedAnimation(parent: _controller, curve: Curves.easeOutCubic));
    if (widget.animate) {
      _controller.forward();
    } else {
      _controller.value = 1.0;
    }
  }

  @override
  void didUpdateWidget(SleepScoreRing oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.score != widget.score) {
      _animation = Tween<double>(begin: 0, end: widget.score / 100)
          .animate(CurvedAnimation(parent: _controller, curve: Curves.easeOutCubic));
      _controller.forward(from: 0);
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  Color _scoreColor() {
    if (widget.score >= 90) return const Color(0xFF4CAF50);
    if (widget.score >= 75) return const Color(0xFF42A5F5);
    if (widget.score >= 60) return const Color(0xFFFF9800);
    return const Color(0xFFEF5350);
  }

  String _scoreLabel() {
    if (widget.score >= 90) return '优秀';
    if (widget.score >= 75) return '良好';
    if (widget.score >= 60) return '一般';
    return '较差';
  }

  @override
  Widget build(BuildContext context) {
    final ringColor = widget.ringColor ?? _scoreColor();
    final bgColor = widget.backgroundColor ?? ringColor.withValues(alpha: 0.15);

    return AnimatedBuilder(
      animation: _animation,
      builder: (context, child) {
        return SizedBox(
          width: widget.size,
          height: widget.size,
          child: CustomPaint(
            painter: _RingPainter(
              progress: _animation.value,
              ringColor: ringColor,
              bgColor: bgColor,
              strokeWidth: widget.size * 0.1,
            ),
            child: Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    '${(widget.score * _animation.value).round()}',
                    style: TextStyle(
                      fontSize: widget.size * 0.28,
                      fontWeight: FontWeight.bold,
                      color: ringColor,
                    ),
                  ),
                  Text(
                    _scoreLabel(),
                    style: TextStyle(
                      fontSize: widget.size * 0.11,
                      color: ringColor,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}

class _RingPainter extends CustomPainter {
  final double progress;
  final Color ringColor;
  final Color bgColor;
  final double strokeWidth;

  _RingPainter({
    required this.progress,
    required this.ringColor,
    required this.bgColor,
    required this.strokeWidth,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = (size.width - strokeWidth) / 2;

    // Background ring
    final bgPaint = Paint()
      ..color = bgColor
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeWidth
      ..strokeCap = StrokeCap.round;
    canvas.drawCircle(center, radius, bgPaint);

    // Progress ring
    final ringPaint = Paint()
      ..color = ringColor
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeWidth
      ..strokeCap = StrokeCap.round;
    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      -pi / 2,
      2 * pi * progress,
      false,
      ringPaint,
    );
  }

  @override
  bool shouldRepaint(_RingPainter oldDelegate) =>
      oldDelegate.progress != progress;
}
