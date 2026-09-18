# 屏幕黑屏 —— 哪些已确认、哪些还不确定、该烧哪个

**日期：** 2026-09-18  
**对象：** Parker 与冯  
**相关文档：** [`LATEST_UPDATE.md`](LATEST_UPDATE.md)（按钮联调本身）、[`INTEGRATION_GUIDE_zh.md`](INTEGRATION_GUIDE_zh.md)（接线与协议约定）

## 更新 —— 9 月 18 日下午第二次尝试：先确认烧到了哪块板

**反馈：** 编译成功、上传成功、屏幕仍然全黑 —— 但**面包板上亮起来的灯明显变多了**，之前只有两个。

**首要推测：固件烧到了 Freenove 控制板上，不是 CrowPanel。**

面包板是接在 Freenove 上的。烧写屏幕没有任何理由改变面包板的表现，所以两个灯变成很多个灯，说明**新烧进去的固件正在驱动 Freenove 的 GPIO**。而驱动 GPIO 正是屏幕固件在做的事：它把 15、7、6、5、4、9、46、3、8、16、1、14、21、47、48、45 当作 16 位 RGB 数据总线，再加 41（DE）、40（VSYNC）、39（HSYNC）、0（PCLK），以 15 MHz 时钟推送。这几乎覆盖了整个低位 GPIO 区间，跟你接 LED 的引脚几乎必然重叠。那些灯是被像素数据点亮的。

**快速确认：** 那些灯是偏暗、闪烁、亮度不均匀的吗？那就是视频信号，不是正常的逻辑电平。如果灯是稳定且看起来正常的，则要往别处找。

**为什么没有任何报错：** 两块板都是 ESP32-S3。esptool 只在芯片**系列**不匹配时才拒绝，所以把 ESP32-S3 的镜像烧到另一块 ESP32-S3 上会完全静默地成功。

### 重新上电之前先处理两件事

1. **控制器固件很可能已被覆盖。** 如果上传落在了 Freenove 上，你的代码已经没了，需要重新烧回去。请先确认，不要假定板子没事。
2. **先把泵和加热负载断开。** 20 个 GPIO 被当成视频总线时会高速翻转。如果其中任何一个接了继电器或功率驱动，实物负载会被随机切换。

### 决定性检查：MAC 地址

esptool 每次上传都会打印这两行：

```
Chip is ESP32-S3 (QFN56) (revision v0.2)
MAC: 68:b6:b3:xx:xx:xx
```

**这个 MAC 唯一标识板子。** 请把那次成功上传的完整日志发来。然后只插 Freenove，用同样方式读一次它的 MAC。两者相同，就说明屏幕从未收到过任何东西。

### 加固后的烧写步骤

故障根源是串口选择，所以把这个选择彻底去掉：

1. **把 Freenove 的 USB 线整根拔掉** —— 不是「在菜单里选另一个口」，是物理拔掉。
2. USB-C **只插 CrowPanel**。
3. 运行 `pio device list`，确认**只有一个**串口，记下名字。
4. 上传时显式指定这个口，不让工具去猜：
   ```bash
   pio run -e panel-mock -t upload --upload-port /dev/cu.usbmodemXXXX
   ```
5. 屏幕应出现 §4 第 4 步描述的画面。

### 照片里两个需要确认的接线问题

- **我看不到 CrowPanel 上插着 USB-C。** 屏幕现在靠什么供电？如果从 Freenove 下来的四根杜邦线（黄/白/黑/红）只走了 TX、RX、GND，屏幕根本没有 5 V。请确认它的电源红灯是亮的。
- **那根带白色 HY2.0-4P 连接器的彩排线正躺在桌上没插。** 那是屏幕的 UART0 线。请确认链路实际走的是哪一路：那根线，还是那四根杜邦线。

---

## 0. 结论先行

这是两个独立的问题，区分开很重要。

1. **给 Parker 的那份代码本身不可能工作。** `Mayhaps` 是我们的固件被删掉了显示驱动**以及整个测试按钮功能**之后的版本。它点不亮屏幕；而且即使屏幕亮了，`LATEST_UPDATE.md` 里的按钮测试也永远通不过。请改用 [`AutoHydroPanel/`](../AutoHydroPanel)。
2. **但我们还不知道他那块屏为什么是黑的**，因为有三种完全不同的故障从外观上一模一样。§2 给出一个能区分它们的测量步骤。在动任何接线之前请先做这一步。

另外有一件必须对 Parker 交底的事：**我们的显示驱动从未在真机上跑过。** 见 §5。

## 1. 现象

- CrowPanel：电源红灯亮，**屏幕全黑**，背光没有任何亮度
- Freenove 控制板：两个灯亮
- 插不插 UART 线，屏幕都没有显示

