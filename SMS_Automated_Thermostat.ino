#include <SoftwareSerial.h>
#include <DHT.h>

// --- Pins ---
SoftwareSerial gsmSerial(7, 8); // RX, TX

#define DHTPIN 4       // Data pin for DHT22
#define FAN_PIN 5      // Relay IN1 for Fan
#define HEATER_PIN 6   // Relay IN2 for Heater

// --- Relay Logic ---
// Relays are Active LOW (LOW turns ON)
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// --- DHT Setup ---
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- Global Variables ---
float currentTemp = 0;
float currentHumidity = 0;
float targetTemp = 25.0;      // Default target temp
bool automationEnabled = true; // Start in AUTO mode
String lastSenderNumber = "";

// --- Timer Variables ---
unsigned long lastSensorCheck = 0;
const long sensorCheckInterval = 5000; // Check sensors every 5 seconds

void setup() {
  Serial.begin(9600);
  Serial.println("System is starting...");

  // Set pin modes for relays
  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  
  // Start with relays OFF
  digitalWrite(FAN_PIN, RELAY_OFF);
  digitalWrite(HEATER_PIN, RELAY_OFF);
  
  // Start DHT sensor
  dht.begin();

  // Start GSM serial
  gsmSerial.begin(9600);
  delay(1000);

  Serial.println("Initializing GSM module for SMS...");
  
  // Configure GSM module for text-mode SMS
  delay(5000); // Wait for module to boot
  gsmSerial.println("AT"); // Check if module is responsive
  delay(1000);
  gsmSerial.println("AT+CMGF=1"); // Set SMS mode to Text
  delay(1000);
  gsmSerial.println("AT+CNMI=2,2,0,0,0"); // Set to notify on new SMS
  delay(1000);
  
  Serial.println("System Ready. Automation is ON.");
  Serial.print("Target temp is: ");
  Serial.println(targetTemp);
}

void loop() {
  // --- Task 1: Check for incoming SMS commands ---
  if (gsmSerial.available()) {
    String gsmResponse = gsmSerial.readString();
    Serial.println(gsmResponse); // Print all GSM chatter for debugging

    // Check if this is a new SMS notification
    if (gsmResponse.indexOf("+CMT:") > -1) {
      parseSmsCommand(gsmResponse);
    }
  }

  // --- Task 2: Run thermostat logic every 5 seconds ---
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorCheck >= sensorCheckInterval) {
    lastSensorCheck = currentMillis;
    
    // Read sensor data
    currentHumidity = dht.readHumidity();
    currentTemp = dht.readTemperature(); // Read in Celsius

    // Check if read failed
    if (isnan(currentHumidity) || isnan(currentTemp)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      Serial.print("Sensor Check -> Temp: ");
      Serial.print(currentTemp);
      Serial.println(" C");
    }

    // Run the automation logic
    runThermostatLogic();
  }
}

// --- This function runs the main automation ---
void runThermostatLogic() {
  // Only run if automation is enabled
  if (automationEnabled) {
    // Check if temp is valid
    if (isnan(currentTemp)) {
      // Don't do anything if sensor read failed
      return; 
    }
    
    // Define a 1-degree "deadband" to prevent rapid switching
    float deadband = 1.0; 

    if (currentTemp > targetTemp + deadband) {
      // Too HOT: Turn fan ON, heater OFF
      digitalWrite(FAN_PIN, RELAY_ON);
      digitalWrite(HEATER_PIN, RELAY_OFF);
      Serial.println("Auto: Too hot, Fan ON");
    } else if (currentTemp < targetTemp - deadband) {
      // Too COLD: Turn fan OFF, heater ON
      digitalWrite(FAN_PIN, RELAY_OFF);
      digitalWrite(HEATER_PIN, RELAY_ON);
      Serial.println("Auto: Too cold, Heater ON");
    } else {
      // JUST RIGHT: Turn both OFF
      digitalWrite(FAN_PIN, RELAY_OFF);
      digitalWrite(HEATER_PIN, RELAY_OFF);
      Serial.println("Auto: Temp is stable");
    }
  }
  // If automation is OFF, this logic is skipped
}

