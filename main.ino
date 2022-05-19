#include "esp_camera.h"
#include <WiFi.h>

///////
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
/////////
//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM

#include "camera_pins.h"

//////////////////////////////
String dId = "XXXXXXXXXXXXXXXX";
String webhook_pass = "XXXXXXXXXXXXXXXXXXXXXXXXXX";
String webhook_endpoint = "http://XXXXXXXXXXXXXXXXXXXXXXXXX:3001/api/getdevicecredentials";
const char* mqtt_server = "XXXXXXXXXXXXXXXXXXXXXXXX";

//WiFi
const char* wifi_ssid = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
const char* wifi_password = "XXXXXXXXXXXXXXXXXX";
/////////////////////////////////

//void startCameraServer();

//Functions definitions
bool get_mqtt_credentials();
void check_mqtt_connection();
bool reconnect();
void process_sensors();
void send_data_to_broker();
void print_stats();

//Global Vars
WiFiClient  espclient;
PubSubClient client(espclient);
long lastReconnectAttemp = 0;
long varsLastSend[20];
String last_received_msg = "";
String last_received_topic = "";



DynamicJsonDocument mqtt_data_doc(2048);

void setup() {
  /////////////
  
  
  Serial.begin(9600);

  delay(3000);
  
  Serial.print("══════════════════════════════════════════════════════════════" );

  Serial.print("\n\n");
  Serial.print("\n╔══════════════════════════╗" );
  Serial.print("\n║ 1   STARTING SYSTEM    1 ║" );
  Serial.print("\n╚══════════════════════════╝" );

  Serial.print("\n\n");
  Serial.print("\n╔══════════════════════════╗" );
  Serial.print("\n║ 2    WIFI CONNECTION   2 ║" );
  Serial.print("\n╚══════════════════════════╝" );
  Serial.print("\n\n");
  
  Serial.print("WiFi Connection in Progress");

  WiFi.begin(wifi_ssid, wifi_password);

  int counter = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    Serial.print("...");
    counter++;
    
    if (counter > 10)
    {
      Serial.println(" Ups WiFi Connection Failed -> Restarting..." );
      delay(2000);
      ESP.restart();
    }
  }

  //Printing local ip
  Serial.println("\n");
  Serial.println("WiFi Connection ---------------------> SUCCESS");
  Serial.println("Local IP: " );
  Serial.println(WiFi.localIP());
  
  ///////////////
  
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); // flip it back
    s->set_brightness(s, 1); // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  

  //startCameraServer();
}

void loop() {
  // put your main code here, to run repeatedly:
  
  check_mqtt_connection();
  delay(3000);
}


///////////////////

void process_sensors(){

  //get temp

  
    camera_fb_t *fb = esp_camera_fb_get();

    
    Serial.println((const char*)fb->buf); 
    Serial.print(fb->len);
    
  //
    mqtt_data_doc["variables"][0]["last"]["value"] = (const char*)fb->buf;

    mqtt_data_doc["variables"][0]["last"]["save"] = 1;
  
  esp_camera_fb_return(fb);
}


void send_data_to_broker(){

  long now = millis();

  for(int i = 0; i < mqtt_data_doc["variables"].size(); i++){

    if (mqtt_data_doc["variables"][i]["variableType"] == "output"){
      continue;
    }

    int freq = mqtt_data_doc["variables"][i]["variableSendFreq"];

    if (now - varsLastSend[i] > freq * 1000){
      varsLastSend[i] = millis();

      String str_root_topic = mqtt_data_doc["topic"];
      String str_variable = mqtt_data_doc["variables"][i]["variable"];
      String topic = str_root_topic + str_variable + "/sdata";

      String toSend = "";

      serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);

      client.publish(topic.c_str(), toSend.c_str());
      
      Serial.println("");
      Serial.println("TOPIC:");
      Serial.println(topic);
      Serial.println("VALUE:");
      Serial.println(toSend);

       //STATS
      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;
    }
    


  }

}

