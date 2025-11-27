# 🚗 STM32 CAN Brake Actuator Driver

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-STM32G431-blue.svg)](https://www.st.com/en/microcontrollers-microprocessors/stm32g4-series.html)
[![CAN](https://img.shields.io/badge/CAN-500kbit%2Fs-green.svg)](https://en.wikipedia.org/wiki/CAN_bus)
[![Tested](https://img.shields.io/badge/Tested-Renode-orange.svg)](https://renode.io/)

> Professional CAN bus driver for brake actuator control on STM32G431KBT6 microcontroller with BTN7971B motor driver

## 📋 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Hardware](#hardware)
- [Software Architecture](#software-architecture)
- [Quick Start](#quick-start)
- [CAN Protocol](#can-protocol)
- [Testing](#testing)
- [Project Structure](#project-structure)
- [Build Instructions](#build-instructions)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

---

## 🎯 Overview

This project implements a **complete CAN bus communication system** for controlling a brake actuator in automotive or industrial applications. The driver follows professional automotive standards and uses a single-threaded RunLoop architecture with efficient ring buffers for CAN message handling.

### Key Highlights

- ✅ **Professional CAN protocol** with extended 29-bit identifiers
- ✅ **Bidirectional heartbeat** monitoring (PC ↔ MCU)
- ✅ **Position feedback** via potentiometer
- ✅ **PWM motor control** with BTN7971B driver
- ✅ **Safety features** (timeout detection, validation)
- ✅ **Fully tested** in Renode emulator with 15+ automated tests

---

## ✨ Features

### Communication
- **CAN Bus**: 500 kbit/s with extended IDs (29-bit)
- **Bidirectional Heartbeat**: 50ms interval
- **Command Protocol**: Push/Release brake commands
- **Telemetry**: 100ms status updates with time estimation
- **Watchdog**: PC communication timeout detection (200ms)

### Control System
- **Motor Driver**: BTN7971B H-Bridge (30A continuous)
- **PWM Control**: 20 kHz, configurable duty cycle
- **Position Feedback**: 12-bit ADC (0-4095)
- **State Machine**: RELEASED → PUSHING → PUSHED → RELEASING
- **Safety**: Position validation, timeout protection

### Architecture
- **Single-threaded**: RunLoop with non-blocking operations
- **Ring Buffers**: Efficient CAN TX/RX queuing (8 messages each)
- **Modular Design**: Separate drivers for CAN, brake, controller
- **HAL-based**: STM32 HAL library integration

---

## 🔧 Hardware

### Required Components

| Component | Part Number | Description |
|-----------|-------------|-------------|
| **Microcontroller** | STM32G431KBT6 | ARM Cortex-M4F @ 170 MHz, 128KB Flash |
| **Motor Driver** | BTN7971B | H-Bridge, 30A, 8-18V |
| **CAN Transceiver** | TJA1050 / MCP2551 | CAN physical layer |
| **Position Sensor** | 10kΩ Potentiometer | Analog feedback |
| **Power Supply** | 12-18V | Motor power |

### Pin Configuration

```
STM32G431KBT6 Pinout:
├─ PA11  → FDCAN1_RX      (CAN receive)
├─ PA12  → FDCAN1_TX      (CAN transmit)
├─ PA8   → TIM1_CH1       (PWM to motor driver IN)
├─ PA9   → GPIO_Output    (Motor driver INH - direction)
├─ PA1   → ADC1_IN2       (Potentiometer position)
└─ PA0   → ADC1_IN1       (Current sense - optional)

BTN7971B Connections:
├─ IN    ← PWM (PA8)
├─ INH   ← Direction (PA9)
├─ OUT1  → Motor +
├─ OUT2  → Motor -
├─ VS    ← 12-18V
└─ GND   → Ground
```

### Schematic

See [gpio.png](_docs/gpio.png) for detailed GPIO configuration.

⚠️ **Safety Warning**: Test PWM signals with oscilloscope before connecting real motor to prevent damage from incorrect timer configuration.

---

## 🏗️ Software Architecture

### Component Diagram

```
┌─────────────────────────────────────────────────────┐
│                   main.c (RunLoop)                  │
│  ┌────────────┐  ┌────────────┐  ┌──────────────┐ │
│  │ Heartbeat  │  │ Telemetry  │  │   Commands   │ │
│  │  (50ms)    │  │  (100ms)   │  │   (async)    │ │
│  └──────┬─────┘  └──────┬─────┘  └──────┬───────┘ │
└─────────┼────────────────┼────────────────┼─────────┘
          │                │                │
┌─────────▼────────────────▼────────────────▼─────────┐
│              controller.c (Business Logic)          │
│  - PC heartbeat monitoring                          │
│  - Command processing                               │
│  - Health status management                         │
└─────────┬────────────────┬────────────────┬─────────┘
          │                │                │
┌─────────▼────────┐ ┌─────▼─────────┐ ┌───▼──────────┐
│   can.c          │ │ left_brake.c  │ │  automate.c  │
│ - Ring buffers   │ │ - State mach. │ │ - Pack/unpack│
│ - FDCAN HAL      │ │ - Motor ctrl  │ │ (cantools)   │
│ - TX/RX queues   │ │ - ADC reading │ │              │
└──────────────────┘ └───────────────┘ └──────────────┘
```

### Module Responsibilities

| Module | File | Responsibility |
|--------|------|----------------|
| **Business Logic** | `controller.c` | Message routing, heartbeat, health monitoring |
| **CAN Driver** | `can.c` | Ring buffers, FDCAN HAL interface |
| **Brake Control** | `left_brake.c` | Motor PWM, ADC reading, state machine |
| **Protocol** | `automate.c/h` | CAN message pack/unpack (auto-generated) |
| **Main Loop** | `main.c` | Initialization, RunLoop, peripheral updates |

---

## 🚀 Quick Start

### Prerequisites

- **Hardware**: STM32G431KBT6 board with CAN transceiver
- **Software**: 
  - STM32CubeIDE or ARM GCC toolchain
  - STM32CubeMX (for configuration)
  - Renode (for emulation testing)

### 1. Clone Repository

```bash
git clone https://github.com/yourusername/stm32-can-brake-driver.git
cd stm32-can-brake-driver
```

### 2. Build Firmware

#### Using STM32CubeIDE
```bash
# Import project
File → Import → Existing Projects
# Select project directory
# Build: Project → Build All
```

#### Using CMake
```bash
mkdir build && cd build
cmake ..
make -j8
```

### 3. Flash Firmware

```bash
# Using STM32CubeProgrammer
STM32_Programmer_CLI -c port=SWD -w build/can_driver_g4.elf -v -rst

# Or using st-flash
st-flash write build/can_driver_g4.bin 0x08000000
```

### 4. Test in Renode (No Hardware Required!)

```bash
cd _emu
renode stm32g431_brake.resc

# In Renode monitor:
(monitor) start
(monitor) push
(monitor) simulate_pushed
```

---

## 📡 CAN Protocol

### Message Overview

| CAN ID | Name | Direction | Period | Purpose |
|--------|------|-----------|--------|---------|
| `0x98FF0D00` | Heart_Beat_MSG | PC ↔ MCU | 50ms | Availability monitoring |
| `0x98FF0D09` | Left_Brake_CMD | PC → MCU | On-demand | Brake commands |
| `0x98FF0D0A` | Left_Brake_MSG | MCU → PC | 100ms | Brake status |

### Message Formats

#### Heart_Beat_MSG (8 bytes)
```c
struct {
    uint8_t  node_id;        // 0x10=PC, 0xF0=MCU
    uint32_t msg_count;      // Incrementing counter
    uint8_t  health;         // 0-5: OFF/ON/INIT/WARNING/FAILURE/CRITICAL
    uint16_t stamp;          // Timestamp (ms)
}
```

#### Left_Brake_CMD (8 bytes)
```c
struct {
    uint8_t  msg_id;         // Command counter
    uint16_t stamp;          // PC timestamp (ms)
    uint8_t  brake_state;    // 0=RELEASE, 1=PUSH
}
```

#### Left_Brake_MSG (8 bytes)
```c
struct {
    uint8_t  msg_id;                   // Telemetry counter
    uint16_t stamp;                    // MCU timestamp (ms)
    uint8_t  brake_releasing : 1;      // In release operation
    uint8_t  brake_released  : 1;      // Released position
    uint8_t  brake_pushing   : 1;      // In push operation
    uint8_t  brake_pushed    : 1;      // Pushed position
    uint16_t time_to_end_operation;    // Estimated time (ms)
}
```

### Example: Send Push Command

```python
import can

bus = can.interface.Bus(channel='can0', bustype='socketcan')

# Pack Left_Brake_CMD: PUSH
data = [
    0x01,        # msg_id
    0xE8, 0x03,  # stamp = 1000ms
    0x00,        # padding
    0x01,        # brake_state = PUSH
    0x00, 0x00, 0x00  # padding
]

msg = can.Message(arbitration_id=0x98FF0D09, data=data, is_extended_id=True)
bus.send(msg)
```

---

## 🧪 Testing

### Automated Tests (Renode)

15 comprehensive tests covering all functionality:

```bash
# Run all tests
make test

# Run specific category
make test-heartbeat    # Heartbeat tests
make test-brake        # Brake operation tests
make test-safety       # Safety & validation tests

# View results
make report
```

**Test Coverage:**
- ✅ Heartbeat transmission/reception
- ✅ Push/Release command execution
- ✅ Position feedback validation
- ✅ Timeout detection (200ms)
- ✅ State machine transitions
- ✅ Time estimation accuracy
- ✅ Error handling

### Manual Testing

```bash
# Interactive emulation
renode stm32g431_brake_simple.resc

# Commands:
(monitor) start
(monitor) push                  # Send PUSH command
(monitor) simulate_pushed       # Set position = pushed
(monitor) release               # Send RELEASE command
(monitor) simulate_released     # Set position = released
```

### Hardware Testing

```bash
# Setup CAN interface (Linux)
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up

# Monitor CAN traffic
candump can0

# Send commands
cansend can0 98FF0D09#0100000001
```

---

## 📂 Project Structure

```
can_driver_g4/
├── 📄 README.md                    # This file
├── 📄 can_driver_g4.ioc            # STM32CubeMX configuration
├── 📄 CMakeLists.txt               # CMake build configuration
├── 📄 Makefile                     # Build automation
│
├── 📁 _docs/                s       # Documentation
specification (UA)
│   ├── gpio.png                    # GPIO configuration diagram
│   ├── PROTOCOL.md        # Protocol documentation
│   └── readme.pdf                  # Guide in Ukrainian
│
├── 📁 _emu/                        # Renode emulation
│   ├── stm32g431.repl             # Platform description
│   ├── stm32g431_brake.resc       # Startup script
│   ├── brake_tests.robot          # Robot Framework tests
│   ├── python_test_scenario.py    # Python test scenarios
│   ├── RENODE_TESTING.md          # Testing guide
│   └── EXAMPLES.md                # Usage examples
│
├── 📁 Core/
│   ├── 📁 Inc/                     # Headers
│   │   ├── automate.h             # Protocol (auto-generated)
│   │   ├── can.h                  # CAN driver interface
│   │   ├── controller.h           # Business logic interface
│   │   ├── left_brake.h           # Brake control interface
│   │   ├── common.h               # Common definitions
│   │   ├── main.h                 # Main declarations
│   │   └── stm32g4xx_*.h          # HAL configuration
│   │
│   └── 📁 Src/                     # Source files
│       ├── automate.c             # Protocol (auto-generated by cantools)
│       ├── can.c                  # CAN driver with ring buffers
│       ├── controller.c           # Business logic & message routing
│       ├── left_brake.c           # Brake motor & ADC control
│       ├── main.c                 # Initialization & RunLoop
│       ├── stm32g4xx_hal_msp.c    # HAL MSP callbacks
│       └── stm32g4xx_it.c         # Interrupt handlers
│
└── 📁 Drivers/                     # STM32 HAL driver (not included)
    ├── STM32G4xx_HAL_Driver/
    └── CMSIS/
```

### Key Files Description

| File | Purpose | Auto-generated? |
|------|---------|-----------------|
| `automate.c/h` | CAN message pack/unpack | ✅ Yes (cantools) |
| `can.c/h` | CAN driver with ring buffers | ❌ Manual |
| `controller.c/h` | Business logic & routing | ❌ Manual |
| `left_brake.c/h` | Motor control & ADC | ❌ Manual |
| `main.c` | RunLoop & initialization | ⚠️ Partial (CubeMX) |

---

## 🛠️ Build Instructions

### Method 1: STM32CubeIDE (Recommended)

1. **Open Project**
   ```
   File → Open Projects from File System
   → Select project directory
   ```

2. **Configure (if needed)**
   ```
   Double-click: can_driver_g4.ioc
   → Modify peripherals
   → Generate Code
   ```

3. **Build**
   ```
   Project → Build All (Ctrl+B)
   ```

4. **Flash & Debug**
   ```
   Run → Debug (F11)
   ```

### Method 2: CMake + ARM GCC

```bash
# Setup toolchain
export ARM_TOOLCHAIN_PATH=/path/to/gcc-arm-none-eabi

# Configure
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
make -j8

# Output: build/can_driver_g4.elf
```

### Method 3: Makefile

```bash
# Build
make

# Clean
make clean

# Flash (requires st-flash)
make flash

# Size info
make size
```

---

## 📚 Documentation

### Included Documents

- **[Technical Specification](_docs/ТЗ на драйвер.md)** - Full requirements (Ukrainian)
- **[Protocol Changes](_docs/PROTOCOL.md)** - CAN protocol documentation

### External Resources

- [STM32G4 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0440-stm32g4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [BTN7971B Datasheet](https://www.infineon.com/dgdl/Infineon-BTN7971B-DataSheet-v02_00-EN.pdf)
- [FDCAN Guide](https://www.st.com/resource/en/application_note/an5348-fdcan-protocol-used-in-the-stm32-mcus-stmicroelectronics.pdf)
- [Cantools Documentation](https://cantools.readthedocs.io/)

---

## 🤝 Contributing

Contributions are welcome! Please follow these guidelines:


## 📊 Performance Metrics

| Metric | Value |
|--------|-------|
| **RAM Usage** | ~2KB (with 16-message buffers) |
| **Flash Usage** | ~28KB |
| **CPU Load** | ~5% @ 170MHz |
| **CAN Latency** | <5ms (command → action) |
| **Position Update** | 10ms (100 Hz) |
| **Heartbeat Period** | 50ms (20 Hz) |
| **Telemetry Period** | 100ms (10 Hz) |

---

## 🔐 Safety & Certification

⚠️ **Important Safety Notice**

This is a **demonstration project** for educational purposes. Before using in safety-critical applications:

**No warranty provided. Use at your own risk.**

---

## 📜 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

```
MIT License

Copyright (c) 2025 [Anatoly Mazkun]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
```

---

## 👨‍💻 Author

**[Your Name]**
- GitHub: [@AMazkun](https://github.com/AMazkun)
- LinkedIn: [Anatoly Mazkun](https://www.linkedin.com/in/anatoly-mazkun/)

---

<div align="center">

**Made with ❤️ for the Embedded Systems Community**

[⬆ Back to Top](#-stm32-can-brake-actuator-driver)

</div>
