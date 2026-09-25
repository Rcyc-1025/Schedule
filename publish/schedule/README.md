# 日程助手 ScheduleWidget

一个轻量的 Windows 桌面日程小组件，纯 C + Win32 API + GDI+ 编写，无任何运行时依赖（静态链接 `/MT`，单个 exe 即可运行）。常驻桌面右下角，提供月历、当日议程、定时提醒与日程文件导入。

## 功能特性

- **月历视图**：标记有日程的日期，高亮今天与选中日，左右翻月，「今」一键回到今日。
- **当日议程**：按时间列出当天所有日程，长列表可滚动。
- **收起态 pill**：可折叠成小胶囊，自动轮播当天日程；鼠标悬停显示完整备注，滚轮可手动切换。
- **定时提醒**：可设置日程开始前提醒，到点系统弹窗通知。
- **循环日程**：支持按天 / 周 / 月 / 年等周期重复。
- **自动清理过期日程**：启动时与运行中每 60 秒巡检；循环事件永不删除，全天事件在结束日次日才移除。
- **文件导入**：支持 `.ics` / `.csv` / `.xlsx` / `.json`，可选择「覆盖」或「追加」，也可拖放文件导入。
- **开机自启**、单实例运行、高 DPI 自适应、深色主题。

## 目录结构

```
schedule/
├── src/                  全部源码与资源（.c/.h/.rc/.ico/.manifest）
├── third_party/cJSON/    第三方 JSON 解析库（MIT，含 LICENSE）
├── tools/                自测程序与测试构建脚本
├── build.bat             一键构建脚本（自动探测 MSVC）
├── .clangd               clangd 编辑器配置
├── compile_flags.txt     clangd 编译标志
└── .gitignore
```

## 环境要求

- **Windows 10 / 11**（x64）
- **Visual Studio 2019 或更高版本**（需勾选「使用 C++ 的桌面开发」，提供 MSVC 编译器与 Windows SDK）
  - 也支持仅安装 [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)。
- 构建脚本会通过 `vswhere` **自动查找** VS 安装路径，无需手工配置；命令行构建不依赖 IDE。

## 一键构建

在项目根目录双击或在命令行执行：

```bat
build.bat
```

成功后产物位于：

```
release\ScheduleWidget.exe
```

直接双击该 exe 即可运行（无需安装、无需附带 dll）。

> 如果脚本提示找不到 vcvars64.bat，请确认已安装 MSVC C++ 工具链；也可在「x64 Native Tools Command Prompt for VS」中手动执行 `build.bat`。

## 使用说明

运行后窗口出现在屏幕右下角，右键窗口或点击右上角「⋯」打开菜单：

| 操作 | 说明 |
|---|---|
| 新建日程 | 编辑标题、日期/时间、备注、颜色、提醒、重复规则 |
| 导入日程文件… | 选择或拖入 ics/csv/xlsx/json；已有日程时可选**覆盖**或**追加** |
| 导出日程… | 导出为 JSON 备份 |
| 恢复备份（JSON）… | 从之前导出的 JSON 恢复 |
| 开机自启 | 切换登录 Windows 后自动运行 |
| 清除所有日程 | 清空（不可撤销，操作前建议先导出备份） |

交互细节：

- **收起态**：点击「—」折叠为胶囊；悬停可看完整备注，**滚轮上下**切换当天不同日程。
- **回到今日**：翻到其他月份或选中其他日期后，月份行会出现「今」按钮，点击即回到今天。
- **导入窗口的「示范文件」**按钮会生成 4 种格式的示例文件，可对照格式编辑后再导入。

### 导入文件格式

- **CSV**：建议表头为 `日期,时间,标题,备注`（表头可识别中英文，列顺序不限）；时间列留空即视为**全天日程**；兼容 UTF-8 BOM。
- **XLSX**：按第一张工作表、与 CSV 相同的列约定解析。
- **ICS**：标准 iCalendar（iCal）格式。
- **JSON**：日程数组，或 `{"events":[...]}` / `{"items":[...]}`；字段支持 `date`/`time`/`title`/`note`，也兼容 `start`/`summary`/`description` 等常见命名。

## 数据存储位置

所有数据保存在当前用户目录，卸载/删除 exe 不会自动清除：

```
%APPDATA%\ScheduleWidget\
├── schedule.json     日程数据（{"events":[...]}）
└── settings.json     设置（开机自启、主题等）
```

如需迁移或备份，直接复制整个 `ScheduleWidget` 文件夹即可。

## 运行自测

`tools/` 下包含 5 套自测（导入解析、全天事件、过期清理、xlsx 文本抽取、示范文件生成）。例如：

```bat
tools\build_test.bat test_purge
```

脚本会自动编译并运行对应测试，输出 ALL PASS 即通过。

## 第三方组件

- [cJSON](https://github.com/DaveGamble/cJSON)（MIT License），见 `third_party/cJSON/LICENSE`。

## 许可

本项目源码可自由学习与使用。
