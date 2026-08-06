# 爽种校准台

这是一个独立的开发审核页，不属于正式应用。它调用已经构建好的 C++ CLI 生成
`daily-good-v0` 样本，然后在浏览器中逐条标记“爽 / 一般 / 不爽”。第二个页面
用于给全部伊甸可用 Q3 主动、被动道具打 `0–4` 分。

双击 `start.cmd`，或运行：

```powershell
py -3 tools/daily-review-webui/server.py
```

默认打开 <http://127.0.0.1:8765/>。需要指定内核或游戏目录时：

```powershell
py -3 tools/daily-review-webui/server.py `
  --exe build/native/IsaacSeedSeeker.exe `
  --game-dir "F:/steam/steamapps/common/The Binding of Isaac Rebirth"
```

每日样本审核结果只保存在当前页面内存中；点击“导出审核 JSON”后才会写入下载
文件。Q3 道具评分会自动保存到 `output/daily-good-q3-ratings.json`，也可以从页面
另行导出。
