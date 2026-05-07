# Benchmarking Cryptographic Algorithms on STM32L432KC (ARM Cortex-M4)

A hands-on benchmarking study that measures and compares the real-world performance of five cryptographic algorithms running on the **STM32L432KC** microcontroller — a popular ultra-low-power ARM Cortex-M4 board widely used in IoT and embedded systems.

All algorithms are implemented **in software only** (no hardware accelerators), so the results reflect true computational cost on constrained hardware.

---

## Table of Contents

- [Overview](#overview)
- [Why This Project?](#why-this-project)
- [Hardware Platform](#hardware-platform)
- [Algorithms Evaluated](#algorithms-evaluated)
- [Repository Structure](#repository-structure)
- [Methodology](#methodology)
- [Key Results Summary](#key-results-summary)
- [How to Reproduce](#how-to-reproduce)
- [Tools & Environment](#tools--environment)
- [Authors](#authors)

---

## Overview

This project benchmarks five cryptographic primitives across multiple payload sizes (16 B to 1024 B) on a single embedded platform under identical clock and compiler settings. The goal is to give IoT developers concrete, hardware-measured data to help them pick the right algorithm for their use case — whether they care most about speed, energy, memory, or security features.

**Algorithms tested:** AES-128 · SHA-256 · ASCON-128 · SPECK-64/128 · PRESENT-128

**Metrics collected:** Execution cycles · Cycles per byte · Throughput (KB/s) · Flash & RAM usage · Estimated energy per operation

---

## Why This Project?

Most cryptographic benchmarks in the literature are done on AVR microcontrollers or desktop-class processors, or they test only one type of algorithm (e.g., only block ciphers). There is very little published data that:

- Covers **all three types** — conventional ciphers (AES), lightweight ciphers (SPECK, PRESENT), authenticated encryption (ASCON), and hashing (SHA-256) — on the **same board**
- Tests **multiple payload sizes** to show how performance scales
- Includes **energy estimation** alongside cycle counts and throughput
- Targets the **ultra-low-power Cortex-M4** class specifically (STM32L4 series)

This project fills that gap with a unified, reproducible benchmarking framework.

---

## Hardware Platform

| Property | Value |
|---|---|
| Microcontroller | STM32L432KC |
| CPU Core | ARM Cortex-M4 |
| Clock Frequency | 80 MHz (fixed) |
| Supply Voltage | 3.3 V |
| Flash Memory | 256 KB |
| SRAM | 64 KB |
| Cryptographic Acceleration | **Not used** — software-only execution |
| Development Environment | STM32CubeIDE + ARM GCC |
| Compiler Optimization | -O2 |

The STM32L432KC was chosen because it is widely used in real IoT products (wearables, wireless sensors, industrial edge devices) and represents a realistic ultra-low-power embedded target.

---

## Algorithms Evaluated

| Algorithm | Type | Structure | Key Size |
|---|---|---|---|
| **AES-128** | Block cipher | Substitution-Permutation Network (SPN) | 128-bit |
| **SHA-256** | Hash function | Merkle–Damgård | 256-bit output |
| **ASCON-128** | Authenticated Encryption (AEAD) | Sponge permutation | 128-bit |
| **SPECK-64/128** | Lightweight block cipher | ARX (Add-Rotate-XOR) | 128-bit |
| **PRESENT-128** | Lightweight block cipher | SPN (hardware-oriented) | 128-bit |

**Why these five?**
- **AES-128** — the standard baseline for embedded symmetric encryption
- **SHA-256** — standard hash for firmware verification and secure boot
- **ASCON-128** — the NIST Lightweight Cryptography winner (2023); provides both confidentiality and integrity in one primitive
- **SPECK-64/128** — ARX design optimized for software on microcontrollers
- **PRESENT-128** — ultra-compact SPN designed for hardware; included to quantify the software penalty of a hardware-oriented design

---



## Methodology

### Execution Time Measurement

Execution time is measured using the **DWT (Data Watchpoint and Trace) cycle counter** built into the Cortex-M4 core. This is a hardware register that counts CPU cycles with zero software overhead, making it the standard method for embedded crypto benchmarking.

```
start_cycles = DWT->CYCCNT
run_crypto_function(payload, size)
end_cycles   = DWT->CYCCNT

elapsed_cycles = end_cycles - start_cycles
execution_time = elapsed_cycles / 80,000,000   // in seconds
```

### Payload Sizes Tested

Each algorithm is tested at **five payload sizes**: 16 B, 64 B, 128 B, 512 B, and 1024 B. This shows how performance scales and reveals per-operation overhead (initialization cost) that gets amortized over larger inputs.

### Metrics Collected

| Metric | How It's Calculated |
|---|---|
| **Execution Cycles** | Raw DWT counter difference |
| **Cycles per Byte (CPB)** | `total_cycles / payload_size_bytes` |
| **Throughput (KB/s)** | `payload_size / execution_time` |
| **Flash Usage** | Compiler build report (text section) |
| **RAM Usage** | Compiler build report (data + bss sections) |
| **Energy per Operation (µJ)** | `V × I × execution_time` where V = 3.3 V, I = 8 mA (from datasheet) |

### Energy Estimation

Energy is estimated analytically using the STM32L432KC datasheet's active-mode current spec. This is a relative comparison model — it does not replace direct current measurement but provides a consistent and reproducible way to rank algorithms by computational energy cost under identical conditions.

```
Energy = Voltage × Current × Execution_Time
       = 3.3 V × 0.008 A × Texec
```

### Experimental Controls

All experiments share the same conditions to ensure fair comparison:
- Fixed clock: **80 MHz**
- Fixed voltage: **3.3 V**
- Fixed compiler optimization: **-O2**
- Same benchmarking harness for all algorithms
- Each measurement repeated **50–100 iterations** and averaged
- No hardware cryptographic accelerators used

---

## Key Results Summary

### Execution Cycles (Encryption, selected sizes)

| Payload | AES-128 | ASCON-128 | SPECK-64/128 | PRESENT-128 | SHA-256 |
|---|---|---|---|---|---|
| 16 B | 6,335 | 10,272 | **468** | 117,364 | 14,480 |
| 128 B | 49,937 | 29,417 | **3,666** | 938,694 | 43,974 |
| 1024 B | 401,523 | 182,576 | **29,314** | 7,509,359 | 252,984 |

### Throughput (Encryption, KB/s)

| Payload | AES-128 | ASCON-128 | SPECK-64/128 | PRESENT-128 | SHA-256 |
|---|---|---|---|---|---|
| 16 B | 202 | 125 | **2,670** | 10.6 | 86 |
| 1024 B | 204 | **449** | 2,729 | 10.6 | 316 |

### Memory Footprint

| Algorithm | Flash (bytes) | RAM (bytes) |
|---|---|---|
| SPECK-64/128 | **23,680** | 3,028 |
| PRESENT-128 | 24,600 | 6,612 |
| AES-128 | 26,168 | **2,996** |
| SHA-256 | 29,284 | **2,512** |
| ASCON-128 | 45,588 | 5,660 |

### Top-Level Takeaways

- **SPECK-64/128** is the fastest and most energy-efficient algorithm in this study — lowest cycle count, highest throughput (~2,729 KB/s), smallest flash footprint, and lowest energy per byte (~0.009 µJ/B). Best choice when raw speed and energy efficiency are the priority.

- **ASCON-128** is the best choice when you need **both encryption and authentication** in a single primitive. Its throughput improves with payload size and surpasses AES-128 for inputs larger than 64 bytes. It is the NIST-standardized lightweight crypto winner.

- **AES-128** delivers stable, predictable performance (~202–205 KB/s) regardless of payload size. It is the right choice when protocol interoperability (TLS, DTLS) is required.

- **SHA-256** has the smallest RAM footprint (2,512 bytes) and is the right primitive for firmware integrity verification and secure boot — it's a hash, not an encryptor.

- **PRESENT-128** performs extremely poorly in software on this architecture — ~256× more cycles than SPECK for the same data. Its bit-permutation layer was designed for hardware gates, not 32-bit word operations. **Not recommended for software deployment on Cortex-M4.**

---

## How to Reproduce

### Prerequisites

- STM32L432KC Nucleo-32 board (or equivalent)
- STM32CubeIDE (v1.x or later)
- ARM GCC toolchain (bundled with CubeIDE)
- Python 3.x with `pandas`, `matplotlib` for analysis scripts
- USB cable for UART output

### Steps

1. **Clone this repository**
   ```bash
   git clone https://github.com/anantha-c/Benchmarking_crypto_algorithms.git
   cd Benchmarking_crypto_algorithms
   ```

2. **Open firmware in STM32CubeIDE**
   - Import the project from `Firmware_/firmware/`
   - Select the algorithm you want to benchmark
   - Build with `-O2` optimization (already configured)

3. **Flash and run**
   - Connect the STM32L432KC via USB
   - Flash the firmware using STM32CubeIDE or `st-flash`
   - Open a serial terminal at 115200 baud
   - Results are printed automatically over UART

4. **Analyze results**
   ```bash
   cd scripts_/scripts
   python parse_results.py          # Parse raw UART logs
   python plot_cycles.py            # Plot execution cycles
   python plot_throughput.py        # Plot throughput
   python plot_energy.py            # Plot energy estimates
   ```

---

## Tools & Environment

| Tool | Purpose |
|---|---|
| STM32CubeIDE | Firmware development, build, and flash |
| ARM GCC | Compiler (bundled with CubeIDE) |
| DWT Cycle Counter | Cycle-accurate execution timing (hardware) |
| Python + matplotlib | Result parsing and visualization |
| UART / Serial Monitor | Capturing benchmark output from the board |

---

## Authors

**Anantha SaiCharan** — Department of Computer Science and Engineering, PES University, Bengaluru
- GitHub: [@anantha-c](https://github.com/anantha-c)
- Email: anantha.c.charan@gmail.com

**Vadiraja A** — Department of Electronics and Communication Engineering, PES University, Bengaluru
- Email: vadiraja@pes.edu

**Prasad B. Honavalli** — PES University, Bengaluru
- Email: prasadhb@pes.edu

---

## Related Publication

This repository accompanies the paper:

> *Experimental Performance Benchmarking of Cryptographic Algorithms on STM32 Cortex-M4 Microcontroller for Resource-Constrained IoT Applications*
> Anantha SaiCharan, Vadiraja A, Prasad B. Honavalli — PES University, 2026

---

*If you find this useful, feel free to star the repo or open an issue for questions.*
