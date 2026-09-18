# Auto Hydro 控制板 ↔ 触屏界面 — 对接指南

**文档版本：** 1.5  
**日期：** 2026-09-18  
**状态：** Parker 已回复，v1 线路约定**已冻结**（原文见附录 C），另新增 **`setpoint_c`**（§6.2）与**联调测试按钮**（§6.4）。联调入口：**`docs/LATEST_UPDATE.md`**  
**读者：** 界面侧（Feng）为主；英文版 `INTEGRATION_GUIDE_en.md` 发给 Parker  
**配套：** 可行性评估见 `FEASIBILITY_ASSESSMENT_zh.md`（背景材料；**以本文件为接口实现依据**）

若与此前草稿冲突，**以本对接指南为准**。

发给 Parker 时请用 **`INTEGRATION_GUIDE_en.md`**，两份章节编号一致，方便对照。

---

## 0. 相对 Parker 交回版本的变更 —— 先看这一节

**有两项需要 Parker 写固件：测试按钮（先做这个）和 `setpoint_c`。** 其余要么是把他自己的答复写进约定，要么是界面侧的活。

| # | 变更 | Parker 要做什么 | 位置 |
|---|------|-----------------|------|
| 1 | **联调测试按钮** —— 面包板上的实体按钮，以可选字段 `test_button` + `test_count` 上报，点亮屏上的 TEST 标签 | **先做这一项：** 不依赖任何传感器就能验证整条线 | §6.4 |
| 2 | **telemetry 新增必须字段 `setpoint_c`（15–30）** —— Parker **当前实际持有的**温度设定值 | **每帧 telemetry 多发一个整数** | §6.2、§15.1 |
| 3 | 已把他的答复写入并冻结：3.3 V 电平、115200、1 Hz 心跳、必须 ACK、实际 `pump` 0/1、实测 `temperature_c`、越大越湿、他供 5 V 1–2 A、探头占空比约 150 ms | 核对措辞是否与他原意一致 | §15 |
| 4 | 新增四条验收项：设定值回读（A13、A14）与测试按钮（A15、A16） | 联调时一起跑 | §14 |
| 5 | 屏上产品名 **AUTO HYDRO**；不显示日期和时钟（离线设备） | 无 | §2 |

**为什么要测试按钮。** 界面固件已经写完，但两边都还没见过一个字节真的过线。按一下按钮点亮屏幕，就同时验证了接线、波特率、分帧和解析；万一不亮，要查的是一根线，而不是整个系统。它是可选且会自动消失的 —— 只要不发这两个字段，指示就不存在。

**`setpoint_c` 是怎么冒出来的。** 第 1 版界面固件已经写完，写的过程中暴露了这个缺口：telemetry 里有**实测**温度，却没有**设定值**。屏上没有掉电保存，刚上电的屏显示自己的默认 22 °C，而 Parker 可能正持着 26 °C —— 同一台机器上两个目标温度，谁都分不清哪个是真的。`setpoint_c` 就是他已经同意的 `pump` 字段在温度上的等价物，成本是每行多一个整数。完整规则见 §6.2。

---

## 1. 目的

架构已冻结：Parker 的 C++ 控制板与 7 吋 Elecrow CrowPanel（ESP32-S3 + LVGL）之间用 **本地 UART**、**每行一条 JSON** 通信，不联网。屏上产品名为 **AUTO HYDRO**。

本文约定：

- 谁负责什么
- 怎么接线
- 双方必须收发的 JSON
- 如何互不阻塞地并行开发
- 联调与验收怎么做

Parker 的答复已写入 **§15**，v1 按此冻结。

---

## 2. 分工

| 负责人 | 负责 | 不负责 |
|--------|------|--------|
| **Parker** | 全部传感器、水泵继电器、加热/温度执行、告警**判定**、控制板 UART JSON | 界面布局、触摸、LVGL |
| **Feng** | CrowPanel 固件：界面、触摸、JSON 收发、告警的**呈现** | 探头 ADC、继电器、闭环控制 |

屏幕几乎没有空闲 GPIO。**不要把传感器接到显示屏上。** 探头全部留在 Parker 板。

### v1 界面功能

