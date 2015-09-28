/*
  Web service client

 This sketch connects to a web service to send messages indicating andon control inputs
 v2.0.0 - add controls for lights for devices equipped with relays
 v1.8.0 - add queries for control status.  Work to improve garbage in telnet messages with string.trim().  use F() to reduce string cost in RAM
 v1.7.4 - corrected :STATUS? telnet response
 v1.7.3 - dead branch
 v1.7.2 - add handling for DHCP lease maintenance (Ethernet.maintain() in loop()).  Changed to solid red led when red button pressed. Fixed control state sent from blue switch  - on/off, not 1/0
 v1.7.1 - added printlines to telnet connection.  populated command list on question mark input
 v1.7 - 20150407 LR - make controller a server to accept connections over ethernet
 v1.6.2 - 20150407 LR - Address very long timeout for bad connections to server
 v1.6.1 - 20150407 LR - Change red button function to move debounce logic to server side
 v1.6 - 20150406 LR - Updated to capture andon switch changes and call new webservice
 v1.5 - 20150207 LR - Explicit SPI controls
 v1.4 branch closed
 v1.3 - 20150108 LR - Changed the loop for the on and off buttons back to original
 v1.2 - 20150108 LR - Corrected erroneous entry in off button detection that prevented off routine from running
 v1.1 - 20141103 LR - Updated off button hold time to 5000ms
 Circuit:
 this is intended to be run on a Arduino Ethernet or Arduino with Ethernet Shield,
 with 2 buttons (on and off), 4 switches, and 2 LEDs for status.  Refer to declarations for pin numbers
 * Ethernet shield attached to pins 10, 11, 12, 13

 created 08SEP2013
 by Lorne Roy
 derived in part from the example webClient sketch
 */

#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <utility/w5100.h>


//8-pin header
//pins 0 and 1 are reserved for serial
#define BLUE_SWITCH 2
#define GREEN_SWITCH 3
#define YELLOW_SWITCH 4
//#define SS_SD_CARD   4
#define RED_LED 5
#define GREEN_LED 6
#define RED_SWITCH 7

//10-pin header
#define RED_BUTTON 8
#define GREEN_BUTTON 9
#define SS_ETHERNET 10 //reserved for ethernet

//analog pins as digital
#define RED_LIGHT A0
#define BLUE_LIGHT A1
#define YELLOW_LIGHT A2
#define GREEN_LIGHT A3


const String versionID = "T03801 v2.0.0";
const char commandList[] PROGMEM = {":CLIENT? - This command returns the IP address for the andon controller\r\n"
":CONTROL:RED_LIGHT:ON/OFF/FLASH - This command sets the red light to on, off or flash based on the last argument\r\n"
":CONTROL:YELLOW_LIGHT:ON/OFF/FLASH - This command sets the yellow light to on, off or flash based on the last argument\r\n"
":CONTROL:BLUE_LIGHT:ON/OFF/FLASH - This command sets the blue light to on, off or flash based on the last argument\r\n"
":CONTROL:GREEN_LIGHT:ON/OFF/FLASH - This command sets the green light to on, off or flash based on the last argument\r\n"
":CONTROL:BUZZER:ON/OFF/FLASH - This command sets the buzzer to on, off or flash based on the last argument\r\n"
":MAC? - This command returns the MAC address for the andon controller\r\n"
":MAC:aa-bb-cc-dd-ee-ff - This command sets the MAC address for the andon controller to aa-bb-cc-dd-ee-ff\r\n"
":PORT? - This command returns the port number for the web service server\r\n"
":PORT:nn - This command sets the port number for the web service server to nn\r\n"
":SERVER? - This command returns the ip address for the web service server\r\n"
":SERVER:aaa.bbb.ccc.ddd - This command sets the ip address for the web service server to aaa.bbb.ccc.ddd\r\n"
":STATUS:SYSTEM? - This command returns the status of the andon controller; 0 = good, 1 = not connected, 2 = last request failed\r\n"
":STATUS:SYSTEM:n - This command sets the status of the andon controller to n\r\n"
":STATUS:CONTROL:BLUE_SWITCH? - This command returns the status of the control\r\n"
":STATUS:CONTROL:GREEN_SWITCH? - This command returns the status of the control\r\n"
":STATUS:CONTROL:RED_SWITCH? - This command returns the status of the control\r\n"
":STATUS:CONTROL:YELLOW_SWITCH? - This command returns the status of the control\r\n"
":STATUS:CONTROL:GREEN_BUTTON? - This command returns the status of the control\r\n"
":STATUS:CONTROL:RED_BUTTON? - This command returns the status of the control\r\n"
":VERSION? - This command returns the firmware version of the andon controller"};
/*
const char redButtonQuery[] PROGMEM = {":STATUS:CONTROL:RED_BUTTON?"}
const char greenButtonQuery[] PROGMEM = {":STATUS:CONTROL:GREEN_BUTTON?"}
const char blueSwitchQuery[] PROGMEM = {":STATUS:CONTROL:BLUE_SWITCH?"}
const char greenSwitchQuery[] PROGMEM = {":STATUS:CONTROL:GREEN_SWITCH?"}
const char yellowSwitchQuery[] PROGMEM = {":STATUS:CONTROL:YELLOW_SWITCH?"}
const char redSwitchQuery[] PROGMEM = {":STATUS:CONTROL:RED_SWITCH?"}

enum Status {
enum Status {
 good = 0,
 noConnection = 1,
 requestFailed = 2,
 maintainFailed = 3
 } systemStatus;
 */

