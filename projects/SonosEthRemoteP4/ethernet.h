#pragma once
#include <ETH.h>
#include "config.h"

static volatile bool ethLinked = false;
static volatile bool ethReady  = false;

static void onEthEvent(arduino_event_id_t ev) {
  switch (ev) {
    case ARDUINO_EVENT_ETH_CONNECTED:    ethLinked = true; break;
    case ARDUINO_EVENT_ETH_GOT_IP:       ethReady = true; break;
    case ARDUINO_EVENT_ETH_DISCONNECTED: ethLinked = ethReady = false; break;
    default: break;
  }
}

inline bool initEthernet(const char* hostname) {
  Network.onEvent(onEthEvent);

  // ESP32-P4 native EMAC + IP101GRI RMII PHY (alias of TLK110).
  // EMAC_CLK_EXT_IN = the PHY's own 50MHz crystal feeds RMII clock.
  // Pins per M5 Unit PoE-P4 schematic; matches official ETH_TLK110 example.
  if (!ETH.begin(ETH_PHY_IP101, PHY_ADDR, PHY_MDC, PHY_MDIO, PHY_POWER,
                 (eth_clock_mode_t)EMAC_CLK_EXT_IN))
    return false;
  ETH.setHostname(hostname);

  auto wait = [](volatile bool& flag, unsigned long ms) {
    unsigned long t = millis();
    while (!flag && (millis() - t) < ms) delay(50);
    return (bool)flag;
  };

  return wait(ethLinked, T_ETH_LINK) && wait(ethReady, T_DHCP);
}
