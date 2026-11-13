# Apollo TAB-1

TAB-1 is a Home Assistant touch tablet from Apollo Automation, running
ESPHome firmware.

- ESP32-P4. Its Wi-Fi comes from an ESP32-C6 on the same board, which the P4
  talks to over SDIO.
- A 10.1-inch MIPI-DSI touch panel, 800x1280, shown landscape at 1280x800.

The firmware has a board file for the Guition JC8012P4A1 (the default) and
one for the Waveshare ESP32-P4-NANO with its 10.1-inch DSI TOUCH-A panel
(`Integrations/ESPHome/boards/`). Pick one in the `packages:` block of the
file you flash.

## Firmware

The files are in `Integrations/ESPHome`.

| File | What it is |
|---|---|
| `TAB-1-engine.yaml` | The dashboard engine, for tablets in use. It shows the dashboards you make in the [Apollo TAB Dashboard](https://github.com/ApolloAutomation/TAB-Dashboard) integration in Home Assistant. Home Assistant sends each dashboard while the tablet runs, so a new or changed dashboard needs no reflash. New releases are published to GitHub Pages, and the tablet installs one when you use its "Firmware Update" in Home Assistant. |
| `TAB-1-engine-bench.yaml` | The same engine with a fixed key from `secrets.yaml`, for bench work. |
| `TAB-1.yaml` | Earlier firmware: 12 tiles you set up on the tablet. |
| `TAB-1-showcase.yaml` | Earlier firmware: a swipeable demo with simulated data. |

All of them use the same device name, so Wi-Fi settings and the Home
Assistant device carry over when you switch.

[`Integrations/ESPHome/docs/engine.md`](Integrations/ESPHome/docs/engine.md)
explains the engine: set-up, updates, erasing a tablet, switching firmware,
and the checks.

## Set up a new tablet

1. Flash `TAB-1-engine.yaml` over USB (or install it from ESPHome Web).
2. Within 15 minutes of power-on, join the open Wi-Fi "Apollo TAB1 Hotspot"
   from a phone and pick the home's Wi-Fi on the page that opens.
3. Add the tablet in Home Assistant when it shows up in Settings > Devices &
   services. Home Assistant gives it its own key on that first connection.
4. In TAB-1 in Home Assistant's sidebar (the Apollo TAB Dashboard
   integration), an admin turns the tablet on once with its dashboard.

## More documents

- [`Integrations/ESPHome/engine_tests/contract/CONTRACT.md`](Integrations/ESPHome/engine_tests/contract/CONTRACT.md):
  the protocol between the tablet and Home Assistant.
- [`docs/runtime-dashboards-design.md`](docs/runtime-dashboards-design.md):
  the design proposal written before the engine, kept for history.

## Links

- Discord (support, feedback, discussion, future products): [dsc.gg/ApolloAutomation](https://dsc.gg/ApolloAutomation)
- Shop: [apolloautomation.com](https://apolloautomation.com)
- Wiki: [wiki.apolloautomation.com](https://wiki.apolloautomation.com/)

## Licence

Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International. See
[License.md](License.md). The Montserrat font is under the SIL Open Font
License 1.1
([`Integrations/ESPHome/fonts/Montserrat-OFL.txt`](Integrations/ESPHome/fonts/Montserrat-OFL.txt)).