//initialize the LED output
byte greenBright = 0;
byte redBright = 255;

//switch status bits used to track if a switch has changed states
boolean greenButtonStatus;
boolean redButtonStatus;
boolean greenSwitchStatus;
boolean blueSwitchStatus;
boolean yellowSwitchStatus;
boolean redSwitchStatus;

String controlState = "";

//system status
int systemStatus = 0;
//time index for status messages
int sti = 0;

//set the time a person must hold the off button in milliseconds
const long offDebounce = 3000;

//use this to determine if the MAC address has been written to EEPROM
boolean MAC_set = false;
boolean server_set = false;
boolean port_set = false;
boolean ethernetBegin = false;
//for tracking connections
boolean lastConnected = false;

//these variables are for the serial port input
String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

//MAC address will be written/read from EEPROM.  Don't want to hardcode into sketch given multiple instances on network
byte mac[6];

// Initialize the Ethernet client library
// with the IP address and port of the webServer
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;
IPAddress webServer(192, 168, 40, 11);
unsigned int port = 80;
//initialize the controller as a server on port 23 (telnet) to take incoming requests
EthernetServer thisServer(23);
EthernetClient telnetClient;

void setup()
{
  //set up the pins
  //explicitly setting up the SS pins for the ethernet and sd card
  //this was for troubleshooting faults in communicating with the server
  //pinMode(SS_SD_CARD, OUTPUT);
  //digitalWrite(SS_SD_CARD, HIGH);  // SD Card not active

  pinMode(SS_ETHERNET, OUTPUT);
  //digitalWrite(SS_ETHERNET, HIGH); // Ethernet not active
  digitalWrite(SS_ETHERNET, LOW);

  //buttons
  pinMode(GREEN_BUTTON, INPUT_PULLUP);
  pinMode(RED_BUTTON, INPUT_PULLUP);
  //leds
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(RED_LIGHT, OUTPUT);
  pinMode(GREEN_LIGHT, OUTPUT);
  pinMode(YELLOW_LIGHT, OUTPUT);
  pinMode(BLUE_LIGHT, OUTPUT);
  
  //switches
  pinMode(GREEN_SWITCH, INPUT_PULLUP);
  pinMode(BLUE_SWITCH, INPUT_PULLUP);
  pinMode(YELLOW_SWITCH, INPUT_PULLUP);
  pinMode(RED_SWITCH, INPUT_PULLUP);

  
  analogWrite(RED_LED, redBright);
  analogWrite(GREEN_LED, greenBright);

  //initialize switch status bits
  greenButtonStatus = digitalRead(GREEN_BUTTON);
  redButtonStatus = digitalRead(RED_BUTTON);

  greenSwitchStatus = digitalRead(GREEN_SWITCH);
  blueSwitchStatus = digitalRead(BLUE_SWITCH);
  yellowSwitchStatus = digitalRead(YELLOW_SWITCH);
  redSwitchStatus = digitalRead(RED_SWITCH);

  // Open serial communications
  Serial.begin(9600);

  /*determine if MAC address and server address set using the following
   EEPROM ADDRESS    EXPECTED VALUE
   0,7                0xAA      used to indicate that MAC EEPROM data is not random
   1-6                MAC address
   8,13                0xAA      used to indicate that MAC EEPROM data is not random
   9-12                Server IP address
   14,17                0xAA      used to indicate that MAC EEPROM data is not random
   15,16                Server IP port
   */
  MAC_set = EEPROM.read(0) == 0xAA & EEPROM.read(7) == 0xAA;
  server_set = EEPROM.read(8) == 0xAA & EEPROM.read(13) == 0xAA;
  port_set = EEPROM.read(14) == 0xAA & EEPROM.read(17) == 0xAA;
  if (MAC_set)
  {
    //read MAC address from EEPROM
    for (int i = 0; i < 6; i++)
    {
      //take care for the address offset
      mac[i] = EEPROM.read(i + 1);
    }
    Serial.println(F("MAC address: "));
    Serial.println(getMACAddress());
    //if successful, start the Ethernet connection:
    //if not, wait for MAC address to be set over serial, flash lights, indicating problem
    ethernetBegin = Ethernet.begin(mac);
    if (ethernetBegin == 0)
    {
      Serial.println(F("Failed to configure Ethernet using DHCP"));
    }
    else
    {
      // give the Ethernet shield a second to initialize:
      delay(1000);
      Serial.println(F("Client IP address:"));
      Serial.println(Ethernet.localIP());
      //these lines control the retries.  Without this the tries take > 30 s
      W5100.setRetransmissionTime(0x07D0);
      W5100.setRetransmissionCount(3);

      thisServer.begin();

    }
  }
  if (server_set)
  {
    int aaa = EEPROM.read(9);
    int bbb = EEPROM.read(10);
    int ccc = EEPROM.read(11);
    int ddd = EEPROM.read(12);

    // use the numeric IP instead of the name for the server:
    webServer = IPAddress(aaa, bbb, ccc, ddd); // numeric IP for server
  }

  Serial.println(F("Server IP address:"));
  Serial.println(webServer);

  if (port_set)
  {
    port = EEPROM.read(15) * 256 + EEPROM.read(16);
  }

  Serial.println(F("Server port number:"));
  Serial.println(port, DEC);

}

