# Бізнес-логіка відповідно до специфікації протоколу


## 🔄 1. Heart_Beat_MSG - Двостороннє повідомлення

### Специфікація:
```
CAN ID: 0x98FF0D00
Напрямок: PC ↔ MCU (двостороннє)
Період: 50 мс
```

### Реалізація:

#### MCU → PC (відправка):
```c
// MCU відправляє кожні 50 мс
hb_msg.node_id = 0xF0;           // MCU ідентифікатор
hb_msg.msg_count = counter++;    // Інкрементний лічильник
hb_msg.health = mcu_health;      // Стан MCU (0-5)
hb_msg.stamp = HAL_GetTick();    // MCU timestamp
```

#### PC → MCU (прийом):
```c
// MCU очікує від PC кожні 50 мс
if (heartbeat.node_id == 0x10) {  // Перевірка PC ID
    last_pc_heartbeat_tick = HAL_GetTick();
    pc_heartbeat_msg_count = heartbeat.msg_count;
    // Моніторинг PC health
}
// Ігнорувати повідомлення з іншими Node_id
```

**Константи:**
```c
#define NODE_ID_MCU  0xF0    // MCU identifier (було: 1)
#define NODE_ID_PC   0x10    // PC identifier (додано)
```

**Фільтрація повідомлень:**
```c
// Фільтрація по Node_id
if (heartbeat.node_id == NODE_ID_PC) {
    // Обробка тільки PC heartbeat
}
```

---

## 📨 2. Left_Brake_CMD - Команди від PC

### Специфікація:
```
CAN ID: 0x98FF0D09
Напрямок: PC → MCU
Формат:
- MSG_Id: лічильник команд (PC)
- Stamp: timestamp PC (мс)
- Brake_State: 0 = release, 1 = push
```

### Реалізація:

```c
// Розпакування команди
struct automate_left_brake_cmd_t brake_cmd;
automate_left_brake_cmd_unpack(&brake_cmd, msg.data, msg.len);

// Валідація
if (automate_left_brake_cmd_brake_state_is_in_range(brake_cmd.brake_state)) {
    // brake_cmd.msg_id - лічильник команд від PC
    // brake_cmd.stamp - коли PC сформував команду
    // brake_cmd.brake_state - 0 або 1
    Brake_ProcessCommand(brake_cmd.brake_state);
}
```

### Семантика:
- **MSG_Id**: лічильник команд від PC (для синхронізації)
- **Stamp**: час формування команди (для латентності)
- **Brake_State**: 
  - `0` (AUTOMATE_LEFT_BRAKE_CMD_BRAKE_STATE_PUSH_CHOICE) = відпустити
  - `1` (AUTOMATE_LEFT_BRAKE_CMD_BRAKE_STATE_RELEASE_CHOICE) = натиснути

---

## 📤 3. Left_Brake_MSG - Телеметрія до PC

### Специфікація:
```
CAN ID: 0x98FF0D0A
Напрямок: MCU → PC
Період: 100 мс
```

### Реалізація:

```c
brake_msg.msg_id = telemetry_msg_id++;    // MCU лічильник телеметрії
brake_msg.stamp = HAL_GetTick();          // MCU timestamp

// Стан операції (тільки один прапорець активний)
brake_msg.brake_releasing = (state == RELEASING) ? 1 : 0;
brake_msg.brake_released = (state == RELEASED) ? 1 : 0;
brake_msg.brake_pushing = (state == PUSHING) ? 1 : 0;
brake_msg.brake_pushed = (state == PUSHED) ? 1 : 0;

// Прогноз часу
brake_msg.time_to_end_operation = Brake_GetTimeToEnd();
```

### Семантика прапорців:

| Стан | Releasing | Released | Pushing | Pushed | Опис |
|------|-----------|----------|---------|--------|------|
| RELEASED | 0 | 1 | 0 | 0 | Гальмо відпущене |
| RELEASING | 1 | 0 | 0 | 0 | Виконується відпускання |
| PUSHED | 0 | 0 | 0 | 1 | Гальмо натиснуте |
| PUSHING | 0 | 0 | 1 | 0 | Виконується натискання |

---

## 🔍 4. Моніторинг комунікації

### PC Watchdog:

```c
#define WATCHDOG_TIMEOUT_MS  200  // 4 пропущені повідомлення @ 50ms
```

