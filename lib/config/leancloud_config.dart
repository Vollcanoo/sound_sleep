class LeanCloudConfig {
  // TODO: 替换为你在 LeanCloud 控制台获取的凭证
  // 控制台地址: https://console.leancloud.cn/
  // 路径: 应用 → 设置 → 应用凭证
  static const String appId = '';
  static const String appKey = '';
  static const String serverUrl = '';

  static bool get isConfigured =>
      appId.isNotEmpty && appKey.isNotEmpty && serverUrl.isNotEmpty;
}