void loop()
{
  //set LEDs to give status
  flashStatus(systemStatus, sti, greenBright, redBright);
  analogWrite(GREEN_LED, greenBright);
  analogWrite(RED_LED, redBright);

  if (ethernetBegin != 0)
  {
    //maintain lease 
    if(Ethernet.maintain() % 2 == 1)
    {
      systemStatus = 3;
    }
    else
    {
      if(systemStatus == 3)
      {
        //reset systemStatus only if it was 3.  Don't stomp on other status values
        systemStatus=0;
      }
    }
    
    //check for client connections
    checkForTelnetClientMessage();

    //if green momentary button changes state
    if (digitalRead(GREEN_BUTTON) != greenButtonStatus)
    {
      greenButtonStatus = digitalRead(GREEN_BUTTON);
      if (greenButtonStatus == HIGH)
      {
        controlState = "released";
        Serial.print(F("green_button "));
        Serial.println(controlState);

        //send message to server and set system status based on response
        if (sendRequest("green_button", controlState))
        {
          systemStatus = 0;
        }
        else
        {
          systemStatus = 2;
        }
        delay(100);
        sti = 0;
      }
      else
      {
        long buttonPressed = millis();
        int seconds = 0;
        //send message on initial state
        controlState = "pressed";
        Serial.print(F("green_button "));
        Serial.println(controlState);

        //send message to server and set system status based on response
        if (sendRequest("green_button", controlState))
        {
          systemStatus = 0;
        }
        else
        {
          systemStatus = 2;
        }
        sti = 0;

        //while switch is held down, send messages each second
        while (digitalRead(GREEN_BUTTON) == LOW)
        {
          //Solid Green LED
          analogWrite(RED_LED, 0);
          analogWrite(GREEN_LED, 255);

          if ((millis() - buttonPressed) > (seconds + 1) * 1000)
          {
            seconds += 1;
            controlState = "held_" + String(seconds) + "_seconds";
            //send message to server and set system status based on response
            if (sendRequest("green_button", controlState))
            {
              systemStatus = 0;
            }
            else
            {
              systemStatus = 2;
            }
          }
        }

      }
    }
    //if red momentary button changes state
    else if (digitalRead(RED_BUTTON) != redButtonStatus)
    {
      redButtonStatus = digitalRead(RED_BUTTON);
      if (redButtonStatus == HIGH)
      {
        controlState = "released";
        Serial.print(F("red_button "));
        Serial.println(controlState);

        //send message to server and set system status based on response
        if (sendRequest("red_button", controlState))
        {
          systemStatus = 0;
        }
        else
        {
          systemStatus = 2;
        }
        delay(100);
        sti = 0;
      }
      else
      {
        long buttonPressed = millis();
        int seconds = 0;
        //send message on initial state
        controlState = "pressed";
        Serial.print(F("red_button "));
        Serial.println(controlState);

        //send message to server and set system status based on response
        if (sendRequest("red_button", controlState))
        {
          systemStatus = 0;
        }
        else
        {
          systemStatus = 2;
        }
        sti = 0;

        //while switch is held down, send messages each second
        while (digitalRead(RED_BUTTON) == LOW)
        {

          //Solid Red LED
          analogWrite(RED_LED, 255);
          analogWrite(GREEN_LED, 0);
          
          if ((millis() - buttonPressed) > (seconds + 1) * 1000)
          {
            seconds += 1;
            controlState = "held_" + String(seconds) + "_seconds";
            //send message to server and set system status based on response
            if (sendRequest("red_button", controlState))
            {
              systemStatus = 0;
            }
            else
            {
              systemStatus = 2;
            }
          }
        }
      }
    }
    if (digitalRead(GREEN_SWITCH) != greenSwitchStatus )
    {
      greenSwitchStatus = digitalRead(GREEN_SWITCH);
      if (greenSwitchStatus == HIGH)
      {
        controlState = "off";
      }
      else
      {
        controlState = "on";
      }
      Serial.print(F("green_switch turned to "));
      Serial.println(controlState);

      analogWrite(RED_LED, 0);
      analogWrite(GREEN_LED, 255);

      //send message to server and set system status based on response
      if (sendRequest("green_switch", controlState))
      {
        systemStatus = 0;
      }
      else
      {
        systemStatus = 2;
      }
      delay(100);
      sti = 0;
    }

    if (digitalRead(RED_SWITCH) != redSwitchStatus )
    {
      redSwitchStatus = digitalRead(RED_SWITCH);
      if (redSwitchStatus == HIGH)
      {
        controlState = "off";
      }
      else
      {
        controlState = "on";
      }
      Serial.print(F("red_switch turned to "));
      Serial.println(controlState);

      analogWrite(RED_LED, 0);
      analogWrite(GREEN_LED, 255);

      //send message to server and set system status based on response
      if (sendRequest("red_switch", controlState))
      {
        systemStatus = 0;
      }
      else
      {
        systemStatus = 2;
      }
      delay(100);
      sti = 0;
    }

    if (digitalRead(YELLOW_SWITCH) != yellowSwitchStatus )
    {
      yellowSwitchStatus = digitalRead(YELLOW_SWITCH);
      if (yellowSwitchStatus == HIGH)
      {
        controlState = "off";
      }
      else
      {
        controlState = "on";
      }
      Serial.print(F("yellow_switch turned to "));
      Serial.println(controlState);

      analogWrite(RED_LED, 0);
      analogWrite(GREEN_LED, 255);

      //send message to server and set system status based on response
      if (sendRequest("yellow_switch", controlState))
      {
        systemStatus = 0;
      }
      else
      {
        systemStatus = 2;
      }
      delay(100);
      sti = 0;
    }
    if (digitalRead(BLUE_SWITCH) != blueSwitchStatus )
    {
      blueSwitchStatus = digitalRead(BLUE_SWITCH);
      if (blueSwitchStatus == HIGH)
      {
        controlState = "off";
      }
      else
      {
        controlState = "on";
      }
      Serial.print(F("blue_switch turned to "));
      Serial.println(controlState);

      analogWrite(RED_LED, 0);
      analogWrite(GREEN_LED, 255);

      //send message to server and set system status based on response
      if (sendRequest("blue_switch", controlState))
      {
        systemStatus = 0;
      }
      else
      {
        systemStatus = 2;
      }
      delay(100);
      sti = 0;
    }

  }
  else
  {
    systemStatus = 1;
  }

  //if a string has come in over serial or telnet
  if (stringComplete)
  {
    //convert to upper case and trim off whitespace, newlines, etc.
    inputString.toUpperCase();
    
    inputString.trim();

    Serial.print(F("received: "));
    Serial.println(inputString);
    if (telnetClient) {
      thisServer.print(F("received: "));
      thisServer.println(inputString);
    }

    //go through commands
    if (inputString=="?")
    {
      //list available commands
      int k;    // counter variable
      char myChar;
      for (k = 0; k < strlen(commandList); k++)
      {
        myChar =  pgm_read_byte_near(commandList + k);
        Serial.print(myChar);
        if (telnetClient) {
          thisServer.print(myChar);
        }
      }
      Serial.println();
      if (telnetClient) {
        thisServer.println();
      }
    }
 
    //determine which control is being queried by looking at the next part of the string
    else if(inputString == ":STATUS:CONTROL:RED_BUTTON?"){
      Serial.println(redButtonStatus);
     if (telnetClient) {
        thisServer.println(redButtonStatus);
     }
    }
    else if(inputString == ":STATUS:CONTROL:GREEN_BUTTON?")
    {
     Serial.println(greenButtonStatus);
     if (telnetClient) {
        thisServer.println(greenButtonStatus);
     }
    }
    else if(inputString == ":STATUS:CONTROL:BLUE_SWITCH?")
    {
     Serial.println(blueSwitchStatus);
     if (telnetClient) {
        thisServer.println(blueSwitchStatus);
     }
    }
    else if(inputString == ":STATUS:CONTROL:GREEN_SWITCH?")
    {
      Serial.println(greenSwitchStatus);
      if (telnetClient) {
        thisServer.println(greenSwitchStatus);
      }
    }
    else if(inputString == ":STATUS:CONTROL:YELLOW_SWITCH?")
    {
      Serial.println(yellowSwitchStatus);
      if (telnetClient) {
        thisServer.println(yellowSwitchStatus);
      }
    }
    else if(inputString == ":STATUS:CONTROL:RED_SWITCH?")
    {
      Serial.println(redSwitchStatus);
      if (telnetClient) {
        thisServer.println(redSwitchStatus);
      }
    }

    else if (inputString.startsWith(":MAC:"))
    {
      //writeMacAddress(inputString);
      //take the next set of characters and store them to EEPROM
      String MACinput = inputString.substring(5);
      //clean up by removing hyphens if present
      MACinput.replace("-", "");
      //uppercase for easier parsing
      MACinput.toUpperCase();
      //trim
      MACinput.trim();

      //2 ASCII characters for each byte
      if (MACinput.length() == 12)
      {
        for (int i = 0; i < 6; i++)
        {
          int val = h2d(MACinput.charAt(2 * i)) * 16 + h2d(MACinput.charAt(2 * i + 1));
          EEPROM.write(i + 1, val);
        }
        //set these to 0xAA to indicate that an address has been written
        EEPROM.write(0, 170);
        EEPROM.write(7, 170);
        Serial.println(F("wrote MAC address to EEPROM"));
        Serial.println(getMACAddress());
        if (telnetClient) {
          thisServer.println(F("wrote MAC address to EEPROM"));
          thisServer.println(getMACAddress());
        }
      }
      else
      {
        Serial.println(F("unexpected MAC input"));
        if (telnetClient) {
          thisServer.println(F("unexpected MAC input"));
        }
      }
    }
    else if (inputString==":MAC?")
    {
      if (telnetClient) {
        thisServer.println(getMACAddress());
      }
      Serial.println(getMACAddress());
    }
    else if (inputString==":SERVER?")
    {
      if (telnetClient) {thisServer.println(webServer);}
      Serial.println(webServer);
    }
    else if (inputString.startsWith(":SERVER:"))
    {
      //writeServerAddress(inputString);
      const int eepromOffset = 8;
      //take the next set of characters and store them to EEPROM
      String serverInput = inputString.substring(8);
      //trim
      serverInput.trim();

      String sub;
      int dotIndex1 = 0;
      int dotIndex2;//=serverInput.indexOf('.');
      int val;
      char charBuffer[4];


      //Address is to be dot separated, e.g. 192.168.40.11
      for (int i = 0; i < 4; i++)
      {
        dotIndex2 = serverInput.indexOf('.', dotIndex1);
        if (dotIndex2 == -1)
        {
          sub = serverInput.substring(dotIndex1);
        }
        else
        {
          sub = serverInput.substring(dotIndex1, dotIndex2);
        }

        sub.toCharArray(charBuffer, 4);
        Serial.println(charBuffer);
        if (telnetClient) {thisServer.println(charBuffer);}
        val = atoi(charBuffer);
        EEPROM.write(i + eepromOffset + 1, val);
        dotIndex1 = dotIndex2 + 1;
      }
      //set these to 0xAA to indicate that an address has been written
      EEPROM.write(eepromOffset, 170);
      EEPROM.write(eepromOffset + 5, 170);
      Serial.println(F("wrote server address to EEPROM"));

      int aaa = EEPROM.read(9);
      int bbb = EEPROM.read(10);
      int ccc = EEPROM.read(11);
      int ddd = EEPROM.read(12);

      // use the numeric IP instead of the name for the server:
      webServer = IPAddress(aaa, bbb, ccc, ddd); // numeric IP for server
      Serial.println(F("server address reset: "));
      Serial.println(webServer);
      if (telnetClient) {
        thisServer.println(F("wrote server address to EEPROM"));
        thisServer.println(F("server address reset: "));
        thisServer.println(webServer);
      }
    }
    else if (inputString.startsWith(":PORT:"))
    {
      //writeServerPort(inputString);
      const int eepromOffset = 14;
      unsigned int val;
      char charBuffer[6];

      //take the next set of characters and store them to EEPROM
      String serverInput = inputString.substring(6);
      //trim
      serverInput.trim();
      //the result should be an ASCII representation of a unsigned integer (word)
      //convert to unsigned integer
      serverInput.toCharArray(charBuffer, 6);
      val = atol(charBuffer);
      //store each byte to EEPROM
      EEPROM.write(eepromOffset + 1, highByte(val));
      EEPROM.write(eepromOffset + 2, lowByte(val));

      //set these to 0xAA to indicate that an address has been written
      EEPROM.write(eepromOffset, 170);
      EEPROM.write(eepromOffset + 3, 170);
      port = val;
      Serial.println(charBuffer);
      Serial.println(F("wrote server port to EEPROM"));
      Serial.println(F("Server port reset:"));
      Serial.println(port);
      if (telnetClient) {
        thisServer.println(charBuffer);
        thisServer.println(F("wrote server port to EEPROM"));
        thisServer.println(F("Server port reset:"));
        thisServer.println(port);
      }
    }
    else if (inputString==":PORT?")
    {
      if (telnetClient) {
        thisServer.println(port, DEC);
      }
      Serial.println(port, DEC);
    }
    else if (inputString.startsWith(":VERSION?"))
    {
      if (telnetClient) {
        thisServer.println("version: " + versionID);
      }
      Serial.println("version: " + versionID);
    }
    else if (inputString==":CLIENT?")
    {
      if (telnetClient) {
        thisServer.println(Ethernet.localIP());
      }
      Serial.println(Ethernet.localIP());
    }
    else if (inputString==":STATUS:SYSTEM?")
    {
      if (telnetClient) {
        thisServer.println(systemStatus, DEC);
      }
      Serial.println(systemStatus, DEC);
    }
    else if (inputString.startsWith(":STATUS:SYSTEM:"))
    {
      //writeSystemStatus(inputString);
      char charBuffer[6];

      //take the next set of characters
      String statusInput = inputString.substring(8);
      //trim
      statusInput.trim();
      //the result should be an ASCII representation of a unsigned integer (word)
      //convert to unsigned integer
      statusInput.toCharArray(charBuffer, 6);
      systemStatus = atol(charBuffer);
      Serial.println(charBuffer);
      Serial.println(F("set system status"));
      if (telnetClient) {
        thisServer.println(charBuffer);
        thisServer.println(F("set system status"));
      }
    }
    else
    {
      if (telnetClient) {
        thisServer.println(F("Input not understood"));
      }
      Serial.println(F("Input not understood"));
    }

    // clear the string:
    inputString = "";
    stringComplete = false;
  }
}