需要说明的是，Freenove 上两个灯亮只表示控制器自己的固件在运行。两板之间的 4 线 UART0 是一根**传输 JSON 文本的数据线**，不是显示总线。CrowPanel 上**自带一颗 ESP32-S3**（带金属屏蔽罩的模块），面板界面跑在那颗芯片上，所以控制器发什么内容都不可能让 LCD 显示东西。

## 2. 先做这一步：到底是四种故障里的哪一种

「电源灯亮、屏幕全黑」同时符合下面四种情况，光看照片无法区分：

| 编号 | 原因 | 可能性 |
|------|------|--------|
| A | 芯片里压根没有面板固件 —— 没烧，或者烧写失败了 | 高 |
| B | 烧进去的是 `Mayhaps`，在 `ui::init()` 里就崩了，什么都没画出来 | 高 |
| C | 固件正常运行，但 RGB 时序或背光配置不对 | 确实存在 —— 见 §5 |
| D | 上传落到了 **Freenove** 而不是 CrowPanel —— 两块都是 ESP32-S3，所以静默成功 | **第二次尝试后成为首要推测** —— 见文首更新 |

情况 D 对下面这个测量是不可见的：你测的是屏幕，而固件在另一块板上。所以先用文首的 MAC 检查把 D 排除掉，再用下面的测量。

**测量方法。** 把 4 针 UART0 线从 CrowPanel 上拔掉，USB-C 只连 CrowPanel，然后用串口监视器以 115200 打开。

- **完全没有输出、串口都不出现** → 情况 A。芯片里什么都没有，或者根本没被烧进去。
- **每秒左右重复出现 `rst:0x... boot:0x...`** → 情况 B。它在启动崩溃循环里。
- **只打印一次启动信息然后安静** → 固件在跑。属于情况 C，问题在显示侧，不在链路。

我们的固件刻意什么都不打印（UART0 归 Parker 用），但 ESP32-S3 的 ROM 引导程序一定会打印，所以不管芯片里装的是哪个固件，这个测试都有效。

## 3. `Mayhaps` 到底是什么

它不是一份未完成的移植，而是我们能用的固件被删掉了几块。除了删除的部分，文件内容和 `firmware/` 完全一致，而时间戳比我们的**更晚**，说明它是从我们的代码派生出来的。

| 被删掉的 | 证据 | 后果 |
|---------|------|------|
| 显示驱动实现 | `display_driver.h` 在，`display_driver.cpp` 不在。`Mayhaps.ino` 第 535 行用注释占了初始化的位置：`// (e.g., gfx.begin(), touch.begin(), lv_init(), lv_display_create())` | 没人配置 RGB 总线、GT911 触摸芯片和 **GPIO2 背光**。`lv_init()` 从未被调用，`ui::init()` 在 LVGL 还不存在时就去操作它。必然黑屏 |
| **整个测试按钮功能** | `types.h` 少了 `test_button` / `test_count`；`app_state.cpp` 少了 `updateTestButton()`、`testLampOn()`、`consumeTestPresses()`、`consumeTestCounterReset()`；`app_config.h` 少了 `kTestLampHoldMs`；`mock_link.h` 少了 `testButtonAt()` | **联调测试不可能通过。** `LATEST_UPDATE.md` 让 Parker 按 5 次按钮、看屏幕数到 5，而这份代码完全没有处理这两个字段，屏上永远不会有反应 —— 他会很合理地开始怀疑自己的接线或 JSON |
| UART 接收与协议层 | 没有 `protocol.cpp`、没有 `main.cpp`，`loop()` 里没有 `Serial.read()`；命令回调全是空 lambda | 既不收也不发。就算屏幕正常，也会永远停在 `NO LINK` |

还有一个构建配置问题，能解释很多事。真正的 `platformio.ini` **在**那个文件夹里 —— 在 `Mayhaps/data/` 里面。PlatformIO 不会去 `data/` 找配置，而 Arduino IDE 把 `data/` 当作 SPIFFS 上传目录，所以**没有任何工具会读它**。而那个文件正是开启 `LV_COLOR_DEPTH=16`、`LV_MEM_SIZE` 和 montserrat 20/28/48 字体的地方。代码里用到这三种字号的地方有 **19 处**，而 LVGL 默认配置只开了 montserrat 14。

**所以 `Mayhaps` 很可能从头到尾没编译成功过**，报错大概是 `'lv_font_montserrat_28' was not declared in this scope`。这一点需要向 Parker 确认：他的编译到底有没有通过。如果通过了，说明他手写了一份 `lv_conf.h`，那我们需要知道。

