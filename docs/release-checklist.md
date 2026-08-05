# Windows 发行检查清单

## 每次测试包

- [ ] `pyproject.toml` 中的版本号已经确认。
- [ ] `scripts/build-native.ps1 -Configuration Release -RunTests` 通过。
- [ ] `scripts/smoke-webui.ps1` 通过。
- [ ] 使用 `scripts/package-windows.ps1` 生成便携 ZIP。
- [ ] 从发行目录中的 EXE 再跑一次 WebUI 冒烟测试。
- [ ] 解压 ZIP，确认 EXE、使用说明、NOTICE 和第三方许可证均存在。
- [ ] 核对 `SHA256SUMS.txt`。
- [ ] 在一台没有 Python/Node.js/MinGW 的 Windows 机器上人工启动一次。

## 正式公开发布前

- [ ] 选择并添加项目主许可证；不要替用户或维护者推断许可证。
- [ ] 确认仓库中的标题图和其他自有视觉资产可以随项目公开分发。
- [ ] 确认版本号、README、使用说明和变更记录一致。
- [ ] 决定是否购买 Windows 代码签名证书；未签名包会触发 SmartScreen 信誉提示。
- [ ] 在 GitHub Actions 或干净的 Windows x64 环境中复现构建。
- [ ] 创建 Release、上传 ZIP 与 `SHA256SUMS.txt`，并从发布页重新下载验证。

## 可选安装器

当前程序是单个静态链接 EXE，便携 ZIP 已覆盖“解压即用”的主要场景。若需要开始菜单快捷方式、卸载入口和安装目录，可在安装 Inno Setup 后增加 Setup 构建；安装器不应额外捆绑 Python、Node.js 或 MinGW 运行时。

