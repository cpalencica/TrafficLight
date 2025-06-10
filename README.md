# Traffic Light Kernel Module – BeagleBone Black

## Overview

This embedded systems project implements a Linux kernel module that simulates a model traffic light system on a BeagleBone Black using GPIO pins. The module controls LEDs (red, yellow, green) and reads input from physical buttons to simulate both traffic control and pedestrian interaction modes. All interactions take place within **kernel space** — no user-space applications are involved.

---

## Features

- **GPIO Control from Kernelspace:** LEDs are toggled and buttons are read directly via kernel-level GPIO manipulation.
- **Multiple Modes:**
  - Normal traffic cycle: green → yellow → red
  - Flashing red mode
  - Flashing yellow mode
- **Pedestrian Call Support:** A pedestrian button allows interrupt-based mode switching to enable pedestrian crossing.
- **Custom Cycle Rate:** Adjustable cycle timing through kernel write interface.
- **Character Device Interface:** Supports read/write operations for monitoring and setting configuration.

---

## Hardware Setup

- **BeagleBone Black**
- **LEDs:** Connected to GPIO pins 44 (Green), 67 (Red), 68 (Yellow)
- **Buttons:** Connected to GPIO pins 26 (BTN0), 46 (BTN1)

Circuit assembly instructions and pin mapping are derived from the BeagleBone Black System Reference Manual.

---

## Demo

[Click to watch the demo](videos/TrafficLight.mp4)

*Note: GitHub does not support inline video playback for `.mp4` files. The video can be downloaded or viewed directly via the link above.*

---

## Building and Running

### 1. Clone this repo

```bash
git clone https://github.com/yourusername/traffic-light-kernel-module.git
cd traffic-light-kernel-module
```

### 2. Build the Kernel Module
Make sure you have the correct kernel headers installed on the BeagleBone.
```bash
make
```

### 3. Insert the Module
```bash
sudo insmod mytraffic.ko
```

### 4. Interact with the Module
``` bash
cat /dev/mytraffic
echo "2" > /dev/mytraffic   # Change cycle rate (e.g., to 2 Hz)
```

### 5. Remove the Module
```bash
sudo rmmod mytraffic
```

## Files
- mytraffic.c – Main kernel module implementation.
- Makefile – Kernel module build file.
- videos/traffic_light_demo.mp4 – Demo video (linked).

## Development Notes
- Interrupt handlers were implemented to switch between modes based on physical button input.
- Kernel-space timers (based on jiffies) cycle through LED states.
- Read/write system calls are implemented to interact with the kernel module.
- Internal state (mode, LED status, pedestrian calls) is exposed via the character device.

