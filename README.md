# 魔灯工作台 / Magic Lantern Workbench

魔灯（Magic Lantern）录制的 **MLV 视频** 的全中文处理工作台。

基于开源项目 [MLV-App](https://github.com/ilia3101/MLV-App)（作者 [ilia3101](https://github.com/ilia3101)）二次开发，**遵循 GPL-3.0 协议**。

- 上游基线：commit [`e25c1fa`](https://github.com/ilia3101/MLV-App/commit/e25c1fa)（Fix rcd and dcb debayer for export）
- 本仓库相对上游：**19 个文件修改，+3002 / -236 行**，另新增 17 个文件
- 上游原始说明保留在 [README_UPSTREAM.md](README_UPSTREAM.md)

---

## 这个版本做了什么

### 1. 完整中文化

- 独立的中文翻译文件（`mlvapp_zh_CN.ts` / `.qm`），**878 条译文**
- 界面、菜单、对话框、状态提示、导出选项全部中文
- 保留 144 条专业术语原文（编解码器名、色彩空间名、算法名、快捷键符号），**故意不翻译**——这些词行业里就用英文

### 2. 界面内即时切换语言（不用重启）

上游没有 i18n 基础设施，本版从零加了：

- 全局 `QTranslator` 常驻
- 4 个窗口（主窗口 / AI 面板 / 图片导出 / LLM 设置）响应 `QEvent::LanguageChange`，一次刷新所有 `.ui` 控件
- 切换语言**窗口不关、素材不丢、参数保留**

**快捷键**：

| 快捷键 | 作用 |
|---|---|
| `Ctrl+Alt+Z` | 切中文 |
| `Ctrl+Alt+E` | 切英文 |

菜单路径：`语言 / Language → 中文 / English`

### 3. AI 智能调色面板（可选功能）

用自然语言描述想要的效果，由大语言模型翻译成调色参数并自动应用。

- 任何 **OpenAI 兼容**的 `/v1/chat/completions` 端点都能接
- 支持本地部署的模型（LM Studio、Ollama 之类）或云端 API
- **默认不预填任何端点**——你需要自己填

> 这是可选功能。不填端点，其余所有功能（转码、调色、导出）都正常使用。

### 4. 12 步新手导览

首次启动会引导一圈核心功能；随时可从菜单重看。

### 5. 其他修复

- **中文路径**打开 MLV/CR2 不再失败（上游 Windows 分支用 `toLatin1()` 把中文转成 `?`，本版改 `toLocal8Bit()`，共 21 处）
- **拖放 MLV/CR2 进窗口**能用了（上游 `dragEnterEvent` 是空实现，没有 `acceptProposedAction`，Qt 根本不会触发 drop）
- 导入 CR2 时不再弹两次文件夹选择框

---

## 系统要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11，64 位 |
| 显卡 | 无硬性要求。核显也能跑，有独显解码更快 |
| 内存 | 4 GB 起；处理 4K MLV 建议 8 GB+ |
| 磁盘 | 解压后约 250 MB（不含素材） |

> 上游也支持 macOS / Linux，但**本仓库的预编译包只提供 Windows 版**。

---

## 下载与使用

1. 到 [Releases](../../releases) 下载最新的 zip
   - `魔灯工作台_vX.X_日期.zip` —— 中文版
   - `MagicLanternWorkbench_EN_vX.X_日期.zip` —— 英文版
2. **解压到任意路径**（本版修好了中文路径问题）
3. 双击里面的 `魔灯工作台.exe`（英文版是 `MagicLanternWorkbench.exe`）

⚠️ **不要只把 exe 单独拷走**，它依赖同目录的 DLL 和 `platforms/` 等文件夹，必须整个目录一起用。

### 基本流程

```
打开 MLV → 调色 → 导出
```

- 打开：`文件 → 打开` 或直接把 `.MLV` 拖进窗口
- 导出：`文件 → 导出`，可选 ProRes / DNxHD / H.265 / CinemaDNG / TIFF / PNG / JPEG
- 导出 **DNG** 时双感光度（Dual ISO）会**自动合并**（条纹消除，但合并质量略逊于 cr2hdr 专用工具）
- 导出 **JPG / PNG / TIFF** 才带调色；DNG 是原始 RAW，不含调色（与相机 RAW 逻辑一致）

---

## AI 智能调色怎么配置

打开 `AI 智能调色` 面板 → 填写：

| 字段 | 说明 |
|---|---|
| 端点 | 例如 `https://api.deepseek.com/v1`、`http://127.0.0.1:1234/v1`（LM Studio）、`http://127.0.0.1:11434/v1`（Ollama） |
| 模型 | 端点支持的模型名，例如 `deepseek-chat` |
| API 密钥 | 云端服务需要；本地模型通常留空 |
| 超时 / 输出上限 | 推理型模型建议把输出上限调到 2000 以上 |

填好后回面板输入自然语言，比如：

> 天空更蓝一点，人物提亮，加一点胶片颗粒

模型会返回结构化参数，确认后应用到当前帧。

---

## 从源码编译

环境：**Qt 5.15.2 + MinGW 8.1**（Windows）

```bash
# 1. 生成翻译文件
lrelease platform/qt/mlvapp_zh_CN.ts

# 2. 生成 Makefile
mkdir build && cd build
qmake ../MLV-App-src/platform/qt/MLVApp.pro

# 3. 编译
mingw32-make -j4
```

产物在 `build/release/MLVApp.exe`。

⚠️ 几个坑（都是我们踩过的）：

- `main.cpp` 里翻译加载顺序是「磁盘优先 → 内嵌资源兜底」。改完 `.qm` **必须重新编译**，因为 rcc 把 `.qm` 编进 exe 了
- Makefile **不跟踪 `.qm` 变化**，只改翻译不重新编译，界面不会变。要强制生效：删 `rcc/qrc_i18n.cpp` 和 `obj/qrc_i18n.o` 再 make
- 改 `.ui` 后如果 `ui_*.h` 没重新生成，菜单改动不会进 exe。删掉对应 `ui_*.h` 再编

---

## 许可

**GPL-3.0**（与上游一致），全文见 [LICENSE](LICENSE)。

这意味着：

- ✅ 可以自由使用、修改、分发，**包括商用**
- ⚠️ 但一旦分发（发 GitHub、发给别人、卖钱），**必须同时提供完整源码**
- ⚠️ 不能改成闭源发布
- 本版新增文件（`AiGradingDialog.*`、`GuidedTourOverlay.*`、`ImageExportDialog.*`、`LlmSettingsDialog.*`）同样以 GPL-3.0 授权

---

## 相关工具

如果你拍的是**双感光度 CR2 照片**（不是 MLV 视频），推荐用专门的 CR2 工具链：

- **cr2hdr**（David Milligan）—— 双感光度合并的权威实现，合并质量最好
- 本工具导出的 DNG 也能合并，但因为是按同 ISO 强制合并（CR2→MLV 转换时丢了 ISO 差值信息），暗部会略浑、带轻微绿偏

---

## 致谢

- [ilia3101](https://github.com/ilia3101) —— MLV-App 原作者，做了全部核心工作
- David Milligan —— cr2hdr / Dual ISO 算法
- MLV-App 的所有贡献者
- 魔灯社区

本仓库只做中文化和体验改进，**核心解码与处理管线全部来自上游**。
