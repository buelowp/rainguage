#include <ArduinoJson.h>
#include <PubSubClient.h>

/* Convert RF signal into bits (rain gauge version) 
 * Written by : Ray Wang (Rayshobby LLC)
 * http://rayshobby.net/?p=9056
 */
#include "ESP8266WiFi.h"

const char* ssid = "Office"; //Enter SSID
const char* password = "Motorazr2V8"; //Enter Password
const char* mqtt_server = "172.24.1.13";

WiFiClient espClient;
PubSubClient client(espClient);

// ring buffer size has to be large enough to fit
// data between two successive sync signals
#define RING_BUFFER_SIZE  256

#define SYNC_HIGH  600
#define SYNC_LOW   600
#define BIT1_HIGH  400
#define BIT1_LOW   220
#define BIT0_HIGH  220
#define BIT0_LOW   400

#define DATAPIN  3  // D3 is interrupt 1

unsigned long timings[RING_BUFFER_SIZE];
unsigned int syncIndex1 = 0;  // index of the first sync signal
unsigned int syncIndex2 = 0;  // index of the second sync signal
bool received = false;

String clientId = "rainguage-";

// detect if a sync signal is present
bool isSync(unsigned int idx) {
  // check if we've received 4 squarewaves of matching timing
  int i;
  for(i=0;i<8;i+=2) {
    unsigned long t1 = timings[(idx+RING_BUFFER_SIZE-i) % RING_BUFFER_SIZE];
    unsigned long t0 = timings[(idx+RING_BUFFER_SIZE-i-1) % RING_BUFFER_SIZE];    
    if(t0<(SYNC_HIGH-100) || t0>(SYNC_HIGH+100) ||
       t1<(SYNC_LOW-100)  || t1>(SYNC_LOW+100)) {
      return false;
    }
  }
  return true;
}

/* Interrupt 1 handler */
void ICACHE_RAM_ATTR handler() {
    static unsigned long duration = 0;
    static unsigned long lastTime = 0;
    static unsigned int ringIndex = 0;
    static unsigned int syncCount = 0;

    // ignore if we haven't processed the previous received signal
    if (received == true) {
        return;
    }
  
    // calculating timing since last change
    long time = micros();
    duration = time - lastTime;
    lastTime = time;

    // store data in ring buffer
    ringIndex = (ringIndex + 1) % RING_BUFFER_SIZE;
    timings[ringIndex] = duration;

    // detect sync signal
    if (isSync(ringIndex)) {
        syncCount ++;
        // first time sync is seen, record buffer index
        if (syncCount == 1) {
            syncIndex1 = (ringIndex+1) % RING_BUFFER_SIZE;
        } 
        else if (syncCount == 2) {
            // second time sync is seen, start bit conversion
            syncCount = 0;
            syncIndex2 = (ringIndex+1) % RING_BUFFER_SIZE;
            unsigned int changeCount = (syncIndex2 < syncIndex1) ? (syncIndex2+RING_BUFFER_SIZE - syncIndex1) : (syncIndex2 - syncIndex1);
            // changeCount must be 136 -- 64 bits x 2 + 8 for sync
            if (changeCount != 136) {
                received = false;
                syncIndex1 = 0;
                syncIndex2 = 0;
            } 
            else {
                received = true;
            }
        }
    }
}

int t2b(unsigned int t0, unsigned int t1) 
{
    if (t0>(BIT1_HIGH-100) && t0<(BIT1_HIGH+100) && t1>(BIT1_LOW-100) && t1<(BIT1_LOW+100)) {
        return 1;
    } else if (t0>(BIT0_HIGH-100) && t0<(BIT0_HIGH+100) && t1>(BIT0_LOW-100) && t1<(BIT0_LOW+100)) {
        return 0;
    }
    return -1;  // undefined
}

void wifiSetup() 
{
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    randomSeed(micros());
    
    Serial.print("");
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) 
{
}

void reconnect() {
    // Loop until we're reconnected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Create a random client ID
        // Attempt to connect
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
            // Once connected, publish an announcement...
            StaticJsonDocument<256> json;
            json["device"]["id"] = ESP.getChipId();
            json["device"]["name"] = "rainguage";
            json["event"] = "startup";
            char msg[128];
            size_t length = serializeJson(json, msg);
            client.publish("weather/startup", msg);
            Serial.print("Client connected as: ");
            Serial.println(clientId);
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nRain Guage Monitor Started.");
    
    pinMode(D1, INPUT);
    attachInterrupt(D1, handler, CHANGE);

    clientId += String(ESP.getChipId());

    wifiSetup();  
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void loop() {
    static long initial_value = -1;
    static long last_value = 0;
  
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    if (received == true) {
        // disable interrupt to avoid new data corrupting the buffer
        detachInterrupt(D1);
    
        // extract rain clicks
        unsigned int startIndex, stopIndex;
        unsigned long rain = 0;
        bool fail = false;
    
        // the lowest 7 bits of byte 3, 4, 5, 6
        for(int byteidx=3;byteidx<7;byteidx++) {
            startIndex = (syncIndex1 + (byteidx*8+1)*2) % RING_BUFFER_SIZE;
            stopIndex =  (syncIndex1 + (byteidx*8+8)*2) % RING_BUFFER_SIZE;
            for (int i=startIndex; i!=stopIndex; i=(i+2) % RING_BUFFER_SIZE) {
                int bit = t2b(timings[i], timings[(i+1) % RING_BUFFER_SIZE]);
                rain = (rain << 1) + bit;
                if (bit < 0)  
                    fail = true;
            }
        }
        
        if (fail) {
            Serial.println("Decoding error.");
        }
        else {
            if (initial_value < 0) 
                initial_value = rain;

            int value = rain - initial_value;
            if (value < 1000 && value > 0) {
                Serial.print("Total rain clicks ");
                Serial.print("[");
                Serial.print(millis());
                Serial.print("]: ");
                Serial.println(value);
                StaticJsonDocument<256> json;
                json["rainfall"]["accumulated"] = value;
                json["rainfall"]["latest"] = value - last_value;
                last_value = value;
                char buffer[128];
                size_t len = serializeJson(json, buffer);
                client.publish("weather/event/rainfall", buffer);
            }
            else {
                StaticJsonDocument<256> json;
                json["device"] = ESP.getChipId();
                json["event"] = "heartbeat";
                char buffer[128];
                size_t len = serializeJson(json, buffer);
                client.publish("weather/event/rainfall", buffer);
                
                Serial.print("Ignoring click value, but using as heartbeat: ");
                Serial.println(value);
            }
        }
    
        // delay for 1 second to avoid repetitions
        delay(500);
        received = false;
        syncIndex1 = 0;
        syncIndex2 = 0;
    
        // re-enable interrupt
        attachInterrupt(D1, handler, CHANGE);
    }
}
