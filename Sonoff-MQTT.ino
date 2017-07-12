/*  Copyright (c) 2017 Juergen Schilling juergen_schilling@web.de

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Dieses Programm ist Freie Software: Sie können es unter den Bedingungen
    der GNU General Public License, wie von der Free Software Foundation,
    Version 3 der Lizenz oder (nach Ihrer Wahl) jeder neueren
    veröffentlichten Version, weiterverbreiten und/oder modifizieren.

    Dieses Programm wird in der Hoffnung, dass es nützlich sein wird, aber
    OHNE JEDE GEWÄHRLEISTUNG, bereitgestellt; sogar ohne die implizite
    Gewährleistung der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
    Siehe die GNU General Public License für weitere Details.

    Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
    Programm erhalten haben. Wenn nicht, siehe <http://www.gnu.org/licenses/>.
*/

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <EEPROM.h>
/****************************************
* Generic Esp8266
* CPU 80MHz
* Flash 1M(64k SPIFFS)
* Sonoff WiFi-Smart-Switch
****************************************/

const char *ssid = "yourSSID";
const char *password = "yourPassword";
const char *mqtt_server = "your-brocker-server";


String Hostname = "Sonoff-";
const String Version ="SonoffSchalter V1.1 \n(C) Schimmi 2017";

int OldMilli;

typedef enum state_t
{
	StateOn = 1,
	StateOff = 0
};
state_t State = StateOff;

#define EE_ADDRESS 10

#define PIN_RELAIS   12
#define PIN_LED   13
#define PIN_BUTTON  0


#define Relais_On HIGH
#define Relais_Off LOW

#define LED_On digitalWrite(PIN_LED, LOW )
#define LED_Off digitalWrite(PIN_LED, HIGH)

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

float temp;

byte addr[8];
int i;

int t1;
int j=0;
uint8_t MAC_array[6];
enum 
{
	StepOn = 10,
	StepOff = 0
};
int Step =  StepOff;


void setup(void) {
  int Timeout;
  Serial.begin(115200);
  Serial.println(Version);
  /* get last state from EEPROM */
  EEPROM.begin(256);
  int eeprom_data = EEPROM.read(EE_ADDRESS);
  if (eeprom_data == 0xFF)
  {
    State = StateOff;
    Serial.println("Init: Off");
  }
  else
  {
    State = (state_t) eeprom_data;
    Serial.printf("Init: ");
    if (State == StateOn)
    {
      Serial.println("EEPROM data On");
      Step = StepOn;
    }
    else
    {
      Serial.println("EEPROM data Off");
      Step = StepOff;
    }
  }

  pinMode(PIN_RELAIS,OUTPUT);
  digitalWrite(PIN_RELAIS, State==StateOn ? Relais_On : Relais_Off);

  pinMode(PIN_LED,OUTPUT);
  LED_On;

  pinMode(PIN_BUTTON, INPUT);

  WiFi.macAddress(MAC_array);
  Hostname = Hostname + String(MAC_array[4], HEX);
  Hostname = Hostname + String(MAC_array[5], HEX);


  Serial.println("Initialized");
  WiFi.mode(WIFI_STA);

  WiFi.hostname(Hostname);

  WiFi.begin ( ssid, password );
  Serial.println("Booting");
  Serial.println ( "" );

  // Wait for connection
  while ( WiFi.status() != WL_CONNECTED ) 
  {
    delay ( 500 );
    Serial.print ( "." );
  }
  LED_Off;
  
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println(Hostname);
  Serial.println();
  delay(1000);

  httpUpdater.setup(&server);
  server.on("/", GetInfo);
  server.on("/On", SetOn);
  server.on("/Off", SetOff);
  server.onNotFound(GetInfo);
  server.begin();               // Starte the Server
  
  client.setServer(mqtt_server, 1883); // starte the MQTT client
  client.setCallback(callback);

  OldMilli = millis();
}

void SetOn()
{
  digitalWrite(PIN_RELAIS, Relais_On);
  //LED_On;
  Step= StepOn;
  State = StateOn;
  EEPROM.write(EE_ADDRESS, State);
  EEPROM.commit();

  server.send(200, "text/plain", "On");
  Serial.println("On ");
}

void SetOff()
{
  digitalWrite(PIN_RELAIS, Relais_Off);
  //LED_Off;
  Step = StepOff;
  State = StateOff;
  EEPROM.write(EE_ADDRESS, State);
  EEPROM.commit();

  server.send(200, "text/plain", "Off");
  Serial.println("Off ");
}

void GetInfo()
{
  String T = "";
  T = Version + "\n\n";
  T = T+ "SSID:     "+WiFi.SSID()+" \n";
  T = T+ "RSSI:     "+String(WiFi.RSSI())+" \n";
  T = T+ "Hostname: "+WiFi.hostname()+" \n";
  T = T+ "Zustand : ";
  if (State == StateOff)
  {
    T = T+" Off \n";
  }
  else
  {
    T = T+" On \n" ;
  }

  T = T+ "/On or /Off\n";
  server.send(200, "text/plain", T);
}

void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) 
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the relay if an 1 was received as first character
  if ((char)payload[0] == '1') 
  {
    SetOn();   // Turn the relay on
  } 
  else 
  {
    SetOff();  // Turn the rely off
  }
}

void reconnect() 
{
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) 
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe("inTopic");
    } 
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void loop()
{
  int mi = millis();
  String T = " ";
  ArduinoOTA.handle();
  if(digitalRead(PIN_BUTTON) == LOW)
  {
    while(digitalRead(PIN_BUTTON) == LOW)
    {}
    if(State == StateOn)
    {
      SetOff();
    }
    else
    {
      SetOn();
    }
  }
  if((mi-OldMilli)>=500)
  {
    OldMilli=mi;
    Serial.print(".");
    switch (Step)
    {
      case StepOff: 
		  LED_On;
          break;
      case 1:
		  LED_Off;
      case 2:
      case 3:
      case 4:
		  break;
      case 5: Step=-1;
		  break;

      case StepOn: 
        LED_Off;
        break;
      case 11:
        LED_On;
      case 12:
      case 13:
      case 14:
        break;
      case 15: Step=9;
        break;
    default:
      Step = StepOff;
      break;
    }
    Serial.println(State);
    Step++;
  }
  server.handleClient();

  if (!client.connected()) 
  {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 2000) 
  {
    lastMsg = now;
    ++value;
    snprintf (msg, 75, "hello world #%ld", value);
    Serial.print("Publish message: ");
    Serial.println(msg);
    client.publish("outTopic", msg);
  }

}