## 4. 该烧什么

用 [`AutoHydroPanel/`](../AutoHydroPanel)。那是完整固件 —— 显示驱动、UART、协议、测试按钮全都在 —— 放在一个扁平文件夹里，**PlatformIO 和 Arduino IDE 都能编**。构建步骤见 [`AutoHydroPanel/README.md`](../AutoHydroPanel/README.md)。

1. **把 4 针 UART0 线从 CrowPanel 上拔掉。** 这块板的 UART0 和 USB-C 烧写口共用，控制器的线插着时烧写会失败，也可能干扰启动。
2. **物理拔掉 Freenove 的 USB 线，USB-C 只插 CrowPanel。** 两块板都是 ESP32-S3，烧错那块不会有任何报错 —— 我们认为第二次尝试就是这么错的。用 `pio device list` 确认只有一个串口，并用 `--upload-port` 显式指定它。
3. **先烧 mock 版本：** `pio run -e panel-mock -t upload`。它自己生成遥测数据，不接控制器就能自证屏幕、触摸和测试指示都正常。
4. **应当看到：** 顶栏 `AUTO HYDRO`，右侧先短暂 `NO LINK`，随后数值开始变化 —— 水分在 45 / 55 / 60% 区间扫动，`TEST` 标记约在第 12、25、26、45 秒闪烁。看到这些就说明屏幕已经没问题，之后所有故障都是链路问题。
5. **然后**接回 UART0（TX↔RX 交叉、共地），烧正式版本：`pio run -e panel -t upload`。

**不要：** 烧 `Mayhaps` · 把面板固件烧进 Freenove · 把控制器代码烧进 CrowPanel。

## 5. 交底：显示驱动未经真机验证

`AutoHydroPanel` 能编译通过，逻辑也在主机上做了单元测试，但**它从未在真实的 CrowPanel 上运行过。** 我们这边没有这块板。`display_driver.cpp` 里的引脚表和时序来自 Elecrow 的例程和数据手册，这一点文件自己的头部注释就写着：

```1:5:firmware/src/display_driver.cpp
// CrowPanel ESP32 HMI 7.0" (Basic, ASIN B0F8NFFH29) display + touch bring-up.
//
// Pin map is from docs/FEASIBILITY_ASSESSMENT_zh.md §2.1. If the screen stays
// white, verify these against the Elecrow example for your board revision
// before touching anything else — the RGB timing is the usual culprit.
```

所以 §2 里的情况 C 是真实可能，不是走个形式。换一个板子版本后最可能出错的三个值：

1. **RGB 时序** —— 行/场同步的 front porch、pulse width、back porch，以及 `freq_write`（当前 15 MHz）。这些值不对会得到白屏、滚动或花屏，而不是黑屏。
2. **GT911 触摸地址** —— 有的批次是 `0x5D`，有的是 `0x14`。地址错了表现为屏幕正常但触摸无效。
3. **背光** —— 由 GPIO2 驱动。如果逻辑看起来正常但面板不亮，在 `gfx.init()` 之后临时加一句 `gfx.setBrightness(255)`，就能区分是背光问题还是渲染问题。

给 Parker 的说明：如果烧了 mock 版本后得到的是白屏或花屏而不是黑屏，那其实是好消息 —— 芯片在跑，只需要修时序。请拍张照片并告知板子版本，我们对照该版本的 Elecrow 例程来改。

## 6. 如果按 §4 做完还是黑屏

按顺序排查：

1. **上传真的成功了吗？** 看有没有 `Writing at 0x...` 和 `Hash of data verified`。静默失败的上传是最常见的原因。
2. **串口选对了吗？** 把 Freenove 整个拔掉，重新列一次串口。
3. **重做 §2 的测量。** 这时它能告诉你是不是在启动循环里。
4. **PSRAM。** 这块是 **N4R8**：4 MB Flash、8 MB **OPI** PSRAM。800×480 帧缓冲没有它放不下。`platformio.ini` 已设好 `board_build.arduino.memory_type = qio_opi` 和 `-DBOARD_HAS_PSRAM`；用 Arduino IDE 则必须手动选 **OPI PSRAM**，否则 RGB 初始化失败并启动循环。
5. **分区表。** LVGL 加 Arduino 装不进默认 app 分区，我们用 `huge_app.csv`。Arduino IDE 里选 **Huge APP**。
6. **然后才是 §5 的那三个值。**

调试时有个硬件细节要留意：PCLK 接在 **GPIO0** 上，而那同时是 BOOT 引脚。这是 Elecrow 的设计而非我们的选择，但它意味着复位时任何把 GPIO0 拉低的东西都会让板子进入下载模式而不是运行固件。
