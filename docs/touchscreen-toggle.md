# Toggling the touchscreen to avoid a Hyprland crash

Hyprland 0.56.2 can crash with SIGSEGV when a touchscreen is tapped right after resuming from suspend while the session is locked. If the tap arrives before the lock client has created a lock surface for the re-enabled output, `CInputManager::onTouchDown` passes a null surface to `CSeatManager::sendTouchDown`, which then dereferences it. Undocking before suspend makes the gap more likely, because the internal panel is re-added on resume.

After the crash, `start-hyprland` restarts Hyprland with `recoverycfg.lua`. Because the session was locked, the recovery instance stays locked and shows "the lockscreen app died".

Upstream report: [hyprwm/Hyprland#16066](https://github.com/hyprwm/Hyprland/discussions/16066).

## Workaround: make libinput ignore the touchscreen

The touchpad is a pointer device and keeps working. The workaround sits below Hyprland, so it also covers the recovery instance.

Find the touchscreen's vendor and product IDs:

```sh
for d in /dev/input/event*; do
  udevadm info "$d" | grep -q ID_INPUT_TOUCHSCREEN=1 && echo "$d"
done
udevadm info -a /dev/input/eventN | grep -m2 -E 'ATTRS\{id/(vendor|product)\}'
```

Create `/etc/udev/rules.d/99-disable-touchscreen.rules`, substituting your IDs:

```udev
ACTION=="add|change", SUBSYSTEM=="input", KERNEL=="event*", ATTRS{id/vendor}=="3558", ATTRS{id/product}=="2002", ENV{LIBINPUT_IGNORE_DEVICE}="1"
```

Match on `ATTRS{id/...}` rather than `ENV{LIBINPUT_DEVICE_GROUP}`. The device group comes from an `IMPORT{program}` in `80-libinput-device-groups.rules`, and a rule matching on it never set the property on a change event in testing.

Apply it and confirm the property is set:

```sh
sudo udevadm control --reload
sudo udevadm trigger --action=change --settle /sys/class/input/eventN
udevadm info /dev/input/eventN | grep LIBINPUT_IGNORE_DEVICE
```

libinput reads the property only when it opens a device, so log out and back in (or restart Hyprland) for it to take effect.

## Toggling

udev loads only files ending in `.rules`, so renaming the file switches the workaround on and off.

Turn touch back on:

```sh
sudo mv /etc/udev/rules.d/99-disable-touchscreen.rules{,.off}
sudo udevadm control --reload
sudo udevadm trigger --action=change /sys/class/input/eventN
```

Turn touch off again:

```sh
sudo mv /etc/udev/rules.d/99-disable-touchscreen.rules{.off,}
sudo udevadm control --reload
sudo udevadm trigger --action=change /sys/class/input/eventN
```

Log out and back in after either change.

Turn touch back on once a Hyprland release guards the null surface in `onTouchDown`.