**Логіка:**
- PC повинен відправляти heartbeat кожні 50 мс
- Якщо немає повідомлень протягом 200 мс → WARNING
- MCU health переходить у WARNING стан
- Можлива захисна дія (наприклад, зупинка гальма)

**API:**
```c
bool Controller_IsPCAlive(void);                    // Перевірка зв'язку
uint32_t Controller_GetPCHeartbeatCount(void);      // Лічильник PC
uint32_t Controller_GetTimeSinceLastPCHeartbeat();  // Час з останнього
```

---

## 📊 5. Діаграма взаємодії

```
    PC                                MCU
     │                                 │
     │──── Heart_Beat_MSG ────────────>│ (Node_id=0x10, every 50ms)
     │                                 │
     │<─── Heart_Beat_MSG ─────────────│ (Node_id=0xF0, every 50ms)
     │                                 │
     │──── Left_Brake_CMD ────────────>│ (push/release command)
     │                                 │
     │<─── Left_Brake_MSG ─────────────│ (state + time, every 100ms)
     │                                 │
     
     Watchdog: якщо PC не відправляє > 200ms → MCU Health = WARNING
```



## ✅ 6. Чеклист відповідності специфікації

- [x] **Heart_Beat_MSG**: MCU використовує Node_id = 0xF0
- [x] **Heart_Beat_MSG**: Фільтрація PC heartbeat по Node_id = 0x10
- [x] **Heart_Beat_MSG**: Двостороннє повідомлення (MCU → PC та PC → MCU)
- [x] **Heart_Beat_MSG**: Період 50 мс
- [x] **Left_Brake_CMD**: Прийом команд від PC
- [x] **Left_Brake_CMD**: Валідація Brake_State (0 або 1)
- [x] **Left_Brake_MSG**: Відправка телеметрії кожні 100 мс
- [x] **Left_Brake_MSG**: Правильні прапорці стану
- [x] **Left_Brake_MSG**: Time_to_end_operation
- [x] **Watchdog**: Моніторинг PC heartbeat з timeout 200 мс
- [x] **Health**: Автоматичний перехід у WARNING при втраті зв'язку


### 7. Основний цикл:

```c
int main(void) {
    HAL_Init();
    SystemClock_Config();
    
    // Ініціалізація периферії
    MX_GPIO_Init();
    MX_FDCAN1_Init();
    MX_TIM1_Init();
    MX_ADC1_Init();
    
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    
    // Ініціалізація драйверів
    CAN_Driver_Init();
    Brake_Init();
    Controller_Init();  // Node_id = 0xF0
    
    while (1) {
        // Оновлення позиції (10 мс)
        Brake_UpdatePosition();
        
        // State machine (20 мс)
        Brake_Update();
        
        // CAN передача
        CAN_Driver_Transmit();
        
        // Бізнес-логіка (heartbeat 50ms, telemetry 100ms)
        BusinessLoop();
        
        // Перевірка зв'язку з PC
        if (!Controller_IsPCAlive()) {
            // PC не відповідає > 200 мс
            // Можлива захисна дія
        }
    }
}
```

### 8. Моніторинг:

```c
// Діагностика комунікації
printf("MCU Node_id: 0x%02X\n", Controller_GetNodeID());  // 0xF0
printf("PC alive: %s\n", Controller_IsPCAlive() ? "YES" : "NO");
printf("PC MSG_Count: %u\n", Controller_GetPCHeartbeatCount());
printf("Time since PC: %u ms\n", Controller_GetTimeSinceLastPCHeartbeat());
```

---

## 🎯 9. Тестування

### Тест 1: PC Heartbeat
```python
# PC надсилає heartbeat кожні 50 мс
msg = {
    'id': 0x98FF0D00,
    'node_id': 0x10,     # PC identifier
    'msg_count': counter++,
    'health': 1,         # ON
    'stamp': timestamp
}
# MCU має отримати та оновити last_pc_heartbeat_tick
```

### Тест 2: Timeout
```python
# Зупинити PC heartbeat на 250 мс
# MCU Health має перейти у WARNING після 200 мс
```

### Тест 3: Команди
```python
# PC відправляє Left_Brake_CMD
msg = {
    'id': 0x98FF0D09,
    'msg_id': cmd_counter++,
    'stamp': timestamp,
    'brake_state': 1  # Push
}
# MCU має виконати команду та відправити Left_Brake_MSG
```
