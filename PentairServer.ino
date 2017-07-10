// JSP
// Blue wire in A (btm/left), Green in B (top/right)
#include <SoftwareSerial.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>

boolean debug=false;

// RS-485 read stuff
uint8_t buffer[256];
uint8_t * bPointer;
uint8_t bufferOfBytes[256];
uint8_t * bPointerOfBytes;
#define header1 1
#define header3 2
#define header4 3
#define bufferData 4
#define calcCheckSum 5
#define saltHead2 6
#define saltTerm 7
#define bufferSaltData 8
volatile int pumpWatts = 0;
int oldPumpWatts = 0;
volatile int pumpRPM = 0;
int oldPumpRPM = 0;
int goToCase = header1;
int byteNum = 0;
int remainingBytes = 0;
int bytesOfDataToGet = 0;
int chkSumBits = 0;

volatile byte poolTemp = 0;
byte oldPoolTemp = 0;
volatile byte airTemp = 0;
int sumOfBytesInChkSum = 0;
int chkSumValue = 0;

int saltBytes1 = 0;
int saltBytes2 = 0;
boolean salt = false;
byte oldSaltPct = 0;
volatile byte saltPct = 0;
unsigned long saltOutputToggle = 0;
uint8_t saltPctQuery[] =
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xA5, 0x07, 0x10, 0x20, 0xD9, 0x01, 0x00, 0x01, 0xB6
}; // triggers salt percent setting
int salinityNow = 0;

#define RS485Transmit    HIGH
#define RS485Receive     LOW
#define SSerialTxControl D1  //RS485 Direction control
#define rxPin D2
#define txPin D3
SoftwareSerial RS485Serial(rxPin, txPin);  //RX, TX
WiFiServer webserver(80);
WiFiManager wifiManager;
const char* MDNSName = "PentairServer";

void setup()
{
	Serial.begin(115200);  //115200
  wifiManager.autoConnect("POOL-SERVER", "pool");
  RegisterMDNS();
  
  // Start TCP (HTTP) server
  webserver.begin();
  MDNS.addService("http", "tcp", 80);

  SetupRS485();
  Serial.println("Waiting...");
}

void loop()
{ 
  while (RS485Serial.available())
        ProcessData((uint8_t)RS485Serial.read() );
  
  RespondtoHTTPClients();
}

void processFrameData(uint8_t * buffer, byte len){
  if (sumOfBytesInChkSum == chkSumValue)
  {           
    if (bufferOfBytes[5] == 0x1D)
    {
      // 29 byte message is for broadcast display updates
      oldPoolTemp = poolTemp;
      poolTemp = bufferOfBytes[20];
      airTemp = bufferOfBytes[24];
      Serial.print(F("Water Temp............. "));
      Serial.println(poolTemp);
      Serial.print(F("Air Temp............... "));
      Serial.println(airTemp);
    }
    if (bufferOfBytes[2] == 0x10 && bufferOfBytes[3] == 0x60 && bufferOfBytes[5] == 0xF) 
    { // 15 byte message is for pump updates
      oldPumpWatts = pumpWatts;
      oldPumpRPM = pumpRPM;
      Serial.print(F("Pump Watts............. "));
      if (buffer[9] > 0) 
      {
        pumpWatts = ((bufferOfBytes[9] * 256) + bufferOfBytes[10]);//high bit
        Serial.println(pumpWatts);
        Serial.print(F("Pump RPM............... "));
        pumpRPM = ((bufferOfBytes[11] * 256) + bufferOfBytes[12]);
        Serial.println(pumpRPM);
      } 
    }
  }
}

void RespondtoHTTPClients()
{
    // Check if a client has connected
  WiFiClient client = webserver.available();
  
  if (client) {
    // bolean to locate when the http request ends
    boolean blank_line = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        
        if (c == '\n' && blank_line) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println("Connection: close");
            client.println();
            // your actual web page that displays temperature
            client.println("<!DOCTYPE HTML>");
            client.println("<html>");
            client.println("<head></head><body><h1>Pool</h1><h3>Pool Temp: ");
            client.println(poolTemp);
            client.println("*C</h3><h3>Air Temp: ");
            client.println(airTemp);
            client.println("*F</h3>");  
            client.println("<h3>Pump Watts: ");
            client.println(pumpWatts);
            client.println("</h3><h3>Pump RPM: ");
            client.println(pumpRPM);
            client.println("</h3></body></html>");  
            break;
        }
        if (c == '\n') {
          // when starts reading a new line
          blank_line = true;
        }
        else if (c != '\r') {
          // when finds a character on the current line
          blank_line = false;
        }
      }
    }  
    // closing the client connection
    delay(1);
    client.stop();
  }
}