unsigned char h2d(unsigned char hex)
{
  if (hex > 0x39) hex -= 7; // adjust for hex letters upper or lower case
  return (hex & 0xf);
}

String getMACAddress()
{
  String MACAddress = "";
  //read MAC address from EEPROM
  for (int i = 1; i < 6; i++)
  {
    //take care for the address offset
    MACAddress += String(EEPROM.read(i), HEX);
    MACAddress += "-";
  }
  MACAddress += String(EEPROM.read(6), HEX);


  return MACAddress;

}

boolean sendRequest(String control, String state)
{

  //added GET alternative per http://forum.arduino.cc/index.php/topic,44551.0.html

  unsigned long timeOut = 10000;
  unsigned long startTime;
  boolean retVal = false;
  String servicePage = "GET /WebServiceRemotePower.asmx/AndonListener?";
  String tempStr = "";
  String content = "ClientMacAddress=";
  content += getMACAddress();      //"00-00-00-00-00-05";//
  content += "&ControlName=";
  content += control;
  content += "&ControlState=";
  content += state;

  Serial.println(F("connecting..."));
  // if you get a connection, report back via serial:
  client.flush();
  if (client.connect(webServer, port))
  {
    Serial.println(F("connected"));
    // Make a HTTP request:
    Serial.print(servicePage);
    Serial.print(content);
    Serial.println(F(" HTTP/1.1"));
    client.print(servicePage);
    client.print(content);
    client.println(F(" HTTP/1.1"));
    Serial.print(F("Host: "));
    client.print(F("Host: "));
    Serial.println(webServer);
    client.println(webServer);
    Serial.println(F("Connection: close"));
    client.println(F("Connection: close"));
    Serial.println();
    client.println();

    startTime = millis();
    while (millis() - startTime < timeOut && client.connected())
    {
      tempStr = clientReadServerResponseLine(client);
      tempStr.trim();
      Serial.println(tempStr);
      if (tempStr.startsWith("HTTP/1.1 200 OK"))
      {
        retVal = true;
      }
    }
    client.stop();
  }
  else
  {
    retVal = false;
    // if you didn't get a connection to the server:
    Serial.println(F("connection failed"));
  }

  return retVal;
}

