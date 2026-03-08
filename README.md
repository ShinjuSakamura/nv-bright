# nv-bright

`nv-bright` is a small user-space daemon I put together to workaround broken OLED brightness controls on my laptop when running in NVIDIA dGPU mode. 

**Note: This was created specifically for, and only tested on, a Lenovo Legion Pro 7 (83RU) with an RTX 5080 and a Samsung OLED panel (`ATNA60HU02-0`). I cannot guarantee it will work on anything else, though it might be helpful for people facing similar issues on other modern OLED laptops.**

## The Problem (As I Understand It)
On my laptop, the brightness controls work perfectly when using the integrated graphics, but fail completely when switched to the discrete NVIDIA GPU.

Even after applying an ACPI DSDT/SSDT override to force the BIOS to tell the NVIDIA driver to use the DPCD backlight interface (which successfully creates the `/sys/class/backlight/nvidia_0` file), moving the GNOME brightness slider does nothing to the actual screen.

With some help from AI, we investigated and found that the NVIDIA driver (or its internal firmware) seems to be failing to actually send the required DisplayPort AUX commands to the panel when the slider is moved.

## The Workaround
`nv-bright` acts as a middleman. It watches the standard Linux `nvidia_0` backlight sysfs file for changes. When you move your brightness slider, `nv-bright` reads the requested percentage and uses the NVIDIA RM API (`CMD_DP_AUXCH_CTRL`) to manually write the raw byte values directly to the DPCD `PANEL_TARGET_LUMINANCE` registers (`0x734` - `0x736`).

By bypassing the NVIDIA driver's internal backlight logic and talking directly to the hardware over the I2C/AUX bus, this tool successfully dims the screen.

## Dynamic EDID Parsing
The `nv-bright-wrapper` script uses `edid-decode` to read your OLED panel's raw EDID, attempts to find its true maximum luminance (in nits), and dynamically scales the brightness curve to match it. If it fails, it defaults to a safe 500 nits.

## Installation

### Prerequisites
*   `gcc` and `make`
*   `edid-decode` (used by the wrapper to automatically determine your screen's max nits)

### Build and Install
```bash
make
sudo make install
```

### Enable the Daemon
```bash
sudo systemctl enable --now nv-bright.service
```

## Manual Usage
You can also run `nv-bright` manually to inspect your display's DPCD capabilities or force a specific luminance for testing.

```bash
# Show current display state and capabilities
sudo nv-bright

# Watch the sysfs file and apply changes automatically (min nits, max nits)
sudo nv-bright watch 5 500

# Set a specific absolute luminance (in milli-cd/m²)
sudo nv-bright lum 250000

# Dump all eDP DPCD registers
sudo nv-bright dump
```

## Note for Fedora / RPM Users
An RPM spec file (`nv-bright.spec`) is included. You can build a native package using `rpmbuild -ba nv-bright.spec`.

## Disclaimer
This tool interacts directly with your graphics card's low-level RM API. Use this entirely at your own risk. It is intended as a temporary band-aid until the underlying bugs are resolved.
