# Custom Flight Controller - SAMV71Q21B (EV61G06A)

## Board Overview
- **MCU:** ATSAMV71Q21B (ARM Cortex-M7, 300MHz, 2MB Flash, 384KB SRAM)
- **Package:** LQFP-144
- **Part Number:** EV61G06A
- **PCB:** Custom drone flight controller designed in Altium

## Clock Configuration
- **Main Oscillator:** 12 MHz external crystal (Y1: DSC6011JI2B-012.0000) — REQUIRED
- **Slow Clock:** 32.768 kHz crystal (Y2: VMK3-9005-32K7680000)
- **PLL:** 12 MHz × 25 = 300 MHz (CPU), MCK = 150 MHz
- **Note:** Without Y1 populated, firmware will NOT boot correctly (garbled UART)

## Programming
- **Interface:** SWD (SWDIO=PB6, SWCLK=PB7)
- **Connector:** J2 (1x6 pin header): Pin1=RST, Pin2=VCC_3.3V, Pin3=GND, Pin4=SWDIO, Pin5=SWCLK, Pin6=NC
- **Tool:** PICkit 4 via MPLAB X/IPE, or J-Link (full, not J-32)
- **Flash command:** Use MPLAB IPE → Device: ATSAMV71Q21B → Load .hex → Program

## Build


## Debug Console
- **USART1:** PA21 (RX), PB4 (TX) — on J1 header (4-pin)
- **Baud:** 115200 8N1
- **Tool:** TeraTerm / PuTTY on the USB-UART adapter COM port

## Pin Mapping

### Motors (8 channels)
| Motor | Pin  | Peripheral         | Type |
|-------|------|--------------------|------|
| M1    | PB0  | PWMC0 CH0 (PerA)  | PWM  |
| M2    | PA2  | PWMC0 CH1 (PerA)  | PWM  |
| M3    | PC19 | PWMC0 CH2 (PerB)  | PWM  |
| M4    | PC13 | PWMC0 CH3 (PerB)  | PWM  |
| M5    | PC29 | TC1 CH2 TIOA5 (PerB) | Timer |
| M6    | PC23 | TC1 CH0 TIOA3 (PerB) | Timer |
| M7    | PC5  | TC2 CH0 TIOA6 (PerB) | Timer |
| M8    | PA15 | TC0 CH1 TIOA1 (PerB) | Timer |

### SPI Buses
| Bus  | Function | CS   | SCK  | MOSI | MISO | INT  |
|------|----------|------|------|------|------|------|
| SPI0 | IMU1     | PD12 | PD22 | PD21 | PD20 | PC2  |
| SPI1 | IMU2     | PC25 | PC24 | PC27 | PC26 | PC3  |
| QSPI | Flash    | PA11 | PA14 | PA13(QIO0) | PA12(QIO1) | — |

### I2C Buses
| Bus  | Function              | SDA  | SCL  |
|------|-----------------------|------|------|
| I2C0 | BMP388, BMM150, EEPROM | PA3  | PA4  |
| I2C2 | GPS, Power Sensor     | PD27 | PD28 |

### Serial Ports
| Port    | Function    | TX   | RX   | RTS  | CTS  |
|---------|-------------|------|------|------|------|
| USART1  | Debug/NSH   | PB4  | PA21 | —    | —    |
| UART0   | Telemetry 2 | PA10 | PA9  | —    | —    |
| USART2  | Telemetry 1 | PD16 | PD15 | PD18 | PD19 |
| UART2   | GPS         | PD26 | PD25 | —    | —    |
| UART1   | RC Input    | —    | PA5  | —    | —    |

### CAN
| Bus  | TX  | RX  | STB (standby) | Transceiver  |
|------|-----|-----|---------------|--------------|
| CAN0 | PB2 | PB3 | PA29          | ATA6563      |

### SD Card (HSMCI)
| Signal | Pin  |
|--------|------|
| MCCK   | PA25 |
| MCCDA  | PA28 |
| MCDA0  | PA30 |
| MCDA1  | PA31 |
| MCDA2  | PA26 |
| MCDA3  | PA27 |
| MCCD   | PA24 |

### Ethernet (LAN8720A PHY)
| Signal  | Pin  |
|---------|------|
| REFCLK  | PD0  |
| TXEN    | PD1  |
| TXD0    | PD2  |
| TXD1    | PD3  |
| RXDV    | PD4  |
| RXD0    | PD5  |
| RXD1    | PD6  |
| RXER    | PD7  |
| MDC     | PD8  |
| MDIO    | PD9  |

### LEDs
| LED  | Pin  | Color |
|------|------|-------|
| LED1 | PC17 | Green |
| LED2 | PA0  | Red   |
| LED3 | PD10 | Blue  |

### Power Control
| Function          | Pin  |
|-------------------|------|
| 3.3V Periph EN    | PB13 |
| 3.3V Periph FLT   | PE0  |
| 5V Periph EN      | PE1  |
| 5V Periph FLT     | PE2  |
| 5V Tele EN        | PD29 |
| 5V Tele FLT       | PB5  |

### USB
- USB D+ : HSDP (pin 137)
- USB D- : HSDM (pin 136)
- Type-C connector (J16)

## Hardware Notes
- ERASE pin (PB12) must be tied LOW externally (10K pull-down recommended)
- TST (pin 85) and JTAGSEL (pin 104) have internal pull-downs — floating OK
- NRST has 10K pull-up via R3, with 1K series resistor (R4) for isolation
- VDDOUT must read 1.2V — if not, reflow the MCU
- Board requires external power (USB-C or battery via power brick connector J17)
