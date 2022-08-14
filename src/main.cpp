#include <Arduino.h>
/*
 * ENC26J60 pins wired as follows:
 *
 * Enc28j60 SO  -> Arduino pin 12
 * Enc28j60 SI  -> Arduino pin 11
 * Enc28j60 SCK -> Arduino pin 13
 * Enc28j60 CS  -> Arduino pin 10
 * Enc28j60 VCC -> Arduino 5V pin (3V3 pin didn't work for me)
 * Enc28j60 GND -> Arduino Gnd pin
 *
 */

#include <DmxSimple.h>
#include <EEPROM.h>
#include <EtherCard.h>

// enter desired universe and subnet  (sACN first universe is 1)
#define DMX_SUBNET 0
#define DMX_UNIVERSE 1  //**Start** universe

static byte mymac[] = {0x01, 0x00, 0x00, 0x45, 0x08, 0x02};
const char hostname[] = "sACN-Node-01";

uint16_t universe = 1;
// 1 - 32767

#define ETHERNET_BUFFER 700
#define CHANNEL_COUNT 512  // because it divides by 3 nicely
#define UNIVERSE_COUNT 1

byte Ethernet::buffer[ETHERNET_BUFFER];  // tcp/ip send and receive buffer
BufferFiller bfill;

uint8_t packetOffset = 42;

void sacnDMXReceived(const byte* pbuff, int count) {
  Serial.println("sacnDMXReceived");
  if (count > CHANNEL_COUNT) count = CHANNEL_COUNT;
  byte b = pbuff[113 + packetOffset];  // DMX Subnet
  if (b == DMX_SUBNET) {
    b = pbuff[114 + packetOffset];  // DMX Universe
    if (b >= DMX_UNIVERSE && b <= DMX_UNIVERSE + UNIVERSE_COUNT) {
      for (uint16_t i = 0; i < 512; i++) {
        DmxSimple.write(i, pbuff[126 + packetOffset + i]);
      }
    }
  }
}

int checkACNHeaders(const byte* messagein, int messagelength) {
  Serial.println("checkACNHeaders");
  Serial.print("msg Length=");
  Serial.println(messagelength);

  if (messagein[1 + packetOffset] == 0x10 && messagein[4 + packetOffset] == 0x41 &&
      messagein[12 + packetOffset] == 0x37) {
    // number of values plus start code
    Serial.println("Header Check passed");

    int addresscount =
        (byte)messagein[123 + packetOffset] * 256 + (byte)messagein[124 + packetOffset];
    return addresscount - 1;  // Return how many values are in the packet.
  }
  Serial.println("Header Check failed");

  return 0;
}

static void sACNPacket(word port, byte ip[4], const char* data, word len) {
  Serial.println("Udp packet recieved");

  // Make sure the packet is an E1.31 packet
  int count = checkACNHeaders(Ethernet::buffer, len);
  Serial.println(count);
  if (count) {
    // It is so process the data to the LEDS
    sacnDMXReceived(Ethernet::buffer, count);
  }
}

void setup() {
  Serial.begin(57600);        // for testing
  DmxSimple.usePin(3);        // DMX output is pin 3
  DmxSimple.maxChannel(512);  // should be 512

  Serial.println("");
  Serial.println("");
  Serial.println("start");

  ether.begin(sizeof(Ethernet::buffer), mymac, 10);

  Serial.println(F("Setting up DHCP"));
  if (!ether.dhcpSetup(hostname, true)) Serial.println(F("DHCP failed"));

  EEPROM.get(0, universe);

  ether.printIp("My IP: ", ether.myip);
  ether.printIp("Netmask: ", ether.netmask);
  ether.printIp("GW IP: ", ether.gwip);
  ether.printIp("DNS IP: ", ether.dnsip);

  Serial.print("Universe: ");
  Serial.println(universe);
  Serial.println("");

  // Register listener
  ether.udpServerListenOnPort(&sACNPacket, 5568);
}

const char http_OK[] PROGMEM =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Pragma: no-cache\r\n\r\n";

const char http_redirect[] PROGMEM =
    "HTTP/1.0 303 Redirect\r\n"
    "Location: /\r\n"
    "Content-Type: text/html\r\n\r\n"
    "<h1>303 Redirect</h1>";

const char http_404[] PROGMEM =
    "HTTP/1.0 404 Not found\r\n"
    "Content-Type: text/html\r\n\r\n"
    "<h1>404 Not found</h1>";

void homePage() {
  bfill.emit_p(PSTR("$F"
                    "<form action='/'>"
                    /* "<label for='hname'>Hostname (prefix):</label><br>"
                    "<input type='text' id='hname' name='hname' value='$F'><br>"

                    "<label for='id'>ID (hostname Sufix)</label><br>"
                    "<input type='number' id='id' name='id' value='$F'><br>" */

                    "<label for='uni'>Universe</label>"
                    "<input type='number' min='1' max='32767' id='uni' "
                    "name='uni' value='$D'>"

                    "<input type='submit' value='Submit'>"
                    "</form>"),
               http_OK, universe  //, "hostname"//, mymac[5], universe

  );
}

void loop() {
  // Process packets
  ether.packetLoop(ether.packetReceive());

  word len = ether.packetReceive();
  word pos = ether.packetLoop(len);

  if (pos) {
    delay(1);  // necessary for my system
    bfill = ether.tcpOffset();
    char* data = (char*)Ethernet::buffer + pos;

    if (strncmp("GET /", data, 5) != 0) {
      // Unsupported HTTP request
      // 304 or 501 response would be more appropriate
      bfill.emit_p(http_404);
    } else {
      data += 5;

      if (data[0] == ' ') {
        // Return home page
        homePage();
      } else if (data[0] == '?') {
        data += 1;

        if (strncmp("uni=", data, 4) == 0) {
          data += 4;

          char buf[6];
          uint8_t i = 0;
          for (i = 0; i < 6; i++) {
            if (data[i] == ' ') {
              break;
            }
            buf[i] = data[i];
          }
          buf[i] = '\0';

          Serial.println(buf);

          universe = atoi(buf);
          Serial.print("universe: ");
          Serial.println(universe);

          EEPROM.put(0, universe);  // save universe
        }
        bfill.emit_p(http_redirect);
      } else if (data[0] == 'f') {
        // favicon
        bfill.emit_p(http_OK);
      } else {
        // Page not found
        bfill.emit_p(http_404);
      }
    }

    ether.httpServerReply(bfill.position());  // send http response
  }
}