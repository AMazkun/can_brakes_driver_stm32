# Renode Testing - Практичні приклади

## 📋 Зміст

1. [Базовий запуск](#базовий-запуск)
2. [Тестування Heartbeat](#тестування-heartbeat)
3. [Тестування команд гальма](#тестування-команд-гальма)
4. [Тестування timeout](#тестування-timeout)
5. [Аналіз CAN трафіку](#аналіз-can-трафіку)
6. [Debugging](#debugging)

---

## Базовий запуск

### Запуск інтерактивної емуляції

```bash
$ renode stm32g431_brake.resc
```

**Вивід:**
```
Renode, version 1.14.0.12345
(monitor) mach create "STM32G431_Brake"
(monitor) machine LoadPlatformDescription @stm32g431.repl
(monitor) sysbus LoadELF @firmware.elf
(monitor) 
======================================================================
  STM32G431KBT6 Brake Controller Emulation
======================================================================

Available commands:
  start                 - Start emulation
  push                  - Send PUSH command to brake
  release               - Send RELEASE command to brake
  ...

(monitor)
```

### Запуск емуляції

```bash
(monitor) start
```

**Вивід в консолі:**
```
Starting emulation...
[INFO] MCU HB: count=0, health=2, stamp=100
[INFO] MCU HB: count=1, health=2, stamp=150
[INFO] Brake Status: RELEASED, time=0ms
[INFO] MCU HB: count=2, health=1, stamp=200
[INFO] Brake Status: RELEASED, time=0ms
```

**Пояснення:**
- `health=2` (INIT) - перша секунда після старту
- `health=1` (ON) - після 1 секунди роботи
- Heartbeat кожні 50ms
- Телеметрія кожні 100ms

---

## Тестування Heartbeat

### Сценарій 1: MCU відправляє Heartbeat

```bash
(monitor) start
(monitor) sleep 2
```

**CAN Analyzer покаже:**
```
Time    | CAN ID      | Data                           | Description
--------|-------------|--------------------------------|------------------
0.050s  | 0x98FF0D00  | F0 00 00 00 00 02 32 00       | MCU HB #0, INIT
0.100s  | 0x98FF0D00  | F0 01 00 00 00 02 64 00       | MCU HB #1, INIT
0.150s  | 0x98FF0D00  | F0 02 00 00 00 02 96 00       | MCU HB #2, INIT
...
1.050s  | 0x98FF0D00  | F0 15 00 00 00 01 14 04       | MCU HB #21, ON
```

**Розшифровка:**
```
[F0] - Node_id = 0xF0 (MCU)
[00 00 00 00] - MSG_Count = 0 (little-endian)
[02] - Health = 2 (INIT)
[32 00] - Stamp = 50ms (little-endian)
```

### Сценарій 2: PC відправляє Heartbeat

```bash
(monitor) start
(monitor) sleep 1
# PC heartbeat відправляється автоматично кожні 50ms
```

**Внутрішній лог (якщо увімкнути debug):**
```
[DEBUG] PC sends: 0x98FF0D00 [10 00 00 00 00 01 32 00]
[DEBUG] MCU receives PC heartbeat, updating watchdog
[DEBUG] PC sends: 0x98FF0D00 [10 01 00 00 00 01 64 00]
[DEBUG] MCU receives PC heartbeat, updating watchdog
```

---

## Тестування команд гальма

### Сценарій 1: Push команда

```bash
(monitor) start
(monitor) simulate_released     # Встановити позицію "відпущено"
(monitor) sleep 1
(monitor) push                  # Відправити PUSH команду
```

**Вивід:**
```
Sent PUSH command to brake
[INFO] Brake Status: PUSHING, time=1900ms
[INFO] Brake Status: PUSHING, time=1800ms
[INFO] Brake Status: PUSHING, time=1700ms
...
```

**CAN повідомлення:**

1. **Команда від PC:**
```
CAN ID: 0x98FF0D09
Data: [01 E8 03 00 01 00 00 00]
       │   │    │  │
       │   │    │  └─ brake_state=1 (PUSH)
       │   │    └──── (padding)
       │   └───────── stamp=1000ms
       └───────────── msg_id=1
```

2. **Телеметрія від MCU:**
```
CAN ID: 0x98FF0D0A (кожні 100ms)
Data: [05 10 27 04 00 6C 07 00]
       │   │    │     │
       │   │    │     └─ time_to_end=1900ms
       │   │    └─────── flags: 0x04 (bit 2 = pushing)
       │   └──────────── stamp=10000ms
       └──────────────── msg_id=5
```

### Симуляція руху актуатора

```bash
(monitor) push
(monitor) sleep 0.5
(monitor) set_position 2000     # Встановити середню позицію
(monitor) sleep 0.5
(monitor) simulate_pushed       # Досягнуто цільової позиції
```

**Вивід:**
```
[INFO] Brake Status: PUSHING, time=1500ms
Set potentiometer to: 2000
[INFO] Brake Status: PUSHING, time=1000ms
Simulated: Brake PUSHED (ADC=3800)
[INFO] Brake Status: PUSHED, time=0ms
```

**Прапорці стану змінюються:**
```
Initial:  [released=1]           (0x02)
After cmd: [pushing=1]            (0x04)
Complete:  [pushed=1]             (0x08)
```

### Сценарій 2: Release команда

```bash
(monitor) simulate_pushed
(monitor) sleep 1
(monitor) release
(monitor) sleep 0.5
(monitor) simulate_released
```

**Вивід:**
```
Sent RELEASE command to brake
[INFO] Brake Status: RELEASING, time=1900ms
[INFO] Brake Status: RELEASING, time=1400ms
Simulated: Brake RELEASED (ADC=200)
[INFO] Brake Status: RELEASED, time=0ms
```

### Повний цикл

```bash
(monitor) start
(monitor) simulate_released
(monitor) sleep 1

# Push cycle
(monitor) push
(monitor) sleep 0.2
(monitor) simulate_pushed
(monitor) sleep 1

# Release cycle
(monitor) release
(monitor) sleep 0.2
(monitor) simulate_released
(monitor) sleep 1

(monitor) pause
```

**Timeline:**
```
t=0.0s:  State = RELEASED
t=1.0s:  PC sends PUSH
t=1.1s:  State = PUSHING
t=1.3s:  ADC → 3800 (pushed)
t=1.4s:  State = PUSHED
t=2.4s:  PC sends RELEASE
t=2.5s:  State = RELEASING
t=2.7s:  ADC → 200 (released)
t=2.8s:  State = RELEASED
```

---

## Тестування Timeout

### Сценарій: PC heartbeat timeout

```bash
(monitor) start
(monitor) sleep 1
# PC heartbeat працює автоматично, MCU health = ON

(monitor) stop_pc               # Зупинити PC heartbeat
Sent command: PC heartbeat stopped (simulating timeout)

(monitor) sleep 0.3             # Чекаємо > 200ms
```

**Вивід:**
```
[INFO] MCU HB: count=20, health=1, stamp=1000    # ON
[INFO] MCU HB: count=21, health=1, stamp=1050
[INFO] MCU HB: count=22, health=1, stamp=1100
# <-- PC heartbeat зупинено
[INFO] MCU HB: count=23, health=1, stamp=1150
[INFO] MCU HB: count=24, health=3, stamp=1200    # WARNING!
[INFO] MCU HB: count=25, health=3, stamp=1250
```

**Пояснення:**
- `health=1` (ON) - нормальна робота
- Після 200ms без PC heartbeat → `health=3` (WARNING)

### Відновлення зв'язку

```bash
(monitor) start_pc              # Відновити PC heartbeat
PC heartbeat resumed

(monitor) sleep 0.2
```

**Вивід:**
```
[INFO] MCU HB: count=30, health=3, stamp=1500    # Still WARNING
# <-- PC heartbeat відновлено
[INFO] MCU HB: count=31, health=1, stamp=1550    # Back to ON!
```

---

## Аналіз CAN трафіку

### Відкриття CAN Analyzer

```bash
(monitor) showAnalyzer sysbus.fdcan1
```

**Analyzer Window покаже таблицю:**
```
┌─────────┬──────────────┬────────────────────────────────────┬─────────────┐
│  Time   │   CAN ID     │              Data                  │ Description │
├─────────┼──────────────┼────────────────────────────────────┼─────────────┤
│ 0.050s  │ 0x98FF0D00   │ F0 00 00 00 00 02 32 00           │ MCU HB      │
│ 0.050s  │ 0x98FF0D00   │ 10 00 00 00 00 01 32 00           │ PC HB       │
│ 0.100s  │ 0x98FF0D0A   │ 00 64 00 02 00 00 00 00           │ Brake Status│
│ 0.100s  │ 0x98FF0D00   │ F0 01 00 00 00 02 64 00           │ MCU HB      │
│ 0.100s  │ 0x98FF0D00   │ 10 01 00 00 00 01 64 00           │ PC HB       │
│ 0.150s  │ 0x98FF0D00   │ F0 02 00 00 00 02 96 00           │ MCU HB      │
│ ...     │ ...          │ ...                                │ ...         │
└─────────┴──────────────┴────────────────────────────────────┴─────────────┘
```

### Фільтрація повідомлень

```bash
# Логування тільки telemetry
(monitor) logLevel 0
(monitor) logLevel 3 sysbus.fdcan1
(monitor) python
import struct

def filter_telemetry(msg):
    if msg.CanId == 0x98FF0D0A:
        data = msg.Data
        flags = data[3]
        time_rem = struct.unpack('<H', data[5:7])[0]
        
        states = []
        if flags & 0x01: states.append("RELEASING")
        if flags & 0x02: states.append("RELEASED")
        if flags & 0x04: states.append("PUSHING")
        if flags & 0x08: states.append("PUSHED")
        
        print(f"Brake: {' '.join(states)}, time={time_rem}ms")

self.Machine.GetNode("fdcan1").MessageSent += filter_telemetry
```

---

## Debugging

### Увімкнення детального логування

```bash
(monitor) logLevel 3
```

**Вивід:**
```
[DEBUG] FDCAN1: TX message 0x98FF0D00, DLC=8
[DEBUG] FDCAN1: Data: F0 00 00 00 00 02 32 00
[DEBUG] ADC1: Channel 2 read, value=200
[DEBUG] TIM1: PWM duty cycle set to 0%
[DEBUG] GPIOA: Pin 9 set to LOW
```

### Крок-за-кроком виконання

```bash
(monitor) start
(monitor) sleep 0.05            # Один heartbeat
(monitor) pause
(monitor) singleStep 1000       # 1000 інструкцій
(monitor) singleStep 1000
```

### Інспекція пам'яті

```bash
# Перевірити стан змінних
(monitor) sysbus ReadDoubleWord 0x20000000    # app_state.state
(monitor) sysbus ReadWord 0x20000004          # current_position
```

### Trace CAN messages

```bash
(monitor) python
def trace_all(msg):
    print(f"CAN TX: ID=0x{msg.CanId:08X}, Data={msg.Data.hex().upper()}")

self.Machine.GetNode("fdcan1").MessageSent += trace_all
```

**Вивід:**
```
CAN TX: ID=0x98FF0D00, Data=F0000000000232 00
CAN TX: ID=0x98FF0D0A, Data=0064000200000000
CAN TX: ID=0x98FF0D00, Data=F0010000000264 00
```

---

## Автоматизовані тести

### Запуск всіх тестів

```bash
$ make test
```

**Вивід:**
```
Running Robot Framework tests...
==============================================================================
Brake Tests                                                                   
==============================================================================
Test 001: MCU Sends Heartbeat After Boot                              | PASS |
Test 002: MCU Responds To PC Heartbeat                                | PASS |
Test 003: Initial Brake State Is Released                             | PASS |
Test 004: Push Command Execution                                      | PASS |
Test 005: Release Command Execution                                   | PASS |
Test 006: Full Cycle Push And Release                                 | PASS |
Test 007: PC Heartbeat Timeout Detection                              | PASS |
Test 008: Heartbeat Message Counter Increments                        | PASS |
Test 009: Telemetry Period Is 100ms                                   | PASS |
Test 010: Duplicate Commands Are Idempotent                           | PASS |
Test 011: Invalid Brake State Is Rejected                             | PASS |
Test 012: Time To End Operation Updates                               | PASS |
Test 013: ADC Position Affects State Transitions                      | PASS |
Test 014: System Initialization Sequence                              | PASS |
Test 015: Bidirectional Heartbeat Communication                       | PASS |
==============================================================================
Brake Tests                                                           | PASS |
15 tests, 15 passed, 0 failed
==============================================================================

✓ Tests completed
Results in: test_results/
```

### Запуск конкретного тесту

```bash
$ make test-004
```

**Вивід:**
```
Test 004: Push Command Execution
==============================================================================
Brake Tests.Test 004: Push Command Execution                          | PASS |
==============================================================================
Brake Tests                                                           | PASS |
1 test, 1 passed, 0 failed
```

### Перегляд звіту

```bash
$ make report
```

Відкриється HTML звіт:
```
Test Execution Report
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Suite: Brake Tests
Status: PASS
Duration: 45.2 seconds
Tests: 15 passed, 0 failed

Details:
  ✓ Test 001: MCU Sends Heartbeat (2.1s)
  ✓ Test 002: MCU Responds To PC (1.8s)
  ...
```

---

## Корисні команди

### Швидкі команди для тестування

```bash
# Запустити і одразу push
(monitor) start; sleep 1; push; sleep 2; pause

# Цикл push-release
(monitor) start; sleep 1; push; sleep 1; release; sleep 1; pause

# Тест timeout
(monitor) start; sleep 1; stop_pc; sleep 0.3; pause

# Ручне керування ADC
(monitor) start
(monitor) set_position 200    # Released
(monitor) set_position 1000   # Quarter
(monitor) set_position 2000   # Half
(monitor) set_position 3000   # Three quarters
(monitor) set_position 3800   # Pushed
```

### Макроси

Можна створити файл `custom_commands.resc`:
```python
macro cycle
"""
    push
    sleep 1
    simulate_pushed
    sleep 0.5
    release
    sleep 1
    simulate_released
"""

macro test_timeout
"""
    stop_pc
    sleep 0.3
    start_pc
"""
```

---

## Tipps & Tricks

### 1. Прискорення емуляції
```bash
(monitor) emulation SetPerformance 0.01  # 1% швидкість (повільніше)
(monitor) emulation SetPerformance 10    # 10x швидкість (швидше)
```

### 2. Запис CAN трафіку
```bash
(monitor) logFile @can_traffic.log
(monitor) logLevel 3 sysbus.fdcan1
(monitor) start
# ... тестування ...
(monitor) pause
```

### 3. Скриншоти Analyzer
```bash
# В GUI analyzer натиснути Ctrl+S або Export
```

### 4. Автоматичний restart при помилці
```bash
macro test_robust
"""
    start
    sleep 10
    pause
    reset
    start
    sleep 10
    pause
"""
```

---

**Готово!** Тепер у вас є повний набір інструментів для тестування в Renode! 🚀