void check_mqtt_connection(){
  
  if(!client.connected()){

    if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(" WiFi Connection Failed -> Restarting...");
    delay(15000);
    ESP.restart();
  }
    
    long now = millis();

    if (now - lastReconnectAttemp > 5000){
      lastReconnectAttemp = millis();
       if(reconnect()){
         lastReconnectAttemp = 0;
       }
    }

  }else{
    client.loop();
    process_sensors();
    send_data_to_broker();
    print_stats();
  }

}

bool reconnect(){

  if (!get_mqtt_credentials()){
    Serial.println("\n");
    Serial.println("Error getting mqtt credentials :( \n\n RESTARTING IN 10 SECONDS");
    delay(10000);
    return false;
  }

  //Setting up Mqtt Server
  client.setServer(mqtt_server, 1883);
  Serial.println("\n");
  Serial.print("Trying MQTT Connection");
  Serial.println("\n");
  
  String str_client_id = "device_" + dId + "_" + random(1,9999);
  const char* username = mqtt_data_doc["username"];
  const char* password = mqtt_data_doc["password"];
  String str_topic = mqtt_data_doc["topic"];

  if(client.connect(str_client_id.c_str(), username, password)){
    Serial.print("Mqtt Client Connection ---------------------> SUCCESS");
    Serial.println("\n");
    delay(2000);

    client.subscribe((str_topic + "+/actdata").c_str());
    return true;
  }else{
    Serial.print("Mqtt Client Connection Failed :( ");
    Serial.println("\n");
    return false;
  }


}


bool get_mqtt_credentials() {

  Serial.print("\n╔══════════════════════════╗" );
  Serial.print("\n║ 3    MQTT CONNECTION   3 ║" );
  Serial.print("\n╚══════════════════════════╝" );
  Serial.print("\n\n");

  Serial.print("Getting MQTT Credentials from WebHook");
  delay(1000);

  String toSend = "dId=" + dId + "&password=" + webhook_pass;

  WiFiClient client2;
  HTTPClient http;
  http.begin(client2,webhook_endpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int response_code = http.POST(toSend);


  if (response_code < 0 ) {
    Serial.print("Error Sending Post Request :( ");
    http.end();
    return false;
  }

  if (response_code != 200) {
    Serial.print("Error in response :(   e-> " + response_code);
    http.end();
    return false;
  }

  if (response_code == 200) {
    String responseBody = http.getString();
    Serial.print("\n");
    Serial.print("Mqtt Credentials Obtained  -------------> Successfully");
    Serial.print("\n");
    deserializeJson(mqtt_data_doc, responseBody);
    //http.end();
    delay(1000);
    return true; 
  }
   
}
long lastStats = 0;

void print_stats()
{
  long now = millis();

  if (now - lastStats > 2000)
  {
    lastStats = millis();

    Serial.print("\n\n");
    Serial.print("\n╔══════════════════════════╗" );
    Serial.print("\n║       SYSTEM STATS       ║" );
    Serial.print("\n╚══════════════════════════╝" );
    Serial.print("\n\n");

    Serial.println("#  Name   Var         Type   Count  Last Value");

    for (int i = 0; i < mqtt_data_doc["variables"].size(); i++)
    {

      String variableFullName = mqtt_data_doc["variables"][i]["variableFullName"];
      String variable = mqtt_data_doc["variables"][i]["variable"];
      String variableType = mqtt_data_doc["variables"][i]["variableType"];
      String lastMsg = mqtt_data_doc["variables"][i]["last"];
      long counter = mqtt_data_doc["variables"][i]["counter"];

      Serial.println(String(i) + "  " + variableFullName.substring(0,5) + "  " + variable.substring(0,10) + "  " + variableType.substring(0,5) + "  " + String(counter).substring(0,10) + "      " + lastMsg);
    }
  }
}