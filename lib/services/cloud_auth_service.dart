// CloudBase 使用匿名 accessToken 认证，不需要单独的 auth service
// 此文件保留为空壳，以免其他文件引用时报错
// 未来如需用户登录（邮箱/手机号），可在此扩展 CloudBase 用户认证

class CloudAuthService {
  bool get isConfigured => false;
}
