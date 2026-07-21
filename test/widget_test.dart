import 'package:flutter_test/flutter_test.dart';
import 'package:haveyousleep/main.dart';
import 'package:haveyousleep/services/auth_service.dart';

void main() {
  testWidgets('App smoke test', (WidgetTester tester) async {
    final authService = AuthService();
    await authService.init();
    await tester.pumpWidget(AppBootstrap(authService: authService));
    await tester.pumpAndSettle();
    expect(find.text('睡了吗'), findsOneWidget);
  });
}
