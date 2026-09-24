# Flux Tweaks Daemon

This is the core of Flux Tweaks that handles automatic profiles, configs and addons.

Before you digging into this code thinking this is some kind of scheduling module like Uperf, it's not. Flux Tweaks is a profile-style performance module, it simply applies performance tweaks as profiles and  <ins>do not dynamically control the scheduling and frequencies</ins>.

Flux Tweaks works by using information published by [SynthesisCore](https://github.com/FebriCahyaa/SynthesisCore), such as:
- Currently running app and its PID
- Screen state, whether it's awake or not
- Battery saver state (yes the ones on your quick settings)
- Thermal headroom and thermal status level
- Charging, audio and call state

## Workflow diagram

![Workflow diagram of Flux Tweaks daemon](./diagram.svg)
