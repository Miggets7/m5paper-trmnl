# M5Paper TRMNL Firmware

Arduino firmware for using the M5Paper e-ink device as a TRMNL display.

## Features
- Full M5Unified library support (M5EPD deprecated)
- PNG and BMP image support
- Deep sleep power management with RTC persistence
- Battery voltage monitoring
- Automatic WiFi retry logic
- Wake on button press or timer

## Hardware Requirements
- M5Paper e-ink display (960x540)
- WiFi network access

## Installation

### PlatformIO
1. Clone this repository
2. Update WiFi credentials in `m5paper_trmnl.ino`
3. Set your TRMNL API key
4. Upload to M5Paper

### Arduino IDE
- Install M5Unified library
- Install ArduinoJson library
- Configure ESP32 board settings

## Configuration
Edit these values in the code:
- `TRMNL_API_KEY` - Your TRMNL API key from trmnl.app
- `WIFI_SSID` - Your WiFi network name
- `WIFI_PASS` - Your WiFi password

## Usage
The device will:
1. Wake up (timer or button press)
2. Connect to WiFi
3. Fetch display from TRMNL API
4. Render image to e-ink screen
5. Enter deep sleep until next refresh

## Troubleshooting
- **WiFi won't connect**: Check credentials, device will auto-retry 3x
- **Image won't load**: Verify TRMNL API key and network connectivity
- **Battery drain**: Normal refresh rate is 15 minutes (900s)

## Credits
Built for the [TRMNL BYOD program](https://docs.trmnl.com/go/diy/byod)

## License
MIT