String clientReadServerResponseLine(EthernetClient &_client)
{
  String returnString = "";
  while (_client.connected())
  {
    if (_client.available())
    {
      // get the new byte:
      char inChar =  _client.read();
      // add it to the inputString:
      returnString += inChar;
      if (inChar == '\n')
      {
        break;
      }
    }
  }
  return returnString;
}


/*
  SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 */
void serialEvent()
{
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // add it to the inputString:
    inputString += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      stringComplete = true;
    }
  }
}

void checkForTelnetClientMessage()
{
  telnetClient = thisServer.available();

  if (telnetClient) {
    char inChar = telnetClient.read();
    // add it to the inputString:
    inputString += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      stringComplete = true;
    }
  }
}

//flash status provides status indications for systems with a red and a green LED
//when all is well (status 0), the green led is given a "fade"
//when an error occurs, status >0) the red led will flash out a number equal to status
// int status: the status number, where zero indicates system OK
// int StatusTimeIndex: this is a index passed by reference. it only needs to be initialized to zero in the calling program
//    the function will otherwise control its incrementation between zero and 'statusCounts' then back to zero
// int GreenBrightness: when status is zero, sets the value based on a triangle wave
//    when status is non-zero, value is set to zero
// int RedBrightness: when status is non-zero, value is set to flash out the status number
//    when status is zero, value is set to zero
void flashStatus (int Status, int &StatusTimeIndex, byte &GreenBrightness, byte &RedBrightness)
{
  //status period is the total time before the status repeats
  const int statusPeriodMilliSeconds = 4000;
  //status counts is the number of counts to be used in the status period
  const int statusCounts = 256;
  const int delayTime = statusPeriodMilliSeconds / 256;
  const int maxBrightnessGreen = 255;
  const int maxBrightnessRed = 200;
  const int errorFlashPeriod = statusCounts / 10;
  //this is used to
  boolean redFlash = false;

  if (Status == 0)
  {
    GreenBrightness = maxBrightnessGreen - abs(StatusTimeIndex * 2 % (2 * maxBrightnessGreen) - maxBrightnessGreen);
    //    RedBrightness = 200 - (GreenBrightness * 200/255) ;
    RedBrightness = 0;
  }
  else
  {
    GreenBrightness = 0;

    if (StatusTimeIndex < (errorFlashPeriod * Status) && (StatusTimeIndex % errorFlashPeriod) < (errorFlashPeriod / 2))
    {
      redFlash = true;
    }
    RedBrightness = maxBrightnessRed * redFlash;
  }
  StatusTimeIndex = (StatusTimeIndex + 1) % statusCounts;
  //delay(500);
  delay(delayTime);
}




























