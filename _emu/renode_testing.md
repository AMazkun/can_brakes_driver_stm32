# STM32G431 Brake Controller - Renode Emulation & Testing

## 📋 Огляд

Цей набір файлів дозволяє емулювати та тестувати STM32G431KBT6 Brake Controller в Renode без реального залізо.

## 📁 Файли проєкту

```
renode/
├── stm32g431.repl              # Опис платформи (periferals)
├── stm32g431_brake.resc        # Скрипт запуску Renode
├── brake_tests.robot           # Robot Framework тести (автоматизовані)
├── python_test_scenario.py     # Python тестові сценарії
└── firmware.elf                # Скомпільована прошивка
```

## 🚀 Встановлення Renode

### Linux (Ubuntu/Debian):
```bash
# Додати репозиторій
wget https://github.com/renode/renode/releases/download/v1.14.0/renode_1.14.0_amd64.deb
sudo dpkg -i renode_1.14.0_amd64.deb

# Або через portable версію
wget https://github.com/renode/renode/releases/download/v1.14.0/renode-1.14.0.linux-portable.tar.gz
tar -xzf renode-1.14.0.linux-portable.tar.gz
```

### macOS:
```bash
brew install --cask renode
```

### Windows:
Завантажити з: https://github.com/renode/renode/releases

## 🎯 Швидкий старт

### 1. Інтерактивна емуляція

```bash
# Запуск Renode
renode stm32g431_brake.resc

# В Renode monitor:
start                    # Запустити емуляцію
pause                    # Пауза
```

### 2. Тестування вручну

```bash
# Запустити Renode
renode stm32g431_brake.resc

# Використати команди:
(monitor) start
(monitor) push                  # Відправити команду PUSH
(monitor) simulate_pushed       # Встановити позицію "натиснуто"
(monitor) release               # Відправити команду RELEASE
(monitor) simulate_released     # Встановити позицію "відпущено"
(monitor) stop_pc               # Зупинити PC heartbeat (тест timeout)
(monitor) start_pc              # Відновити PC heartbeat
```

### 3. Автоматизовані тести

```bash
# Robot Framework тести
renode-test brake_tests.robot

# Запуск конкретного тесту
renode-test --test "Test 004: Push Command Execution" brake_tests.robot

# Запуск з тегами
renode-test --include heartbeat brake_tests.robot
```

## 📊 Доступні команди в Renode Monitor

### Керування емуляцією
```
start                           # Запустити
pause                           # Зупинити
quit                            # Вийти
reset                           # Reset MCU
```

### CAN команди (PC симулятор)
```
push                            # Відправити PUSH команду
release                         # Відправити RELEASE команду
stop_pc                         # Зупинити PC heartbeat
start_pc                        # Відновити PC heartbeat
```

### Симуляція позиції
```
set_position VALUE              # Встановити ADC (0-4095)
simulate_released               # ADC = 200 (відпущено)
simulate_pushed                 # ADC = 3800 (натиснуто)
simulate_midway                 # ADC = 2000 (посередині)
```

### Діагностика
```
sysbus.fdcan1                   # Статус FDCAN
sysbus.adc1                     # Статус ADC
sysbus.tim1                     # Статус Timer
sysbus LogPeripheralAccess sysbus.fdcan1  # Логування CAN
```

## 🧪 Тестові сценарії

### Сценарій 1: Перевірка Heartbeat

```bash
(monitor) start
(monitor) sleep 1

# PC відправляє heartbeat автоматично кожні 50ms
# MCU повинен відповідати своїм heartbeat (Node_id=0xF0)
# Спостерігайте в CAN analyzer
```

**Очікуваний результат:**
- MCU Heartbeat: CAN ID `0x98FF0D00`, Data: `[F0 XX XX XX XX YY ZZ ZZ]`
  - Byte 0: `0xF0` (MCU Node_id)
  - Bytes 1-4: MSG_Count (інкремент)
  - Byte 5: Health (1=ON після 1 сек)
  - Bytes 6-7: Timestamp

### Сценарій 2: Push операція

```bash
(monitor) start
(monitor) simulate_released      # ADC = 200
(monitor) sleep 1
(monitor) push                   # Відправити PUSH команду

# Спостерігайте телеметрію
# MCU Left_Brake_MSG: CAN ID 0x98FF0D0A
# Byte 3 має містити brake_pushing=1 (біт 2)

(monitor) sleep 0.5
(monitor) simulate_pushed        # ADC = 3800

# Тепер byte 3 має містити brake_pushed=1 (біт 3)
```

**Очікуваний результат:**
- Початок: `brake_released=1` (біт 1)
- Після PUSH: `brake_pushing=1` (біт 2)
- Після досягнення позиції: `brake_pushed=1` (біт 3)
- `time_to_end_operation` зменшується

### Сценарій 3: Timeout PC

```bash
(monitor) start
(monitor) sleep 1

# PC heartbeat працює (автоматично)
# MCU Health = ON (1)

(monitor) stop_pc               # Зупинити PC heartbeat

# Чекаємо > 200ms
(monitor) sleep 0.3

# Перевіряємо MCU heartbeat
# Health повинен бути WARNING (3)
```

**Очікуваний результат:**
- Після 200ms без PC heartbeat: MCU Health = WARNING (3)

### Сценарій 4: Повний цикл

