#pragma once
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <HTTPClient.h>
#include "config.h"
#include "speaker.h"

// Scan state (read by web UI)
static bool   scanActive = false;
static String scanMsg    = "";
static bool   scanRequested = false;  // async trigger from web API

static bool hasSpeakerIP(const String& ip) {
  for (auto& s : speakers) if (s.ip == ip) return true;
  return false;
}

static String fetchRoomName(const String& ip) {
  HTTPClient http;
  http.setTimeout(2000);
  http.begin("http://" + ip + ":1400/xml/device_description.xml");
  String name;
  if (http.GET() == HTTP_CODE_OK) {
    String xml = http.getString();
    name = SonosController::tag(xml, "roomName");
    String sz = SonosController::tag(xml, "internalSpeakerSize");
    if (sz.length() > 0 && sz.toInt() < 0) name = "";
  }
  http.end();
  return name;
}

static void addSpeaker(const String& ip, const String& fallbackName = "") {
  if (hasSpeakerIP(ip)) return;
  scanMsg = "getting name for " + ip;
  String name = fetchRoomName(ip);
  if (name.length() == 0) name = fallbackName;
  if (name.length() == 0) return;

  speakers.push_back({ip, name});
  dbg("found: %s @ %s", name.c_str(), ip.c_str());
}

static bool discoverViaMdns() {
  scanMsg = "mDNS query...";
  int n = MDNS.queryService("sonos", "tcp");
  if (n <= 0) return false;

  dbg("mDNS: %d service(s)", n);
  for (int i = 0; i < n; i++)
    addSpeaker(MDNS.address(i).toString(), MDNS.hostname(i));

  return !speakers.empty();
}

static bool discoverViaSsdp() {
  scanMsg = "SSDP broadcast...";
  NetworkUDP udp;
  if (!udp.begin(1901)) return false;

  static const char msearch[] =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 2\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n\r\n";

  // Broadcast + multicast for W5500 reliability
  for (auto& addr : {IPAddress(255,255,255,255), IPAddress(239,255,255,250)}) {
    udp.beginPacket(addr, 1900);
    udp.write((const uint8_t*)msearch, strlen(msearch));
    udp.endPacket();
  }

  unsigned long t = millis();
  while (millis() - t < 3000) {
    if (udp.parsePacket() > 0) {
      char buf[512];
      int len = udp.read((uint8_t*)buf, sizeof(buf) - 1);
      buf[len] = '\0';
      if (strstr(buf, "ZonePlayer"))
        addSpeaker(udp.remoteIP().toString());
    }
    delay(10);
  }
  udp.stop();
  return !speakers.empty();
}

static bool discoverSpeakers() {
  speakers.clear();
  scanActive = true;
  scanMsg = "starting...";

  bool found = discoverViaMdns();

  // Only run SSDP if mDNS found nothing
  if (!found) found = discoverViaSsdp();

  scanActive = false;
  scanMsg = String(speakers.size()) + " speaker(s) found";
  dbg("discovery: %d speakers", speakers.size());
  return found;
}
