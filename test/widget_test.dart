import 'package:flutter_test/flutter_test.dart';
import 'package:haveyousleep/main.dart';

void main() {
  testWidgets('App smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const AppBootstrap());
    await tester.pumpAndSettle();
    expect(find.text('睡了吗'), findsOneWidget);
  });
}
