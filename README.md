# Sampled Trace

`Sampled Trace` — это плагин для Unreal Engine, предназначенный для melee hit detection на основе animation montage.  
Он помогает повысить стабильность попаданий при низком FPS и во время быстрых атак.

Во время работы плагин сэмплирует точки трейса из анимации, кэширует их в пределах trace window, а затем выполняет collision-проверки с помощью sweep box trace между сохранёнными позами.

## Возможности

- Trace window на основе montage 
- Встроенный workflow через `AnimNotifyState`
- Ручной запуск из Blueprint или C++
- Несколько режимов работы
- Настраиваемые sockets, толщина трейса, sampling interval и query mode
- Делегаты для hit results
- Опциональный debug draw

## Основные классы

### `SampledTraceComponent`
Главный runtime-компонент, который управляет trace window, кэшированными сэмплами, выполнением трейсов и hit-событиями.

### `AnimNotify_SampledTraceWindow`
Встроенный `AnimNotifyState`, который автоматически запускает и завершает trace window из animation montage.

## Режимы трейса

### `SweepBetweenSamples`
Выполняет sweep между соседними анимационными сэмплами, чтобы покрыть движение между позами.

### `SamplePoseOnly`
Обрабатывает только сами сэмплированные позы. При необходимости можно добавить дополнительные интерполяционные шаги между ними.

---

# Установка и настройка

## 1. Включите плагин
Включите плагин в вашем проекте.

## 2. Добавьте компонент
Добавьте `SampledTraceComponent` на вашего персонажа.

## 3. Настройте trace sockets
Создайте как минимум 2 сокета на skeletal mesh персонажа.

Эти сокеты определяют форму трейса вдоль оружия или траектории удара.

Пример:
- `Weapon_Base`
- `Weapon_Mid`
- `Weapon_Tip`

## 4. Выберите способ запуска трейса

Можно использовать как встроенный workflow через `AnimNotifyState`, так и ручной запуск через `BeginTraceWindow` или `BeginTraceWindowFromMontage`.

---

# Вариант A: через Anim Notify State

## Добавьте `SampledTraceWindow` в montage
Откройте animation montage и добавьте `SampledTraceWindow` Anim Notify State на нужный участок атаки.

## Настройте notify
Настройте trace settings прямо в notify:

- Trace sockets
- Sample interval
- Trace thickness
- Query mode
- Debug options
- Ignore self
- Unique actor filtering

## Проиграйте montage
Во время проигрывания montage:

- trace window запускается в `NotifyBegin`
- trace window завершается в `NotifyEnd`

---

# Вариант B: ручной запуск из Blueprint / C++

Используйте `BeginTraceWindow` или `BeginTraceWindowFromMontage`, если вам нужна собственная логика запуска трейса.

## Blueprint

Вызовите `BeginTraceWindowFromMontage` и укажите необходимые параметры:

- Trace Settings
- Montage
- Window Start Time
- Window End Time

Функция возвращает `int32` handle трейса.

Сохраните этот handle, если хотите позже вручную остановить трейс через `EndTraceWindow`.

При необходимости можно остановить все активные трейсы через `EndAllTraceWindows`.

Для отслеживания окончания trace window и результатов попадания используйте делегаты:

- `OnTraceWindowEnded`
- `OnTraceHitsDetected`

Trailer - https://youtu.be/NTHnjkdSp-8?si=jfhaPsxG3k-qUuiI
Tutorial - https://www.youtube.com/watch?v=iCusfRU7tZY