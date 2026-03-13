# Sampled Trace

`Sampled Trace` is an Unreal Engine plugin designed for melee hit detection based on animation montages.  
It helps improve hit stability during low FPS situations and fast attacks.

At runtime, the plugin samples trace points from animation data, caches them within a trace window, and then performs collision checks using swept box traces between the cached poses.

## Features

- Montage-based trace windows
- Built-in workflow using `AnimNotifyState`
- Manual activation from Blueprint or C++
- Multiple trace modes
- Configurable sockets, trace thickness, sampling interval, and query mode
- Delegates for hit results
- Optional debug drawing

## Main Classes

### `SampledTraceComponent`
The main runtime component that manages trace windows, cached samples, trace execution, and hit events.

### `AnimNotify_SampledTraceWindow`
A built-in `AnimNotifyState` that automatically starts and ends a trace window from an animation montage.

## Trace Modes

### `SweepBetweenSamples`
Performs sweeps between neighboring animation samples to cover movement between poses.

### `SamplePoseOnly`
Processes only the sampled poses. Additional interpolation steps can be added if needed.

---

# Setup

## 1. Enable the plugin
Enable the plugin in your project.

## 2. Add the component
Add `SampledTraceComponent` to your character.

## 3. Configure trace sockets
Create at least 2 sockets on the character’s skeletal mesh.

These sockets define the trace shape along the weapon or attack path.

Example:
- `Weapon_Base`
- `Weapon_Mid`
- `Weapon_Tip`

## 4. Choose how to start the trace

You can use either the built-in workflow through `AnimNotifyState`, or manually start traces using `BeginTraceWindow` or `BeginTraceWindowFromMontage`.

---

# Option A: Using Anim Notify State

## Add `SampledTraceWindow` to a montage
Open your animation montage and add the `SampledTraceWindow` Anim Notify State over the desired attack section.

## Configure the notify
Set the trace settings directly on the notify:

- Trace sockets
- Sample interval
- Trace thickness
- Query mode
- Debug options
- Ignore self
- Unique actor filtering

## Play the montage
When the montage plays:

- the trace window starts in `NotifyBegin`
- the trace window ends in `NotifyEnd`

---

# Option B: Manual activation from Blueprint / C++

Use `BeginTraceWindow` or `BeginTraceWindowFromMontage` if you need custom trace activation logic.

## Blueprint

Call `BeginTraceWindowFromMontage` and provide the required parameters:

- Trace Settings
- Montage
- Window Start Time
- Window End Time

The function returns an `int32` trace handle.

Store this handle if you want to stop the trace manually later using `EndTraceWindow`.

If needed, you can stop all active traces using `EndAllTraceWindows`.

To track when a trace window ends and to receive hit results, use the following delegates:

- `OnTraceWindowEnded`
- `OnTraceHitsDetected`

Trailer - https://youtu.be/NTHnjkdSp-8?si=jfhaPsxG3k-qUuiI
Tutorial - https://www.youtube.com/watch?v=iCusfRU7tZY