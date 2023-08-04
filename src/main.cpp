/////////////////////////////////////////////////////////////////
// Generic Header
/////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <stdio.h>
#include <thread>
#include <mutex>

/////////////////////////////////////////////////////////////////
// GPIO & freertos
/////////////////////////////////////////////////////////////////
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

/////////////////////////////////////////////////////////////////
// Firebase Client
/////////////////////////////////////////////////////////////////
#define ESP32
#include <WiFi.h>
#include "Firebase_ESP_Client.h"

//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Insert your network credentials
#define WIFI_SSID "FatLady"
#define WIFI_PASSWORD "CaputDracons"

// Insert Firebase project API Key
#define API_KEY "ULTRA_LONG_KEY"

// Insert RTDB URLefine the RTDB URL */
#define DATABASE_URL "SECRET_URL" 

//Define Firebase Data object
FirebaseData   fbdo;
FirebaseData   streamState;
FirebaseData   streamColor;
FirebaseAuth   auth;
FirebaseConfig config;

std::mutex    fireBaseMtx;
unsigned long sendDataPrevMillis = 0;
int           count              = 0;
bool          signupOK           = false;


// setting PWM properties
const int freq = 5000;
const int ledChannel = 0;
const int ledChannelRed = 1;
const int ledChannelGreen = 2;
const int ledChannelBlue = 3;
const int resolution = 8;

enum colors 
{
    BLUE = 0
  , GREEN = 1
  , RED = 2
};

String parentPath    = "test/LED_COLOR/";
String childPaths[3] = {"BLUE", "GREEN", "RED"}; 
int    childVals[3]  = { 0, 0, 0 };


#define PWM_GPIO_RED GPIO_NUM_5
#define PWM_GPIO_GREEN GPIO_NUM_18
#define PWM_GPIO_BLUE GPIO_NUM_19



/**********************************************************************
 * LED configuration
 **********************************************************************/
static uint8_t s_led_state = 0;

void update_color(String iColor)
{
    Serial.printf("Color: %s\n", iColor.c_str());

    if ( iColor == "/BLUE" )
    {
      Serial.printf("WriteBlue - led state %d", int(s_led_state));
      analogWrite(PWM_GPIO_BLUE, (s_led_state) ? (childVals[colors::BLUE]) : 0);
    }
    else if ( iColor == "/GREEN" )
    {
      Serial.printf("WriteGreen - led state %d", int(s_led_state));
      analogWrite(PWM_GPIO_GREEN, (s_led_state) ? (childVals[colors::GREEN]) : 0);
    }
    else if ( iColor == "/RED" )
    {
      Serial.printf("WriteRed - led state %d", int(s_led_state));
      analogWrite(PWM_GPIO_RED, (s_led_state) ? (childVals[colors::RED]) : 0);
    }
}

void configure_led(void)
{
    // Set the GPIO as a push/pull output
    gpio_reset_pin(PWM_GPIO_RED);
    gpio_set_direction(PWM_GPIO_RED, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PWM_GPIO_GREEN);
    gpio_set_direction(PWM_GPIO_GREEN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PWM_GPIO_BLUE);
    gpio_set_direction(PWM_GPIO_BLUE, GPIO_MODE_OUTPUT);
 
}


/**********************************************************************
 * changes
 **********************************************************************/
volatile bool dataStateChanged = false;
volatile bool dataColorChanged = false;

/**********************************************************************
 * Single stream callback - LED state on/off
 **********************************************************************/
void stream_callback(FirebaseStream data)
{
  Serial.println("StreamCallback");
  Serial.printf("sream path, %s\nevent path, %s\ndata type, %s\nevent type, %s\n\n",
                data.streamPath().c_str(),
                data.dataPath().c_str(),
                data.dataType().c_str(),
                data.eventType().c_str());
  printResult(data); // see addons/RTDBHelper.h
  Serial.println();

  // This is the size of stream payload received (current and max value)
  // Max payload size is the payload size under the stream path since the stream connected
  // and read once and will not update until stream reconnection takes place.
  // This max value will be zero as no payload received in case of ESP8266 which
  // BearSSL reserved Rx buffer size is less than the actual stream payload.
  Serial.printf("Received stream payload size: %d (Max. %d)\n\n", data.payloadLength(), data.maxPayloadLength());

  // Due to limited of stack memory, do not perform any task that used large memory here especially starting connect to server.
  // Just set this flag and check it status later.
  dataStateChanged = true;
}

