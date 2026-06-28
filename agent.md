# Agent Notes

## 项目定位

这是一个用于 8051 智能小车的 Android 蓝牙控制软件。Android 端通过经典蓝牙 SPP 连接已经在系统里配对好的 HC-05/HC-06 等串口蓝牙模块，然后向单片机发送单字节 ASCII 指令。

`ref/main.c` 是已经拷进单片机的参考固件代码，后续修改 Android 端时要以它的串口协议为准。除非明确要改固件，不要随意改 `ref/main.c`。

## 目录结构

- `app/src/main/java/com/ajddwbo/carcontroller/MainActivity.kt`：唯一 Activity，使用原生 Android View 动态构建界面。
- `app/src/main/AndroidManifest.xml`：蓝牙权限和入口 Activity。
- `app/src/main/res/drawable/control_button.xml`：控制按钮背景样式。
- `app/src/main/res/values/strings.xml`：应用名。
- `ref/main.c`：8051 单片机端参考代码，包含电机、循迹、避障、蜂鸣器音乐、串口命令处理。
- `README.md`：面向使用者的简要说明。

## 构建环境

- Android Gradle Plugin：`8.7.3`
- Kotlin Android 插件：`2.0.21`
- Gradle Wrapper：`9.3.0`
- Android SDK：`compileSdk 35`，`targetSdk 35`，`minSdk 23`
- JVM toolchain：17

常用验证命令：

```powershell
.\gradlew.bat :app:assembleDebug
```

如果 Gradle 需要下载依赖或 wrapper 分发包，当前环境可能需要网络权限。

## Android 端行为

`MainActivity.kt` 直接创建 UI，不使用 XML layout、Compose 或 Fragment。界面包含：

- 状态文本
- 已配对蓝牙设备下拉框
- 刷新、连接、断开按钮
- 编辑布局开关
- 按钮大小滑杆
- 恢复默认按钮位置
- 控制面板和 7 个控制按钮

按钮布局会保存到 `SharedPreferences("layout")`：

- 每个按钮保存 `${command}_x` 和 `${command}_y`
- 全局按钮大小保存为 `button_size`

方向按钮是点动逻辑：

- 按下 `F` / `B` / `L` / `R`
- 松开或取消时自动发送 `S`

非点动按钮在按下时只发送一次，松开时不会补发 `S`：

- `S`
- `A`
- `M`

蓝牙连接使用经典 SPP UUID：

```text
00001101-0000-1000-8000-00805F9B34FB
```

Android 12 及以上需要 `BLUETOOTH_CONNECT` 运行时权限。当前 app 只列出已配对设备，不做扫描；用户需要先在系统蓝牙设置里配对模块。

## 单片机串口协议

单片机串口参数在 `ref/main.c` 的 `uart_init()` 中设置：

- 8051 串口方式 1
- Timer1 方式 2
- `TH1 = 0xFD`
- 以 `FOSC_HZ = 11059200UL` 计算，对应 9600 baud

Android 端每次发送一个 ASCII 字符，单片机在串口中断中把 `SBUF` 保存到 `bluetooth_cmd`，再由主循环 `handle_bluetooth_cmd()` 处理。

命令表：

| 命令 | 单片机行为 | Android 当前按钮 |
| --- | --- | --- |
| `F` / `f` | 进入蓝牙手动模式，前进 | 前进 |
| `B` / `b` | 进入蓝牙手动模式，后退 | 后退 |
| `L` / `l` | 进入蓝牙手动模式，左转 | 左转 |
| `R` / `r` | 进入蓝牙手动模式，右转 | 右转 |
| `S` / `s` | 停止，退出自动循迹和手动模式 | 停止、方向键松开 |
| `A` / `a` | 进入自动循迹模式 | 循迹 |
| `M` / `m` | 停车并播放蜂鸣器音乐；播放中再次收到 `M` 可中断 | 音乐 |
| `+` | 增加左轮差速校准值 | Android 尚未提供按钮 |
| `-` | 减少左轮差速校准值 | Android 尚未提供按钮 |
| `0` | 左轮差速校准归零 | Android 尚未提供按钮 |

如果要新增 Android 按钮，优先复用 `Control(command, label, momentary, defaultX, defaultY)`，并确认 `ref/main.c` 的 `handle_bluetooth_cmd()` 已支持对应字符。

## 单片机端关键逻辑

硬件引脚：

- `P1.0` / `P1.1`：右电机控制
- `P1.2` / `P1.3`：左电机控制
- `P1.4`：右侧循迹红外
- `P1.5`：左侧循迹红外
- `P1.6`：左前避障红外
- `P1.7`：右前避障红外
- `P3.2`：S4 物理按键
- `P3.6`：蜂鸣器

模式状态：

- `car_enabled = 1`：自动循迹模式
- `manual_control = 1`：蓝牙手动模式
- `manual_action`：当前手动动作，默认 `S`
- `bluetooth_cmd_ready` / `bluetooth_cmd`：串口命令接收标记和内容

主循环顺序：

1. 扫描 S4 物理按键，切换自动循迹。
2. 处理蓝牙命令。
3. 先执行前方避障检测，避障优先级最高。
4. 如果没有障碍，再执行自动循迹、蓝牙手动动作或停车待机。

注意：避障逻辑会接管底盘控制。即使 Android 正在发送方向命令，`check_and_handle_obstacle()` 检测到障碍时也会优先停车、后退或转向。

## 修改建议

- 保持蓝牙命令为单字节 ASCII，避免在 Android 端改成带换行、JSON 或多字节中文命令。
- 新增控制命令时，同时更新 `README.md`、本文件和 `ref/main.c` 的命令表。
- UI 仍是原生 View 动态构建，除非明确重构，不要引入 Compose。
- 已配对设备列表可能为空时要保留友好提示；不要改成扫描流程，除非同时处理 Android 版本权限。
- 方向键点动行为很重要，松手发送 `S` 是防止小车持续运动的安全逻辑。
- `ref/main.c` 依赖 8051/Keil C 风格语法，不应按标准桌面 C 编译器的习惯重写。

## 已知注意点

- `README.md` 里的示例打开路径可能与当前工作区路径不同；当前工作区是 `E:\ajddwbo\android_car_controller`。
- `ref/main.c` 中还有 `+`、`-`、`0` 三个差速校准命令，但 Android UI 当前没有入口。
- 本项目当前没有单元测试；最小验证是成功构建 debug APK，并用已配对蓝牙模块实机验证发送指令。
