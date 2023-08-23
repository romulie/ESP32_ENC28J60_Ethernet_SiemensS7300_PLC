/*----------------------------------------------------------------------
 !!!WORKING USING ECN28J60 ETHERNET module with ESP32
 (make shure to use latest version of ESP32 Board version for Arduino IDE, this sketch works fine with ESP32 Espressif pack ver.2.0.11)
 !!! Asynchronously receives UDP packets on port 3000
 !!! Connects to the PLC 
 !! Writes to PLC DB
 !! Reads from PLC DB

 Created 12 Dec 2016
 Modified 10 Mar 2019 for Settimino 2.0.0 by Davide Nardella
 Modified 10 Aug 2023 by romulie
----------------------------------------------------------------------*/

// Configure ENC28J60 module connected to HSPI of ESP32
#include <ESP32-ENC28J60.h>      // special ENC28J60 library for ESP32
#define SPI_HOST       1
#define SPI_CLOCK_MHZ  8
#define INT_GPIO       4
#define MISO_GPIO 19 
#define MOSI_GPIO 23
#define SCLK_GPIO 18 // !!! connect to SCK NOT CLK on ECN28j60
#define CS_GPIO   5
// WORKING. !!!Mind interconnection wires length (works with 20cm long ones, but some people complain about wires longer than 10cm)

// Asyncronous UDP-server and port to listen to
#include <AsyncUDP.h>
#define UDP_PORT 3000 // UDP-port of ISD-5 sending measurement packets
#define LED_PIN 13
AsyncUDP udp;

// Use these to assign static IP for WiFi or ETHERNET
IPAddress local_IP(192, 168, 1, 108); // Desired Static IP Address
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1); // Not Mandatory
IPAddress secondaryDNS(0, 0, 0, 0);   // Not Mandatory

volatile bool got_UDP_packet = false;
static bool eth_connected = false;

#define ISD5_HEADER_LEN 15 // 16??
#define ISD5_PACKET_LEN 32 // 2 + 32 = 34 byte??

volatile static struct ISD5_packet_t { // data packet sent from ISD-5 Sensor ????
  //char header[ISD5_HEADER_LEN]; //?? "START_OF_FRAME\0" ?? 
  uint32_t time = 0x00000000;   // 4 byte ???float32_t&???
  uint32_t speed = 0x00000000;  // 4 byte
  uint32_t length; // 4 byte
  uint32_t snr1;   // 4 byte
  uint32_t snr2;   // 4 byte
  uint32_t accX;   // 4 byte not used in ISD-5
  uint32_t accY;   // 4 byte not used in ISD-5
  uint32_t accZ;   // 4 byte not used in ISD-5
} ISD5_packet;

void WiFiEvent(WiFiEvent_t event){
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname("esp32-ethernet");//set eth hostname here
      /// SET STATIC ETHERNET IP. Comment following 3 lines to use dynamic ETHERNET IP-address
      if (!ETH.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)){
          Serial.println("STATIC ETHERNET IP Configuration Failed!");
      }
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      /// ??? OR SET STATIC IP HERE ???
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