// --- This function reads the SMS and decides what to do ---
void parseSmsCommand(String response) {
  Serial.println("New SMS Received! Parsing command...");

  // --- 1. Extract Sender Number ---
  // Format is: +CMT: "+8801xxxxxxxxx","","..."
  int numStart = response.indexOf("\"") + 1;
  int numEnd = response.indexOf("\"", numStart);
  String senderNumber = response.substring(numStart, numEnd);
  lastSenderNumber = senderNumber; // Save for later
  
  Serial.print("From: ");
  Serial.println(senderNumber);

  // --- 2. Extract Message Body ---
  // The message is on the next line
  int msgStart = response.indexOf("\n", numEnd) + 1;
  String msg = response.substring(msgStart);
  msg.trim(); // Remove any extra spaces or newlines
  msg.toUpperCase(); // Make it case-insensitive (e.g., "status" == "STATUS")

  Serial.print("Message: ");
  Serial.println(msg);

  String replyMessage = ""; // We will build a reply

  // --- 3. Match the command ---
  if (msg.indexOf("STATUS") > -1) {
    String mode = automationEnabled ? "AUTO" : "MANUAL";
    replyMessage = "Temp: " + String(currentTemp) + "C\n";
    replyMessage += "Hum: " + String(currentHumidity) + "%\n";
    replyMessage += "Target: " + String(targetTemp) + "C\n";
    replyMessage += "Mode: " + mode;
    
  } else if (msg.indexOf("AUTO") > -1) {
    automationEnabled = true;
    replyMessage = "Automation is now ON. Target: " + String(targetTemp) + "C";
    
  } else if (msg.indexOf("MANUAL") > -1) {
    automationEnabled = false;
    // Turn off all relays when switching to manual
    digitalWrite(FAN_PIN, RELAY_OFF);
    digitalWrite(HEATER_PIN, RELAY_OFF);
    replyMessage = "Automation is OFF. Relays are OFF.";

  } else if (msg.indexOf("FAN ON") > -1) {
    if (automationEnabled) {
      replyMessage = "Cannot turn on Fan. System is in AUTO mode.";
    } else {
      digitalWrite(FAN_PIN, RELAY_ON);
      replyMessage = "Manual Fan: ON";
    }
    
  } else if (msg.indexOf("FAN OFF") > -1) {
     if (!automationEnabled) {
       digitalWrite(FAN_PIN, RELAY_OFF);
       replyMessage = "Manual Fan: OFF";
     }
     
  } else if (msg.indexOf("HEAT ON") > -1) {
    if (automationEnabled) {
      replyMessage = "Cannot turn on Heater. System is in AUTO mode.";
    } else {
      digitalWrite(HEATER_PIN, RELAY_ON);
      replyMessage = "Manual Heater: ON";
    }

  } else if (msg.indexOf("HEAT OFF") > -1) {
     if (!automationEnabled) {
       digitalWrite(HEATER_PIN, RELAY_OFF);
       replyMessage = "Manual Heater: OFF";
     }

  } else if (msg.indexOf("SET") > -1) {
    // This is trickier. We need to find the number after "SET "
    // Find the space after "SET"
    int tempStart = msg.indexOf("SET") + 4; // 4 chars: S, E, T, [space]
    String tempValue = msg.substring(tempStart);
    targetTemp = tempValue.toFloat(); // Convert text to a number
    replyMessage = "New Target Temp is " + String(targetTemp) + "C";
  }
  
  // --- 4. Send the reply (if any) ---
  if (replyMessage.length() > 0) {
    sendSms(senderNumber, replyMessage);
  }
}

// --- This function sends an SMS ---
void sendSms(String number, String message) {
  Serial.print("Sending reply to ");
  Serial.println(number);

  gsmSerial.println("AT+CMGF=1"); // Set text mode
  delay(1000);
  
  // Force GSM alphabet (solves many send errors)
  gsmSerial.println("AT+CSCS=\"GSM\"");
  delay(1000);
  
  // Set the recipient's phone number
  gsmSerial.println("AT+CMGS=\"" + number + "\"");
  delay(1000);
  
  // Send the message text
  gsmSerial.print(message);
  delay(1000);
  
  // Send the "End of Message" character (Ctrl+Z)
  gsmSerial.write(26); 
  delay(1000);
  
  Serial.println("Reply Sent!");
}