1. 水泵开关 — 仅在操作者点「确认」后发命令。
2. 土壤**水分** %（来自 12 位 ADC）以及过高/过低状态（**由 Parker 计算**）。
3. 目标温度滑块 15–30 °C — 仅确认后发命令。**同时显示实测 `temperature_c`。**
4. 浮球报低水位时全屏闪烁 **Low Water**。

**界面外壳（v1）：** 标题 **AUTO HYDRO**（本系统无 steamer / 雾化器，不用 Aeroponic Life Support）。状态栏只显示 **LINK OK** / 通信中断。**不显示日期、不做时钟、不提供校时界面** —— 设备离线，MCU 时间会漂，显示日期只会误导。

电阻式**空气湿度** v1 **不上屏**。Parker 控制板会自动调节空气湿度。若以后双方同意，可以再加湿度设定滑块；**不进 v1 JSON**。

---

## 3. 拓扑

```
Parker 控制板 (C++)                              CrowPanel 7" (C++ / LVGL)
传感器、水泵、加热                                只做显示和触摸
        │                                              │
        │   UART 115200 8N1，3.3 V TTL（已确认）       │
        │   Parker TX ──► 屏 RX (IO44)                 │
        │   Parker RX ◄── 屏 TX (IO43)                 │
        │   GND ──────── GND                           │
        │                                              │
        │   telemetry  (1 Hz 心跳，NDJSON)        ──►   │
        │   command    (仅用户确认后)              ◄──   │
        │   ack        (v1 必须)                   ──►   │
```

全程离线，无 Wi-Fi、无云。

---

## 4. 物理连接  （先于「聪明固件」）

### 4.1 电气

| 项 | 值 | 说明 |
|----|-----|------|
| 屏侧接口 | **UART0**，HY2.0-4P，丝印 `UART0` | 与 USB-C 的 CH340 **共用**。TX = **IO43**，RX = **IO44** |
| 波特率 | **115200**，8N1 | **已冻结。** Parker 能跑 1200–921600；未经双方同意不改 |
| 电平 | **两边都是 3.3 V TTL** | Parker 已确认 3.3 V。**不需要电平转换**。5 V 直灌 ESP32-S3 会烧 |
| 地 | **必须共地** | 只接 TX/RX 不接 GND 是「JSON 永远到不了」的第一原因 |

### 4.2 供电（Parker 给屏供电）

7 吋屏要 **5 V / 2 A**（USB-C 或 UART0 的 **5 V 输入**脚）。

Parker 将从控制板给屏供 **5 V、1–2 A**（控制板不开 Wi-Fi，电流余量更紧）。**尽量按 2 A 设计。** 1 A 时背光可能掉电复位。

**接线警告：** CrowPanel 的 **BAT** 口是 **3.7–4.2 V 锂电池**，不是 5 V。5 V 只能进 **USB-C** 或 **UART0 的 5 V 输入**。不要把 5 V 接到 BAT。

若背光闪烁或屏在负载下重启，改独立 5 V / 2 A，两板之间只留 GND + TX + RX。

针脚顺序以双方**丝印**为准。TX↔RX 交叉。5 V 不得接到 3.3 V 脚。

### 4.3 调试约束（界面侧必记）

CrowPanel 的 UART0 **与 USB-C 共用**。Parker 线插在 UART0 上时：

- **不能**同时用 USB 串口监视器
- 要给屏烧录或看 USB 日志，先拔掉 Parker 的 UART

对策：界面做**屏上日志区**。Parker 应能在自己板子上独立打日志（第二路 UART、SWD 等）。

联调现场建议备：USB-UART 转接、电平转换模块、独立 5 V/2 A、杜邦/HY2.0 线。

---

## 5. 协议约定（v1）

| 规则 | 细节 |
|------|------|
| 帧格式 | **NDJSON**：一个 JSON 对象 + `\n`（LF）。无长度头、无 STX/ETX |
| 编码 | UTF-8，**无 BOM**，紧凑单行（对象内部不要换行） |
| 长度 | 每行 **&lt; 256 字节** |
| CRLF | Parker **应**只发 LF。界面 **必须**同时接受 `\n` 和 `\r\n` |
| 未知字段 | 忽略，不要判失败 |
| 坏行 | 丢掉该行，继续跑。**解析失败不得复位** |
| 取消 | 操作者点取消，界面**不发任何报文** |

v1 消息类型：

