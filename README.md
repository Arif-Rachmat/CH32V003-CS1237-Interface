# CH32V003 × CS1237 — Bidirectional SPI interface

<p align="center">
  <img src="https://img.shields.io/badge/MCU-WCH CH32V003%20-blue" alt="MCU">
  <img src="https://img.shields.io/badge/ADC-CS1237%20(24--bit)-orange" alt="ADC">
  <img src="https://img.shields.io/badge/Interface-Bidirectional%20SPI-brightgreen" alt="Interface">
  <img src="https://img.shields.io/badge/Language-C-informational" alt="Language">
  <img src="https://img.shields.io/badge/Platform-PlatformIO-blueviolet" alt="PlatformIO">
</p>

Interfacing CS1237 24-bit Sigma-Delta ADC to CH32V003 MCU using only Two-Wire SPI (Bidirectional SPI).



## What Is It?

The CH32V003 is an ultra-cheap, 48 MHz 32-bit microcontroller developed by WCH. It's popular because it offers 32-bit performance for roughly $0.10 to $0.30, where usually that is the price point of very limited 8-bit microcontrollers.

While the CS1237 is a high-precision, low-power Analog-to-Digital Converter (ADC) designed by Chipsea and it's also a Very cheap device.

This firmware reads the 24-bit data from CS1237 continuously and convert it into signed raw value. The CS1237 communicates with the MCU over a single bidirectional data line for both control and data transfer:

1. Using EXTI to detects falling edge on DRDY/DOUT (data ready)
2. Clocks out 24 bits of ADC data (MSB first) via SPI BIDIMODE
3. Sign-extends the 24-bit result to 32-bit signed integer
4. Prints via UART and re-arms EXTI for the next sample

## The Challange

Having a non-standard SPI protocol, the cs1237 has been a challenge for embedded developers who want to avoid messy, inefficient code. Unlike conventional SPI peripherals that utilize a dedicated Chip Select ($\overline{\text{CS}}$) line to cleanly frame data packets, the CS1237 operates on a strict clock-pulse counting mechanism layered across a single, shared bidirectional data line.

The standard practice for this kind of protocol has to just been a software bit-banging. While bit-banging gives absolute control over the clock edges, it forcefully locks the CPU core in blocking loop structures, leaving little runtime availability for heavy signal processing, digital filtering, or proper UART communications.

This project resolves the conflict by harnessing the native, underutilized hardware Bidirectional SPI capabilities of the CH32V003. This architecture gains the timing safety of an explicit hardware gate while keeping data ingestion completely non-blocking.


## Hardware Architecture
![Simple Circuit Diagram](/Img/SampleCircuit.png)

It is highly recommended to use a proper low-noise external Voltage Reference


## The Protocol

### Data Read Sequence

```
PC6 act as EXTI6 waiting for interrupt signal
         │
         ▼
CS1237 pulls DRDY LOW → Data ready
         │
         ▼
MCU detects falling edge on EXTI6
         │
         ▼
MCU enables SPI (BIDIMODE, output disabled) → SCLK starts
         │
         ▼
CS1237 shifts out 24 bits MSB first on each SCLK rising edge
         │
         ▼
MCU sign-extends 24-bit value → prints via UART
         │
         ▼
MCU re-enables EXTI → waits for next DRDY LOW
```


## Timing Is Everything
Being a Non-Standard SPI Protocol, we have to pays a lot of attention to its signal timing

![CS1237 Timing Diagram](/Img/TimingDiagram.png)

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| SCLK high/low pulse | t5 | 455 | — | ns |
| SCLK → data valid | t6 | — | 455 | ns |
| Data hold time | t7 | 227.5 | 455 | ns |

---

## Output Data
The microcontroller reads 3 Byte of data outputted by the CS1237, but stores it into 32 Bit Signed Integer Variable

## Building & Flashing

```bash
pio run                    # Build
pio run --target upload    # Flash via WCH-LinkE
pio device monitor         # Monitor output at 9600 baud
```

Expected output:
```bash
CH32V003 CS1237 ADC Reader
ADC: 1234567               # Example ADC Value
ADC: 1234890
ADC: 1234123
```

---

## Configuring the CS1237

**⚠️The Configuration Function Is not yet impelmented, due to the limitation of the protocol, it requires to be manually bit-banged**
For now its using the default value of the configuration register.

### Config Register Map (Address 0x0C)

| Bits | Name | Function | Default |
|------|------|----------|---------|
| [7] | — | Reserved (write 0) | 0 |
| [6] | REFO_OFF | 1 = disable internal reference | 0 |
| [5:4] | SPEED | 00=10Hz, 01=40Hz, 10=640Hz, 11=1280Hz | 00 |
| [3:2] | PGA | 00=1×, 01=2×, 10=64×, 11=128× | 11 |
| [1:0] | CH | 00=ChA, 10=Temp, 11=Internal short | 00 |

### Reset Value: `0x0C`

---

## Why This Project Exists

The CS1237 is popular in precision weight scales and bridge sensor applications. Most implementations use bit-banged GPIO or external converters. This project demonstrates that the CH32V003's `SPI BIDIMODE` peripheral can handle the protocol natively, no extra components, no bit-banging, just two wires.

Moreover, both of the device is popular in the ultra-cheap.....

CH32V003 (~$0.30) + CS1237 (~$0.50) = **$1 High-Res measurement front-end**

**It's CHEAP!**



## License

MIT