```bash
(monitor) start
(monitor) simulate_released
(monitor) sleep 1

# 1. Push
(monitor) push
(monitor) sleep 0.2
(monitor) simulate_pushed

# 2. Release
(monitor) release
(monitor) sleep 0.2
(monitor) simulate_released

# Перевірити всі переходи станів
```

## 🔍 Моніторинг CAN повідомлень

### В Renode Analyzer

Renode автоматично відкриває CAN analyzer. Ви побачите:

**MCU → PC:**
- `0x98FF0D00` - Heartbeat кожні 50ms (Node_id=0xF0)
- `0x98FF0D0A` - Telemetry кожні 100ms

**PC → MCU:**
- `0x98FF0D00` - Heartbeat кожні 50ms (Node_id=0x10)
- `0x98FF0D09` - Commands (при виклику `push`/`release`)

### Логування в консоль

```bash
# Увімкнути детальне логування
(monitor) logLevel 3 sysbus.fdcan1

# Побачите:
# [INFO] MCU HB: count=123, health=1, stamp=5000
# [INFO] Brake Status: PUSHING, time=1500ms
```

## 📈 Robot Framework тести

### Структура тестів

15 автоматизованих тестів покривають:

1. **Basic Functionality (001-003)**
   - Heartbeat transmission
   - PC heartbeat reception
   - Initial state

2. **Command Execution (004-006)**
   - Push command
   - Release command
   - Full cycle

3. **Safety & Validation (007-008, 011)**
   - Timeout detection
   - Counter increments
   - Invalid command rejection

4. **Timing & Protocol (009-010, 012)**
   - Telemetry period
   - Idempotency
   - Time estimation

5. **Advanced (013-015)**
   - ADC position feedback
   - Initialization sequence
   - Bidirectional communication

### Запуск тестів

```bash
# Всі тести
renode-test brake_tests.robot

# З виведенням логів
renode-test --show-log brake_tests.robot

# З HTML звітом
renode-test --output-directory results brake_tests.robot

# Тільки тести з тегом "heartbeat"
renode-test --include heartbeat brake_tests.robot

# Виключити повільні тести
renode-test --exclude slow brake_tests.robot
```

### Звіти тестів

Після запуску генеруються:
- `log.html` - Детальний лог
- `report.html` - Звіт з результатами
- `output.xml` - XML для CI/CD

## 🐍 Python сценарії

```bash
# Запуск Python тестів
python3 python_test_scenario.py

# Виведе:
# - Опис кожного сценарію
# - Очікувані CAN повідомлення
# - Renode команди для виконання
```

## 🔧 Налаштування емуляції

### Зміна параметрів ADC

```python
# В stm32g431.repl
potentiometer: Analog.LinearScalingAnalogInput @ adc1 2
    scalingFactor: 4095        # Max ADC value
    valueRange: 4095
```

### Зміна швидкості CAN

```python
# В stm32g431.repl
canBus: CAN.CANBUS @ fdcan1
    canSpeed: 500000           # 500 kbit/s
```

### Додавання периферії

```python
# В stm32g431.repl
newPeripheral: Type @ sysbus 0xAddress
    parameter: value
```

## 🐛 Troubleshooting

### Проблема: "Cannot find platform file"
```bash
# Переконайтеся що .repl файл в тій же директорії
ls stm32g431.repl
```

### Проблема: "ELF file not found"
```bash
# Скомпілюйте прошивку або вкажіть правильний шлях
$bin=@/full/path/to/firmware.elf
```

### Проблема: "CAN messages not visible"
```bash
# Перевірте що analyzer відкритий
(monitor) showAnalyzer sysbus.fdcan1

# Або дивіться логи
(monitor) logLevel 3 sysbus.fdcan1
```

### Проблема: "Heartbeat timeout не працює"
```bash
# Переконайтеся що stop_pc викликано
(monitor) stop_pc
(monitor) sleep 0.3

# Перевірте MCU Health в наступному heartbeat
```

## 📊 CI/CD Integration

### GitHub Actions

```yaml
name: Renode Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Install Renode
        run: |
          wget https://github.com/renode/renode/releases/download/v1.14.0/renode_1.14.0_amd64.deb
          sudo dpkg -i renode_1.14.0_amd64.deb
      
      - name: Build firmware
        run: make
      
      - name: Run tests
        run: renode-test brake_tests.robot
      
      - name: Upload results
        uses: actions/upload-artifact@v2
        with:
          name: test-results
          path: results/
```

## 📚 Додаткові ресурси

- [Renode Documentation](https://renode.readthedocs.io/)
- [Robot Framework Guide](https://robotframework.org/robotframework/latest/RobotFrameworkUserGuide.html)
- [CAN Protocol Specification](https://www.can-cia.org/)

## ✅ Чеклист тестування

Перед релізом прошивки:

- [ ] Всі Robot Framework тести проходять
- [ ] Heartbeat працює в обидва напрямки
- [ ] Timeout detection активується через 200ms
- [ ] Push/Release команди виконуються коректно
- [ ] ADC position feedback працює
- [ ] Time_to_end_operation оновлюється
- [ ] Невалідні команди ігноруються
- [ ] Повний цикл push-release працює
- [ ] MCU Health transitions правильні
- [ ] CAN analyzer показує всі повідомлення

---

**Версія**: 1.0  
**Дата**: 2025-01  
**Автор**: Brake Controller Team