//Siemens S7 PLC communication library
#include "Settimino_lib/Platform.h" // Uncomment your dev.board configuration in the file Platform.h
#include "Settimino_lib/Settimino.cpp"
IPAddress PLC(192,168,1,222);       // PLC Address. When using S7 simulation on PC enter the PC-IPAddress as PLC-IPAddress
#define DBNum 21                    //21 for default demo Snap7 server, or = 200 - DataBase number to write/read useful data
S7Client Client;

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);     // configure LED to indicate UDP-packet receit
  digitalWrite(LED_PIN, LOW);

  WiFi.onEvent( WiFiEvent );
  ETH.begin( MISO_GPIO, MOSI_GPIO, SCLK_GPIO, CS_GPIO, INT_GPIO, SPI_CLOCK_MHZ, SPI_HOST ); // start Ethernet on ENC28J60

  while( !eth_connected) {
    Serial.println("Connecting...");
    delay( 1000 );
  }
  
  WiFi.disconnect(true);  // no need?
  WiFi.mode(WIFI_OFF);    // no need?

  /// UDP packets are recieved here asyncronously
  if(udp.listen(UDP_PORT)) {
        Serial.print("UDP Listening on IP: ");
        Serial.println(ETH.localIP());
        udp.onPacket([&ISD5_packet](AsyncUDPPacket packet) {
            Serial.print("UDP Packet Type: "); Serial.print(packet.isBroadcast()?"Broadcast":packet.isMulticast()?"Multicast":"Unicast");
            Serial.print(", From: "); Serial.print(packet.remoteIP()); Serial.print(":"); Serial.print(packet.remotePort());
            Serial.print(", To: "); Serial.print(packet.localIP()); Serial.print(":"); Serial.print(packet.localPort());
            Serial.print(", Length: "); Serial.print(packet.length()); Serial.print(", Data: ");
            Serial.write(packet.data(), packet.length()); Serial.println();
            //if (packet.length() == ISD5_PACKET_LEN){
              //memcpy(&ISD5_packet, &packet, packet.length());
              ++ISD5_packet.time;
              got_UDP_packet = true;
              digitalWrite(LED_PIN, HIGH);
              Serial.print("ISD5_packet.time = "); Serial.println(ISD5_packet.time);
              Serial.print("ISD5_packet.speed = "); Serial.println(ISD5_packet.speed);
            //} 
        });
    } /// end UDP
}

bool Connect() { // Connects to the PLC
    int Result=Client.ConnectTo(PLC, 0, 2);  // IP, Rack = 0 (for S7 300, see the doc.), Slot (for S7 300, see the doc.)
    Serial.print("Connecting to ");Serial.println(PLC);  
    if (Result==0) {
      Serial.print("Connected ! PDU Length = ");Serial.println(Client.GetPDULength());
    }
    else
      Serial.println("Connection error");
    return Result==0;
}

void CheckError(int ErrNo) { // Prints the Error number and disconnects if severe error
  Serial.print("Error No. 0x");
  Serial.println(ErrNo, HEX);
  if (ErrNo & 0x00FF){
    Serial.println("SEVERE ERROR, disconnecting.");
    Client.Disconnect(); 
  }
}

void loop() {
  
  int Result, Status;
  
  while (!Client.Connected){ // Waiting for the connection to PLC
    if (!Connect())
      delay(500);
  }
  
  if (got_UDP_packet){
    Result = Client.WriteArea(S7AreaDB, DBNum, 0, 3*4, (void*)&ISD5_packet);  //We are requesting DB access, DB Number, Start from byte N.0, We need "Size" bytes ?? or Words??
    if (Result == 0){
      Serial.println("++++++++Data write to S7 success!");
      got_UDP_packet = false;
      digitalWrite(LED_PIN, LOW);
    }
    else {
      Serial.println("--------Data write to S7 failure!");
      got_UDP_packet = false;
    }
    uint32_t tmp_time = 0;
    Result = Client.ReadArea(S7AreaDB, DBNum, 0, 1*4, (void*)&tmp_time);
    if (Result == 0){
      Serial.println("++++++++Data read from S7 success!");
      Serial.print("read from S7 time = "); Serial.println(tmp_time);
    }
    else {
      Serial.println("--------Data read from S7 failure!");
    }
  }
/*
  Result = Client.GetPlcStatus(&Status);
  if (Result==0){
    if (Status==S7CpuStatusRun){
      //Serial.println("STOPPING THE CPU"); Client.PlcStop(); // just for testing 
    }
    else{
      //Serial.println("STARTING THE CPU"); Client.PlcStart();  // just for testing 
    }
  }
  else
    CheckError(Result);
    
  //udp.broadcast("Anyone here?"); ///Send broadcast. COMMENT
 */ 
  delay(10); 
}