/**********************************************************************
 * Single stream callback - timeout
 **********************************************************************/
void stream_timeout(bool timeout)
{ 
  Serial.printf("StreamTimeout: %d", int(timeout));
}


/**********************************************************************
 * Multi stream callback - Colors
 **********************************************************************/
void stream_color_callback(MultiPathStream data)
{
  Serial.println("StreamColorCallback");
  size_t numChild = sizeof(childPaths) / sizeof(childPaths[0]);

  for (size_t i = 0; i < numChild; i++)
  {
    if (data.get(childPaths[i]))
    {
      Serial.printf("path: %s, event: %s, type: %s, value: %s%s", data.dataPath.c_str(), data.eventType.c_str(), data.type.c_str(), data.value.c_str(), i < numChild - 1 ? "\n" : "");
      childVals[i] = data.value.toInt();
    }
  }

  Serial.println();


  // This is the size of stream payload received (current and max value)
  // Max payload size is the payload size under the stream path since the stream connected
  // and read once and will not update until stream reconnection takes place.
  // This max value will be zero as no payload received in case of ESP8266 which
  // BearSSL reserved Rx buffer size is less than the actual stream payload.
  Serial.printf("Received stream payload size: %d (Max. %d)\n\n", data.payloadLength(), data.maxPayloadLength());

  // Due to limited of stack memory, do not perform any task that used large memory here especially starting connect to server.
  // Just set this flag and check it status later.
  dataColorChanged = true;
}


/**********************************************************************
 * Multi stream callback - timeout
 **********************************************************************/
void stream_color_timeout(bool timeout)
{ 
  Serial.println("StreamColorTimeout");
}


/**********************************************************************
 * WiFi config
 **********************************************************************/
void configure_wifi()
{
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Assign the api key
  config.api_key = API_KEY;

  // Assign the RTDB URL
  config.database_url = DATABASE_URL;

  // Sign up
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  // Assign the callback function for the long running token generation task
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // connect colors first
  if (!Firebase.RTDB.beginMultiPathStream(&streamColor, "test/LED_COLOR"))
    Serial.printf("sream begin error, %s\n\n", streamColor.errorReason().c_str());

  Firebase.RTDB.setMultiPathStreamCallback(&streamColor, stream_color_callback, stream_color_timeout);

  // then state of LED
  if (!Firebase.RTDB.beginStream(&streamState, "test/LED_STATE"))
    Serial.printf("sream begin error, %s\n\n", streamState.errorReason().c_str());

  Firebase.RTDB.setStreamCallback(&streamState, stream_callback, stream_timeout);
}



/////////////////////////////////////////////////////////////////
// Init 
/////////////////////////////////////////////////////////////////
void setup() 
{
  // put your setup code here, to run once:
  configure_led();

  // WIFI
  configure_wifi();
}


/////////////////////////////////////////////////////////////////
// Change color or state
/////////////////////////////////////////////////////////////////
void loop()
{
  if (dataStateChanged)
  {
    dataStateChanged = false;

    /* Set the GPIO level according to the state (LOW or HIGH)*/
    s_led_state = streamState.boolData();

    for ( int i = 0; i < sizeof(childVals) / sizeof(int); i++ )
    {
      String color = "/";
      color += childPaths[i];
      update_color( color );
    }
  }

  if (dataColorChanged)
  {
    dataColorChanged = false;

    Serial.printf("path: %s, event: %s, streampath: %s", streamColor.dataPath().c_str(), streamColor.eventType().c_str(), streamColor.streamPath().c_str());

    update_color( /* color */ streamColor.dataPath() );
  }
}