| `type` | 方向 | v1 是否必须 |
|--------|------|-------------|
| `telemetry` | Parker → 屏 | **必须** |
| `command` | 屏 → Parker | **必须** |
| `ack` | Parker → 屏 | **必须** |

---

## 6. Telemetry — Parker → 界面

大约 **每秒一行** telemetry（心跳）。**Parker 已确认。** 水分硬件约 **30 s** 采一次，且**采样间隙必须断电**（探头常电会腐蚀；上电后约 **150 ms** 才稳定）。1 Hz 线上重复上次的 `moisture_raw`。链路上长时间静默，界面会当成断线。

`moisture_alert` 或 `water_level` **跳变时立即补发**一帧。

### 6.1 示例（按这个形状发）

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.0,"setpoint_c":22,"pump":1}
```

### 6.2 字段

| 字段 | 类型 | v1 | 取值 | 含义 |
|------|------|----|------|------|
| `type` | string | 必须 | `"telemetry"` | 类型 |
| `moisture_raw` | int | 必须 | **0–4095** | 电容式水分，**12 位 ADC 原始值**。**不要降采样** |
| `moisture_alert` | int | 必须 | 0 / 1 / 2 | **0** = 工作区内，**1** = 太湿，**2** = 太干。阈值由 **Parker 比较**；界面只展示 |
| `water_level` | int | 必须 | 0 / 1 | 浮球：**0** = 正常，**1** = 低水位 |
| `temperature_c` | number | 必须 | 实测 °C | 当前温度。界面**会**显示在目标温度旁边 |
| `setpoint_c` | number | 必须 | **15–30** | Parker **当前实际持有的**温度设定值。作用和 `pump` 一样：让屏重启后与箱体对齐 |
| `pump` | int | 必须 | **0** 或 **1** | 水泵**实际**状态：0 = 关，1 = 开。屏重启后 Switch 才对得上继电器 |

**`setpoint_c` 为什么必须（v1.3 新增）。** 屏上没有带电池的存储，记不住设定值。没有这个字段时，刚上电的屏显示自己的默认 22 °C，而 Parker 可能正持着 26 °C —— 同一台机器上两个数字，谁也分不清哪个是真的。有了这个字段，屏在收到第一帧 telemetry 时就采用 Parker 的值。

界面遵守的规则：

- **Parker 回读的值优先。** 箱体那边有人改了设定值，屏在 1 s 内跟上。
- 屏自己发的温度命令正在等 ACK 时，忽略回读值，数字不会跳。
- 温度命令收到 `ok:true` 后的 **1 s 内**也忽略回读值，覆盖「先 ACK 后执行」的情况。这 1 s 过后 Parker 重新成为权威。
- 在收到第一个 `setpoint_c` 之前，屏上会用琥珀色标注 **"PANEL DEFAULT - NOT CONFIRMED BY CONTROLLER"**，绝不让人误以为这是 Parker 的设定值。

另有两个**可选、仅联调期**的字段（`test_button`、`test_count`）见 **§6.4**。建议 Parker 先做这一项 —— 不接任何传感器就能验证线通不通。

**不要**再发 `humidity_raw` / `humidity_alert`，旧名已废。

v1 **不要**发电阻式空气湿度。

### 6.3 水分数字（两边显示必须一致）

| ADC `moisture_raw` | 土壤水分 | 含义 |
|--------------------|----------|------|
| 1844 | 45% | 过干线 |
| ≈ 2252 | 55% | Parker 的控制目标 |
| 2457 | 60% | 过湿线 |

界面换算：`percent = round(raw * 100 / 4095)`，钳位 0–100。主显示整数 %；条形图在 45%、60% 刻度，55% 打目标标。

建议 Parker 侧判据（界面不二次判决）：

| 条件 | `moisture_alert` |
|------|------------------|
| raw &lt; 1844 | **2**（过干） |
| 1844 ≤ raw ≤ 2457 | **0** |
| raw &gt; 2457 | **1**（过湿） |

**极性（Parker 已确认）：** **ADC 越大越湿**，越小越干。界面不取反。若某支探头反了，由 Parker 固件取反。

**探头供电：** 电容式水分探头**不要 24 小时常电**（腐蚀）。只在采样时上电（约 150 ms 就绪），然后关掉。上次整数按 1 Hz 重发。不要把采样间隔压得很短 —— 采得越勤探头寿命越短。

界面实现要点：两次 30 s 采样之间**保持上次读数**，不要每秒闪烁；心跳断了才把数字改成 `--`。

### 6.4 仅联调期：实体测试按钮（可选，建议第一个做）

**目的。** 一个动作验证整条链路：**在面包板上按一下按钮 → 控制板 → UART → 屏上亮起来。** 不需要传感器、水泵、继电器。这应该是第一个跑通的东西，因为在它跑通之前，别的问题都没法定位。

在控制板任意空闲 GPIO 上接一个**自复位按钮** —— 面包板上现成的那个绿色按钮就行。内部上拉，另一端接 **GND**，固件里做约 20–30 ms 消抖。**按钮不要接到 CrowPanel 上** —— 让信号走一遍控制板，才是这个测试的全部意义。

然后在 telemetry 里加两个可选字段：

| 字段 | 类型 | 取值 | 含义 |
|------|------|------|------|
| `test_button` | int | 0 / 1 | **按住期间为 1。** 驱动屏上的实时指示灯 |
| `test_count` | int | 单调递增，从 0 开始 | **每次按下沿 +1。** 除重启外不得减小 |

```json
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":0,"test_button":1,"test_count":3}
```

**按钮跳变时必须立刻补发一帧 telemetry**，规则与 `water_level` 相同。正常按一下只有约 100–250 ms，短于 1 Hz 心跳，不补发这一帧的话按键会正好落在两帧之间，看起来就像按钮坏了。

**为什么要计数器而不只是 0/1 状态。** 计数器才是真正的证据：

- 万一按键还是落在两帧之间，也没丢 —— 计数值会在下一个心跳到达，屏那时补闪一次。
- **按 5 次，屏上必须显示 5。** 显示 4 就说明在丢帧，那是一个可查的具体问题，而不是一团迷雾。
- 连按两次显示 **+2**，所以能区分「按了两次」和「长按一次」。

**屏上会看到什么。** 顶部状态栏出现一个 **TEST** 标签，带指示灯和计数。按住期间灯亮；每次计数到的按压会让它亮 800 ms 再灭，所以隔着工作台也能看清短按。每次按压还会往屏下方日志里写一行（`TEST BUTTON +1, count 3`），万一眨眼错过了也有可数的记录。如果控制板重启导致计数回退，屏上会明确写出来，而不是装作没事。

这个标签**只在收到这两个字段时才出现**。停发就消失 —— 两边固件都不需要为了量产删代码，所以按钮那段代码暂时留着就行。

**顺带一个不改协议的技巧：** 面包板上现成的那个电位器可以接到水分 ADC 通道上，临时替代电容式探头。旋钮一转，屏上水分条会从 0 扫到 100%，经过 45 / 55 / 60% 三条刻度，等于在探头还没插进土里之前就把模拟通道和告警判据都验证了。

---

## 7. Command — 界面 → Parker

只有点了「确认」才发。取消 = UART 上什么都没有。

解析 `type == "command"`，再看带了哪些键。一条 command **要么**是泵，**要么**是温度，不会两个一起。

### 7.1 水泵

```json
{"type":"command","pump":1}
```

| `pump` | 含义 | 界面会发吗 |
|--------|------|------------|
| **1** | **开**泵 | 确认后会 |
| **2** | **关**泵 | 确认后会 |
| 0 | 无变化 | **不会** — 若收到当空操作 |

这套 **1 / 2** 是原始需求。它和 telemetry 里的 `pump`（**0 / 1** = 实际状态）**不是同一套编码**，两边都要写清楚。

Parker 驱动继电器之后，后续 telemetry 应带上新的实际 `pump` 0 或 1。

界面：Switch 只在手指抬起（`LV_EVENT_RELEASED`）弹确认框；维护 `pending` / `confirmed`；取消则回弹。未确认前**不得**发 UART。

### 7.2 温度设定

```json
{"type":"command","temperature":{"changed":1,"new_temp":23}}
```

| 字段 | 含义 |
|------|------|
| `changed` | **1** = 采用 `new_temp`。界面不会发 `changed: 0` |
| `new_temp` | 整数 **15–30**，单位 **°C** |

Parker **必须**再次校验 15–30，即使界面已经限幅。串口误码是真的。

闭环仍归 Parker；界面只传操作者的目标。

---

## 8. ACK — Parker → 界面（必须）

收到合法 `command` 后回：

```json
{"type":"ack","ok":true}
```

非法（pump 取值不对、`new_temp` 越界）：

```json
{"type":"ack","ok":false}
```

界面等 **1 秒**。无 ACK → 状态栏「指令未确认」，Switch / 滑块**不落确认态**，允许重试。

**Parker 已确认 v1 做 ACK。** 界面：确认后先发 command，等到 `ok:true` 再固化控件。

---

## 9. 时序

| 事件 | 要求 |
|------|------|
| Telemetry 心跳 | **约 1 Hz**（0.5–2 Hz 可接受） |
| 水分 ADC 采样 | 约 30 s；采样间隙断电；上电约 150 ms 就绪 |
| 告警沿（`moisture_alert` 或 `water_level` 变化） | **立即**补发 telemetry |
| 界面判定断线 | **2 秒**无任何 telemetry → 「通信中断」，水分/温度改 `--` |
| Command | 仅确认时发送；不连发；用户再次确认才会再发 |
| ACK | command 后 **1 秒**内 |

**1 Hz 心跳已确认。** 不要改成「水分采一次才发一帧」。

---

## 10. Parker 固件清单（便于你核对英文版）

- [ ] UART 115200 8N1，TX/RX 交叉，共地
- [ ] **第一个里程碑：** 空闲 GPIO 上接自复位测试按钮，消抖后以 `test_button` + `test_count` 上报，跳变时补发一帧（§6.4）
- [x] 电平 **3.3 V**（已确认，无需转换）
- [ ] NDJSON：单行 JSON + `\n`，忽略未知键，坏行不崩溃
- [x] 约 1 Hz telemetry：重复上次水分整数；探头仅采样时上电（~150 ms）
- [ ] telemetry 含 `type`、`moisture_raw`（0–4095）、`moisture_alert`、`water_level`、**`temperature_c`**、**`setpoint_c` 15–30**、**`pump` 0/1**
- [ ] `moisture_raw` **不**降采样
- [x] 线上极性：越大越湿
- [ ] 上报前 ADC 平均/中值
- [ ] 解析 command：`pump` 1/2，以及 `temperature.new_temp` 15–30
- [ ] 固件再次校验温度范围
- [ ] 未确认的 UI 拖动不会出现在 UART 上
- [x] v1 做 ACK
- [ ] 解析错误不复位

---

## 11. 界面行为（Parker 用来预判 UART）

| 操作者动作 | UART |
|------------|------|
| 拨水泵，再点**取消** | 无 |
| 拨水泵，确认 **开** | `{"type":"command","pump":1}` |
| 确认关 | `{"type":"command","pump":2}` |
| 拖滑块，**取消** | 无 |
| 拖到 23 °C，**确认** | `{"type":"command","temperature":{"changed":1,"new_temp":23}}` |
| 关掉 Low Water 闪烁 | **无**（消警只是显示层；Parker 在水位恢复前继续发 `water_level:1`） |

`water_level` 出现 **0 → 1** 跳变时，即使上次已消警，也要重新全屏闪烁。

消警建议：强制闪烁 ≥ 3 s + 中央大按钮「ACKNOWLEDGE」（防雾培凝露误触）。消警后主界面保留红色「低水位」条，直到 `water_level` 回到 0。

界面**不会**根据水分自己开关泵。水分告警只显示；控制逻辑在 Parker。

---

## 12. 并行开发（不要等对方板子）

### 12.1 界面侧（你现在就可以做）

用 mock telemetry：1 Hz 心跳、水分每 30 s 变一次、可手动切 `water_level` / `moisture_alert` / `pump`。Command 先打到屏上日志或 USB（没接 Parker 时）。

建议自备：

- 假 telemetry 发生器（PC 脚本或固件内编译开关 `MOCK_TELEMETRY`）
- 屏上日志区（UART0 被占用时唯一调试手段）
- 断线：停止 mock 2 s，确认变灰 `--`

### 12.2 Parker 无屏时

他把 telemetry 打到调试串口或 USB-TTL；用串口助手贴 command 看继电器。英文版 **附录 B** 有一份假界面 Python，可给他一起发。

JSON **现在**对齐；硬件可以后到。

---

## 13. 联合联调（两块板都在桌上时）

按顺序做，某步失败就停。

1. **电源与电平** — **UART 3.3 V，无需转换。** 屏的 5 V 由 Parker 提供（尽量 2 A，进 USB-C 或 UART0 的 5 V 输入，**不要进 BAT**）。
2. **先 GND**，再 TX/RX。双方上电。
3. **Parker TX → 屏**：状态栏 **LINK OK**，改 `moisture_raw`（或注入测试值）时百分比会动。
4. **心跳**：拔 TX 约 3 s → 通信中断；插回 → 数值恢复。
5. **确认开泵** → 继电器开，telemetry `pump:1`。
6. **确认关泵** → 继电器关，telemetry `pump:0`。
7. **取消改泵** → 继电器不变，线上无 command。
8. **确认温度 15 和 30** → 接受。用测试 command 注入 14 或 31 → Parker **拒绝**。
9. **`water_level:1`** → 全屏 Low Water。屏上消警 → 闪烁停，telemetry 仍为低水位。先 `0` 再 `1` → 再次闪烁。
10. **`moisture_alert` 1 和 2** → 过高 / 过低（颜色+文字）。
11. 连续跑 ≥ 30 分钟：心跳稳定，UART 不卡死。

**联调现场注意：** 插着 Parker UART 时不要指望 USB 串口监视器；看屏上日志。改屏固件先拔 Parker 线。

---

## 14. 验收（下列全过才算 v1）

| ID | 测试 | 通过标准 |
|----|------|----------|
| A1 | 1 Hz telemetry，合法 JSON | 屏显示 LINK OK |
| A2 | `moisture_raw` 1844 / 2252 / 2457 | 界面约 45% / 55% / 60% |
| A3 | `moisture_alert` 2 → 0 → 1 | 过干 / 正常 / 过湿 |
| A4 | 确认开/关泵 | 继电器一致；telemetry `pump` 0/1 |
| A5 | 取消泵或温度 | 无 command；硬件不变 |
| A6 | 确认温度 23 | 目标 23 °C 生效 |
| A7 | 越界 `new_temp` | Parker 忽略或 NACK |
| A8 | `water_level` 0→1 | 闪烁；消警；再次 1 再闪 |
| A9 | 断开 TX 3 s | 通信中断；恢复后正常 |
| A10 | 先发一行垃圾再发好 JSON | 两边都不死机 |
| A11 | command 后 1 s 内 ACK | 仅 `ok:true` 后 Switch / 设定才落下 |
| A12 | telemetry 带 `temperature_c` | 界面在目标温度旁显示实测 °C |
| A13 | Parker 持 26 °C 时给屏重新上电 | 屏 1 s 内显示 **26**，琥珀色「未确认」提示消失 |
| A14 | 在 Parker 侧把设定值改成 19 °C | 屏上滑块和大字跟到 **19** |
| A15 | **先做这一条。** 慢慢按测试按钮 5 次 | TEST 标签每次都闪，最终读数 **5**；日志有 5 行。计数必须完全对上 |
| A16 | 长按测试按钮 3 s | 指示灯全程亮，计数只 **+1**，不能多 |

---

## 15. Parker 答复（2026-09-17 已冻结）

Parker 原文已抄入**附录 C**（他批注过的那两份文件不再单独保留文件夹）。

| # | 问题 | Parker | v1 决定 |
|---|------|--------|---------|
| 1 | UART 电平 | **3.3 V**。波特率可 1200–921600，115200 可以 | **115200 8N1，3.3 V，无需电平转换** |
| 2 | 水分 30 s 一采，能否 1 Hz 心跳 | **可以。** 上次读数存成整数按 1 Hz 重发。探头**采样间隙必须断电**（腐蚀）；上电约 **150 ms** 就绪。采得越勤寿命越短 | **1 Hz 心跳，断线超时 2 s。** 采样间隔维持约 30 s |
| 3 | telemetry 带实际 `pump` 0/1 | **是** | **必须** |
| 4 | `ack` | **是** | **必须** |
| 5 | 实测 `temperature_c` | **是**（可行性 Ans10） | **必须。** 界面显示实测 °C |
| 6 | 是否越大越湿 | **是**（越大越湿、越小越干）。单支探头不 100% 有把握，固件可取反。0–4096 量程可微调 | 界面**不取反** |
| 7 | 屏怎么供电 | Parker 侧 **5 V、1–2 A**，走「板载电池接口」；控制板不开 Wi-Fi 所以电流更紧 | **5 V 只进 USB-C 或 UART0 的 5 V 输入，不要进锂电池 BAT。** 尽量 **2 A** |
| 8 | 空气湿度上屏 | 有用但非必须。MCU 会自动调节湿度。若 Feng 同意，以后可加湿度**设定**滑块 | **v1 不上屏、不传湿度 JSON。** 可留 v1.1 |

### 15.1 反向提出的一处改动（v1.3）

写界面固件时发现清单里漏掉的一个缺口：telemetry 里有**实测**温度，但没有**设定值**，所以屏重新上电后两台设备可能显示不同的目标温度。请在每帧 telemetry 里加上 **`setpoint_c`**（见 §6.2）。只是一个整数，作用等价于已经谈好的 `pump` 字段之于水泵。

外壳（可行性 Ans5）：CrowPanel 自带亚克力背板；Parker 仍建议做**全包外壳**。这是硬件项，不改协议。

---

## 16. 界面侧自己的实现备忘

| 项 | 做法 |
|----|------|
| 产品名 | 左上角 **AUTO HYDRO**（无 steamer，不用 Aeroponic Life Support） |
| 日期 | **不显示**；不联网、不做校时界面 |
| 标签 | **水分 / MOISTURE**，不要写 HUMIDITY |
| 换算 | `round(raw * 100 / 4095)`，0–4095 钳位 |
| 心跳 vs 水分 | 1 Hz 收包只刷新「链路」；水分数字可 30 s 不变 |
| 断线 | 2 s 无 telemetry → LINK 断开，数值 `--` |
| 水泵 | 抬起才弹窗；取消不发报；**等到 `ack.ok:true` 再固化 Switch** |
| 温度 | 范围锁 15–30；确认后才发；**主界面显示实测 `temperature_c` + 目标滑块**；设定值以 Parker 回读的 `setpoint_c` 为准 |
| 低水位 | 看 **0→1 跳变** 而非「值为 1 就反复弹」；消警是 UI 状态；值回到 0 才清状态条 |
| 空气湿度 | v1 不上屏；闭环归 Parker |
| 兼容 | 联调初期若 Parker 仍发 `humidity_*`，可临时两种都认，定稿只留 `moisture_*` |
| 工具链 | PlatformIO + LVGL 9 + LovyanGFX；PSRAM = OPI；Huge APP 分区 |

---

## 17. 快记：做 / 不做

**做**

- 协议保持 12 位 `moisture_raw`（0–4095）
- 争取 1 Hz 心跳
- 告警由控制板判定
- 温度 15–30 在固件再验一次
- 共地
- 屏由 Parker 供 5 V（进 USB-C / UART0 5V，**不要进 BAT**；尽量 2 A）

**不做**

- 让 Parker 把 ADC 压成 0–100 再上传
- 把 telemetry 的 `pump` 理解成 command 的 0=无变化（telemetry 是实际 0/1）
- 传感器接到 CrowPanel
- 5 V UART 直连屏（两边已是 3.3 V）
- 水分探头 24 小时常电
- 把 5 V 接到 CrowPanel 锂电池 **BAT** 口
- 多行 pretty-print JSON
- 用 USB 串口监视器和 Parker 线抢同一路 UART0

---

## 附录 A — 报文示例

心跳（可重复；水分可变可不变）：

```
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":0}
```

过干：

```
{"type":"telemetry","moisture_raw":1500,"moisture_alert":2,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":1}
```

低水位：

```
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":1,"temperature_c":22.5,"setpoint_c":22,"pump":0}
```

开泵 + ACK + 状态回读：

```
{"type":"command","pump":1}
{"type":"ack","ok":true}
{"type":"telemetry","moisture_raw":2252,"moisture_alert":0,"water_level":0,"temperature_c":22.5,"setpoint_c":22,"pump":1}
```

设定 25 °C：

```
{"type":"command","temperature":{"changed":1,"new_temp":25}}
{"type":"ack","ok":true}
```

假面板 Python 见英文版 **附录 B**（发给 Parker 即可，不必另附中文脚本）。

---

## 附录 C — Parker 原文（2026-09-11 / 2026-09-17）

从他交回的批注稿抄出，拼写保持原样。这些答复已经写进 §15；独立文件夹删除后，本附录就是存档。

### C.1 对接指南清单上的旁注

| # | 问题 | Parker 原话 |
|---|------|-------------|
| 3 | v1 telemetry 是否带实际 `pump` 0/1？ | Yes |
| 4 | v1 是否做 `ack`？ | Yes |
| 6 | `moisture_raw` 是否已经「越大越湿」？ | Im not 100% sure, but I can add code to make it inverted, so Yes |
| 8 | 以后要不要把空气湿度上屏？ | Yes/Later |

第 1、2、5、7 题写在下面这份可行性评估的末尾，没有写在这张表上。

### C.2 可行性评估末尾的逐条答复

Ans1: UART Communications level will happen at 3.3V with the BAUD rate being negotiable (does 115,200 BAUD work for you, the controller can handle 1200 to 921,600 BAUD).

Ans4: The Panel will be powered using its onboard Battery connections Supplying 5V at 1A to 2A (current reduced due to the controller not using Wi-Fi capibilities)

Ans5: The specific model of Crowpanel we plan to use comes with an acrylic back plate to protect the circuitry from accidental short circuits, However I too recommend creating a more robust case that fully encloses the device.

Ans6: The data from the previous scan can be stored as an integer within the code on the microcontroller and sent back out ever 1Hz, but due to the moisture sensors suffering from corrosion when powered constantly, they must be powered down when not in use, and require roughly 150mS to return to a ready state after power is returned. The time between readings can be reduced, but at the cost of quicker component degredation.

Ans7: Shwoing the resistive humidity output can be a useful addition, but is not required, However if Feng is in agreement and willing, we can adjust both of our codes so the Humidity set-point can be adjusted. Or just leave the display of that information out entirely, the microcontroller within the planter will attempt to automatically monitor and adjust the humidity at a constant rate.

Ans8: You have it correct, the higher the reading from the moisture sensor, the more wet it is, and vice versa, the lower the reading the dryer it is. (But the sensors can be can be manually adjusted to give larger steps or chunks of the 0-4096 range)

Ans9: Acknowledgements would make communications easier and less buggy during use, while also allowing for self managed debugging (Smart man for thinking of this Mr.Feng)

Ans10: The measured temperature display can be done, and is also an amazing yet simple little addition (Double points for Mr.Feng)

Parker 在修订记录里写的 v2.2：All Priority0 dependancies have been met, Priority1 dependancies have been met(pending viewership), and Priority2 dependancies have been met but can be cancelled if workload becomes too-much.

---

## 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-09-11 | 首版对接约定：UART + NDJSON v1、水分 ADC、1 Hz 心跳、command/ack、联调与验收 |
| 1.1 | 2026-09-17 | 屏上产品名 **AUTO HYDRO**；状态栏去掉日期，不做校时 |
| 1.2 | 2026-09-17 | 写入 Parker 答复并冻结：3.3 V、115200、1 Hz 心跳、ACK 必须、`pump` + `temperature_c` 必须、越大越湿、Parker 供 5 V 1–2 A（不进 BAT）、水分探头占空比约 150 ms。空气湿度 v1 不上屏 |
| 1.3 | 2026-09-17 | 第 1 版固件实现时发现缺口：telemetry 新增 **`setpoint_c`（必须，15–30）**，让屏重启后与 Parker 的实际设定值对齐；写明回读优先、ACK 后 1 s 宽限、未确认时屏上的琥珀色提示。新增验收项 A13/A14 |
| 1.4 | 2026-09-18 | **联调测试按钮**（§6.4）：可选 `test_button` / `test_count`，跳变必须补帧，以按压计数作为链路无丢帧的证据。屏上显示会自动消失的 TEST 标签并逐次记日志。新增验收项 A15/A16，并列为 Parker 清单第一项 |
| 1.5 | 2026-09-18 | Parker 批注原文抄入**附录 C**，删除 `docs/parker confirmed/`。给 Parker 的联调一页纸：**`docs/LATEST_UPDATE.md`** |
