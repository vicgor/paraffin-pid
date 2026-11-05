# Архитектура и роли датчиков

[Вернуться к README](../README.md)

## Контексты выполнения

Схема показывает границы выполнения и обмен данными, а не точный порядок вызовов функций.

```mermaid
flowchart TB
    subgraph Service["Обслуживание — loop()"]
        Clients["UI, USB CLI, HTTP, MQTT"]
        Store["Отложенное сохранение настроек во flash"]
    end

    API["app.* — команды и снимки состояния"]
    Sync["hal::Mutex g_lock — команды и снимки"]
    Flags["volatile-флаги swap / rescan"]

    Clients <--> API
    API <--> Sync
    API --> Flags
    API --> Store

    subgraph Control["Контур управления — период 5 мс"]
        Tick["controlTick"]
        Sensors["TempSensors.tick"]
        Controller["Controller.step при новом измерении"]
        Timeout["Контроль отсутствия измерений"]
        SSR["SsrOutput — обновление выхода"]
        WDT["Обслуживание watchdog"]

        Tick -. "Каждый вызов" .-> WDT
        Tick --> Sensors
        Tick --> Timeout
        Sensors --> Controller
        Timeout -->|"Нет измерений 5 с"| Controller
        Controller --> SSR
    end

    Sync <--> Controller
    Flags -. "Проверка в начале controlTick" .-> Sensors
```

- ESP32: отдельная задача FreeRTOS; RP2350: отдельное ядро.
- Период контекста управления 5 мс не равен периоду измерений. В текущем драйвере DS18B20 преобразования запускаются раз в 1000 мс, чтение выполняется после 760 мс без блокирующего ожидания преобразования. Сами обмены по 1-Wire (`getTempC()`, поиск датчиков `discover()`) синхронные и выполняются внутри `controlTick`, занимая единицы–десятки мс.
- Запросы swap / rescan передаются через volatile-флаги и выполняются в начале controlTick; swap выполняется под g_lock.
- Веб, MQTT, отрисовка и сохранение настроек выполняются вне контура управления.
- Сторожевой таймер (5 с) кормит только контур управления. Зависание `loop()` или сети сброса не вызовет: нагрев продолжит регулироваться, но команды с UI, веба и MQTT перестанут обрабатываться.
- Во время записи настроек во flash контур может замереть: на ESP32 отключается кэш flash, на RP2350 второе ядро приостанавливается. SSR в это время сохраняет текущее состояние.
- Схема не описывает запуск после перезагрузки и не является доказательством отсутствия гонок или блокировок.

Источники: [main.cpp](../src/main.cpp), [app.cpp](../src/app.cpp), [hal.cpp](../src/hal.cpp), [sensors.cpp](../src/sensors.cpp).

## Размещение по ядрам

```mermaid
flowchart LR
    subgraph RP["RP2350 · arduino-pico, 2 ядра"]
        direction TB
        RPLOOP["Core 0<br/>setup() / loop()<br/>USB CDC, CYW43 на pico2w"]
        RPCTL["Core 1<br/>setup1() ждёт старта<br/>loop1(): тик каждые 5 мс"]
        RPWDT["Аппаратный WDT 5 с<br/>кормит только core 1"]
        RPLOOP <-->|"pico mutex_t"| RPCTL
        RPWDT -.-> RPCTL
    end

    subgraph S3["ESP32-S3 · FreeRTOS, 2 ядра"]
        direction TB
        S3WIFI["Core 0<br/>Wi-Fi / lwIP<br/>приоритеты по умолчанию ESP-IDF"]
        S3CTL["Core 1 · задача control<br/>приоритет 5<br/>vTaskDelayUntil 5 мс"]
        S3LOOP["Core 1 · loopTask<br/>loop(), приоритет 1"]
        S3TWDT["TWDT 5 с<br/>подписана только задача control"]
        S3CTL <-->|"FreeRTOS mutex"| S3LOOP
        S3TWDT -.-> S3CTL
    end
```

- RP2350: период в `loop1()` выдерживается опросом `millis()` с `delay(1)`, поэтому возможен джиттер около 1 мс.
- ESP32-S3: задача control закреплена за `ARDUINO_RUNNING_CORE` (ядро 1) и вытесняет `loop()` за счёт более высокого приоритета; мьютекс FreeRTOS наследует приоритет.
- ESP32-C6 пока не поддерживается: окружений сборки нет.

Источники: [hal.cpp](../src/hal.cpp), [app.cpp](../src/app.cpp).

## Роли датчиков

```mermaid
flowchart TB
    Target["Уставка ванны"]
    PV["Датчик ванны<br/>procC / pv"]
    Heater["Датчик нагревателя<br/>heaterC / heater"]

    subgraph Regulation["Регулирование"]
        Ramp["Рампа уставки<br/>rampCpm / rampHoldC"]
        PID["ПИД ванны"]
        Tune["Autotune"]

        Ramp --> PID
    end

    Target --> Ramp
    PV --> Ramp
    PV --> PID
    Target --> Tune
    PV --> Tune

    subgraph Protection["Защиты"]
        Safety["Safety<br/>датчики, перегрев, NO RISE, SSR STUCK"]
        Fault["Фиксация аварии<br/>режим FAULT"]

        Safety -->|Авария| Fault
    end

    PV --> Safety
    Heater --> Safety

    Context["Режим, уставка<br/>предыдущий выход регулятора"]
    Context --> Safety

    Output["Выбор итоговой мощности<br/>HEAT: PID; TUNE: Autotune<br/>STOP / FAULT: 0"]

    PID --> Output
    Tune --> Output
    Fault --> Output

    Output --> SSR["SsrOutput<br/>оконное управление SSR"]
```

Схема показывает зависимости данных, а не порядок вызовов. Недостоверное измерение температуры ванны также обнуляет выход в HEAT и TUNE, даже до фиксации аварии SENSOR. Проверки имеют собственные условия и выдержки времени; подробности — в таблице защит ниже.

Датчик ванны — обратная связь ПИД и Autotune. Датчик нагревателя не входит непосредственно в расчёт ПИД: каскадное регулирование и плавный ограничитель мощности по его температуре пока не реализованы. Показания двух датчиков не усредняются.

| Проверка | Использование измерений |
|---|---|
| OVERTEMP | Достоверная температура ванны не ниже maxProcessC |
| HTR HOT | Достоверная температура обнаруженного датчика нагревателя не ниже maxHeaterC |
| SENSOR / HTR SENS | Ошибки чтения; отсутствие обязательного датчика нагревателя |
| NO RISE | Больший из приростов температуры ванны и достоверного датчика нагревателя за контрольное окно |
| SSR STUCK | Рост большей из доступных достоверных температур при выключенном режиме нагрева; проверка требует достоверной температуры ванны |

heaterSensorRequired=0 разрешает отсутствие второго датчика, но не отключает проверки уже обнаруженного датчика. Команда swap меняет роли щупов; после неё необходимо проверить соответствие pv физическому датчику ванны.

Оба DS18B20 используют общую шину 1-Wire и разные ROM-адреса. Команда преобразования отправляется всем датчикам одновременно. Второй датчик не заменяет первый при его отказе.

Источники: [Controller.cpp](../lib/control/src/Controller.cpp), [Safety.cpp](../lib/control/src/Safety.cpp), [sensors.cpp](../src/sensors.cpp).

## Границы изменения

Это документация текущего алгоритма, а не схема электрического подключения и не изменение защит. Все окружения остаются без изменений; новые unit-тесты не требуются. Перед слиянием следует проверить отображение Mermaid и соответствие подписей исходному коду.
