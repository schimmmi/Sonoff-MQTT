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
****************************************/

const char *ssid = "yourSSID";
const char *password = "yourPassword";
const char *mqtt_server = "your-brocker-server";


String Hostname = "Sonoff-";
const String Version ="SonoffSchalter V1.1 \n(C) Schimmi 2017";

int altMilli;
#define ZEin 1
#define ZAus 0
int Zustand = ZAus;
#define EE_ADDRESS 10

#define PIN_Relais   12
#define PIN_LED   13
#define PIN_TASTER  0


#define Relais_Ein HIGH
#define Relais_Aus LOW

#define LED_Ein digitalWrite(PIN_LED, LOW )
#define LED_Aus digitalWrite(PIN_LED, HIGH)

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
int schritt=0;


void setup(void) {
  int Timeout;
  Serial.begin(115200);
  Serial.println(Version);
  /* get last state from EEPROM */
  EEPROM.begin(256);
  int eeprom_data = EEPROM.read(EE_ADDRESS);
  if (eeprom_data == 0xFF)
  {
    Zustand = ZAus;
    Serial.println("Init: Aus");
  }
  else
  {
    Zustand = eeprom_data;
    Serial.printf("Init: ");
    if (Zustand == ZEin)
    {
      Serial.println("EEPROM data Ein");
      schritt = 10;
    }
    else
    {
      Serial.println("EEPROM data Aus");
      schritt = 0;
    }
  }

  pinMode(PIN_Relais,OUTPUT);
  digitalWrite(PIN_Relais, Zustand==1?Relais_Ein:Relais_Aus);

  pinMode(PIN_LED,OUTPUT);
  LED_Ein;

  pinMode(PIN_TASTER, INPUT);

  WiFi.macAddress(MAC_array);
  Hostname = Hostname + String(MAC_array[4], HEX);
  Hostname = Hostname + String(MAC_array[5], HEX);


  Serial.println("Initialized");
  WiFi.mode(WIFI_STA);

  //wifiMulti.addAP("schimmi2", "ot3458to"); // hier müssen natürlich die richtigen Angaben rein
  //wifiMulti.addAP("SSID2", "PW2");
  //wifiMulti.addAP("SSID3", "PW3");
  WiFi.hostname(Hostname);

  LED_Ein;
  WiFi.begin ( ssid, password );
  Serial.println("Booting");
  Serial.println ( "" );

  // Wait for connection
  while ( WiFi.status() != WL_CONNECTED ) 
  {
    delay ( 500 );
    Serial.print ( "." );
  }
/*  while (wifiMulti.run() != WL_CONNECTED)// Warten auf Verbindung
  {
    delay(500);
    Timeout++;
    if ((Timeout & 1)>0) LED_Aus;
    else                 LED_Ein;
    if (Timeout>100) break;
    Serial.print(".");
  } */
  LED_Aus;
  
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
  server.on("/", Ereignis_Info);
  server.on("/Ein", Ereignis_Ein);
  server.on("/Aus", Ereignis_Aus);
  server.onNotFound(Ereignis_Info);
  server.begin();               // Starte den Server
  
  client.setServer(mqtt_server, 1883); // starte den client
  client.setCallback(callback);

  altMilli = millis();
}

void Ereignis_Ein()
{
  digitalWrite(PIN_Relais, Relais_Ein);
  //LED_Ein;
  schritt = 10;
  Zustand = ZEin;
  EEPROM.write(EE_ADDRESS, Zustand);
  EEPROM.commit();

  server.send(200, "text/plain", "Ein");
  Serial.println("Ein ");
}

void Ereignis_Aus()
{
  digitalWrite(PIN_Relais, Relais_Aus);
  //LED_Aus;
  schritt = 0;
  Zustand = ZAus;
  EEPROM.write(EE_ADDRESS, Zustand);
  EEPROM.commit();

  server.send(200, "text/plain", "Aus");
  Serial.println("Aus ");
}

void Ereignis_Info()
{
  String T = "";
  T = Version + "\n\n";
  T = T+ "SSID:     "+WiFi.SSID()+" \n";
  T = T+ "RSSI:     "+String(WiFi.RSSI())+" \n";
  T = T+ "Hostname: "+WiFi.hostname()+" \n";
  T = T+ "Zustand : ";
  if (Zustand == ZAus)
  {
    T = T+" Aus \n";
  }
  else
  {
    T = T+" Ein \n" ;
  }

  T = T+ "/Ein oder /Aus\n";
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
    Ereignis_Ein();   // Turn the relay on
  } 
  else 
  {
    Ereignis_Aus();  // Turn the rely off
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
  if(digitalRead(PIN_TASTER) == LOW)
  {
    while(digitalRead(PIN_TASTER) == LOW)
    {}
    if(Zustand == ZEin)
    {
      Ereignis_Aus();
    }
    else
    {
      Ereignis_Ein();
    }
  }
  if((mi-altMilli)>=500)
  {
    altMilli=mi;
    Serial.print(".");
    switch (schritt)
    {
      case 0: // Aus
        LED_Ein;
        Serial.print('E');
        break;
      case 1:
        LED_Aus;
       Serial.print('A');
      case 2:
      case 3:
      case 4:
        break;
      case 5: schritt=-1;
        break;

      case 10: // Ein
        LED_Aus;
        break;
      case 11:
        LED_Ein;
      case 12:
      case 13:
      case 14:
        break;
      case 15: schritt=9;
        break;
    default:
      schritt = 0;
      break;
    }
    Serial.println(schritt);
    schritt++;
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