void clear485Bus()
{
  memset(buffer, 0, sizeof(buffer));
  bPointer = buffer;
  memset(bufferOfBytes, 0, sizeof(bufferOfBytes));
  bPointerOfBytes = bufferOfBytes;
  byteNum = 0;
  bytesOfDataToGet = 0;
  remainingBytes = 0;
  chkSumBits = 0;
  sumOfBytesInChkSum = 0;
  chkSumValue = 0;
  goToCase = header1;
}


void RegisterMDNS ()
{
  if (!MDNS.begin(MDNSName)) {
    while(1) { 
      delay(1000);
    }
  }
}

void ProcessData(uint8_t c)
{
    if (debug) Serial.print(c, HEX);
    switch (goToCase)
    {
      case header1:
        if (c == 0xFF)   //0xFFFFFFFF// ignoring leading FF so do nothing, repeat again
        {
          * bPointer++= (char) c;
          byteNum = 1;
          break;
        }
        else
          if (c == 0x0)
          {
            // is this a 0 in byte 2?  could be Pentair packet
            goToCase = header3;
            * bPointer++= (char) c;
            byteNum++;
            break;
          }
        else
        {
          // if (c == 0x10) // is this an IntelliChlor header?  could be an IntelliChlor packet
          yield();
          goToCase = saltHead2;
          * bPointer++= (char) c;
          byteNum = 1;
          break;
        }
        break;
      case header3:
        yield();
        * bPointer++= (char) c;
        if (c == 0xFF) //0xFFFFFFFF
        {
          // it's not really the start of a frame, must be deeper into a Pentair packet
          goToCase = header4;
          byteNum++;
          break;
        }
        else
        {
          clear485Bus();
          goToCase = header1;
          break;
        }
        break;
      case header4:
        yield();
        if (c == 0xA5)  //0xFFFFFFA5
        {
          // it's not really the start of a frame, almost have a Pentair preamble match
          goToCase = bufferData;
          sumOfBytesInChkSum += (byte) c, HEX;
          yield();
          * bPointerOfBytes++= (char) c;
          * bPointer++= (char) c;
          byteNum++;
          break;
        }
        else
        {
          clear485Bus();
          goToCase = header1;
          break;
        }
        break;
      case bufferData:
        yield();
        * bPointer++= (char) c; // loop until byte 9 is seen
        * bPointerOfBytes++= (char) c; // add up in the checksum bytes
        byteNum++;
        sumOfBytesInChkSum += (byte) c, HEX;
        if (1 != 2)
        {
          // janky code here... whatever.  you clean it up mr. awesome
          if (byteNum == 9)
          {
            // get data payload length of bytes
            bytesOfDataToGet = (c);
//              Serial.println(F("NEW RS-485 FRAMES RECEIVED"));
//              Serial.print(F("Payload bytes to get... "));
//              Serial.println(bytesOfDataToGet);
            
            if (bytesOfDataToGet < 0 || bytesOfDataToGet > 47)
            {
              // uh oh.....buffer underflow or buffer overflow... Time to GTFO
              clear485Bus();
              break;
            }
            if (remainingBytes == bytesOfDataToGet)
            {
              goToCase = calcCheckSum;
              break;
            }
            remainingBytes++;
            break;
          }
          if (byteNum >= 10)
          {
            if (remainingBytes == bytesOfDataToGet)
            {
              goToCase = calcCheckSum;
              break;
            }
            remainingBytes++;
            break;
          }
          break;
        }
        break;
      case calcCheckSum:
        yield();
        if (chkSumBits < 2)
        {
          * bPointer++= (char) c;
          if (chkSumBits == 0)
          {
            
//            Serial.print(F("Checksum high byte..... "));
//            Serial.println(c, HEX);
            chkSumValue = (c * 256);
          }
          else
            if (chkSumBits == 1)
            {
//              Serial.print(F("Checksum low byte...... "));
//              Serial.println((byte) c, HEX);
              goToCase = header1;
              byte len = (byte) (bPointer - buffer);
              chkSumValue += (byte) c;
              processFrameData(buffer, len);
              clear485Bus();
              break;
            }
          chkSumBits++;
          break;
        }
        break;
      case saltHead2:
        yield();
        if (c == 0x02)
        {
          // is this Intellichlor STX header frame 2 ?
          goToCase = bufferSaltData;
          * bPointer++= (char) c;
          byteNum++;
          break;
        }
        else
        {
          clear485Bus();
          goToCase = header1;
          break;
        }
        break;
      case bufferSaltData:
        yield();
        if (c != 0x10)
        {
          * bPointer++= (char) c; // loop until byte value 0x10 is seen
          byteNum++;
          break;
        }
        else
        {
          // a ha! found a 0x10, we're close
          goToCase = saltTerm;
          * bPointer++= (char) c;
          byteNum++;
          break;
        }
        break;
      case saltTerm:
        yield();
        * bPointer++= (char) c;
        byteNum++;
        goToCase = header1;
        if (c != 0x03)
        {
          clear485Bus();
          break;
        }
        else
        {
          // found an ETX 0x3.  See what we've got
          byte len = (byte) (bPointer - buffer);
//          Serial.println(F("NEW RS-485 IntelliChlor FRAMES RECEIVED"));
          if (len == 8)
          {
            saltBytes1 = (buffer[2] + buffer[3] + buffer[4] + 18);
//            Serial.print(F("Short salt byte sum +18. "));
//            Serial.println(saltBytes1);
//            Serial.print(F("Short Salt checksum is.. "));
//            Serial.println(buffer[5]);
            if (saltBytes1 == buffer[5])
            {
              salt = true;
              oldSaltPct = saltPct;
              saltPct = buffer[4];
//              Serial.println(F("Checksum is............. GOOD"));
//              Serial.print(F("Chlorinator Load........ "));
//              Serial.print(saltPct);
//              Serial.println(F("%"));
//              Serial.println();
              processFrameData(buffer, len);
              // if (oldSaltPct != saltPct) { //disabled b/c when idle the output doesn't toggle, but provides interval updates anyway for other vars
              // saltSetpointTrigger = true;
              saltOutputToggle++;
              // }
            }
            else
            {
//              Serial.println(F("Checksum is............. INVALID"));
              salt = true;
              processFrameData(buffer, len);
            }
            clear485Bus();
            break;
          }
          else
          {
            saltBytes2 = (buffer[2] + buffer[3] + buffer[4] + buffer[5] + 18);
//            Serial.print(F("Long salt byte sum +18.. "));
//            Serial.println(saltBytes2);
//            Serial.print(F("Long Salt checksum is... "));
//            Serial.println(buffer[6]);
            if (saltBytes2 == buffer[6])
            {
              salt = true;
//              Serial.println(F("Checksum is............. GOOD"));
              processFrameData(buffer, len);
              // someVal = buffer[6];
              digitalWrite(SSerialTxControl, RS485Transmit);
              for (byte count = 0; count < 2; count++)
              {
                // whenever the long salt string checksum checks out, query the chlorinator setpoint
                for (byte i = 0; i < sizeof(saltPctQuery); i++)
                {
                  RS485Serial.write(saltPctQuery[i]);
                  // Serial.print((byte)saltPctQuery[i], HEX);
                }
              }
              salinityNow = (buffer[4] * 50);
            }
            else
            {
//              Serial.println(F("Checksum is............. INVALID"));
              salt = true;
              processFrameData(buffer, len);
            }
            clear485Bus();
            break;
          }
          break;
        }
        clear485Bus();
        break;
    }
    // end switch( goToCase )  
}

void SetupRS485 ()
{
  pinMode(rxPin, INPUT);
  pinMode(txPin, OUTPUT);
  pinMode(SSerialTxControl, OUTPUT); 
  digitalWrite(SSerialTxControl, RS485Receive); // to receive from rs485
  clear485Bus();
  RS485Serial.begin(9600);  
}











