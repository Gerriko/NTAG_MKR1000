/* ===================================================================================
 *
 * Copyright Gerrikoio, June 2019
 * This code example reads the NTAG EEPROM to see if the data stored is in NDEF format
 * It then looks for WiFi connection details. If found it will attempt to connect
 * to the WiFi network. If already connected it will disconnect and reconnect with
 * the new details.
 * 
 * The ntag library that is used in part can be found here: 
 * 
 * If so, it then uses the NDEF libary as found here https://github.com/don/NDEF.
 * to read and decode the NDEF message(s).
 * 
 * We will use Bounce2 library to avoid debounce on FD_Pin
 * The Bounce2 library found here :
 * https://github.com/thomasfredericks/Bounce2
 * 
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ============================================================================
*/

#include <SPI.h>
#include <WiFi101.h>
#include "connection.h"

#include <nfc_ntag.h>
#include <NdefMessage.h>

#include <Bounce2.h>

#define FD_PIN			        6

const uint16_t
  WIFITIMEOUTDELAY =        25000,      // 25 seconds      
  EEPROMWRITETIMEDELAY =    100;

const char ssidHC[] = WIFI_SSID;        // your hardcoded network SSID
const char passHC[] = WIFI_PASS;        // your hardcoded network password

uint32_t
  t_wifi =                  0L, 
  FDPinDetected =           0L;

int 
  WiFiStatus = WL_IDLE_STATUS;

String 
  NewSSID =                 "",
  NewPWD =                  "";

uint8_t 
  FDstate =                 0;

char 
  *ssid,        // your run-time network SSID
  *pass;        // your run-time network password

bool
  ConnectedToWiFi =         false;

// Initialize the WIFI client library
WiFiClient client;

// NFC Tag Objects
NFC_ntag ntag(NTAG_I2C_1K, FD_PIN);

NdefMessage* _ndefMessage;             // instaniate the ndefMessage object

// Instantiate a Bounce object
Bounce debouncer = Bounce(); 

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial) {}
  delay(500);
  Serial.println(F("NFC ntag - Read EEPROM to get WiFi details"));
  delay(500);

  // Setup the Bounce instance :
  debouncer.attach(FD_PIN);
  debouncer.interval(25); // interval in ms
 
  ntag.begin();

  if (strlen(ssidHC)> 2 && strlen(passHC) > 2) {
    ssid = (char *) malloc(strlen(ssidHC)+1);
    memcpy(ssid, (char *)ssidHC, strlen(ssidHC)+1);
    pass = (char *) malloc(strlen(passHC)+1);
    memcpy(pass, (char *)passHC, strlen(passHC)+1);
    
    establishWiFiConnection();
    
  }
  else {
    Serial.println(F("No hardcoded WiFi credentials found in firmware..."));
  }
  
  Serial.println(F("Checking NTAG EEPROM to see if anything found in NV memory..."));
  checkTheEEPROM();
  
}

void loop() {
  // Update the Bounce instance :
  debouncer.update();

  // Get the updated value for the FD pin:
  FDstate = debouncer.read();

  // we will be polling to check if the FD pin drops to 0 (indicates NFC transaction)
  // Then when FD pin returns to 1 we recheck the EEPROM. 
  if (FDstate) {
    if (FDPinDetected) {
      if ((millis() - FDPinDetected) > EEPROMWRITETIMEDELAY) {
        // We can now check the EEPROM
        checkTheEEPROM();
        
        // Reset timer back to zero
        FDPinDetected = 0;
      }
    }
  }
  else {
    // We update the timer
    FDPinDetected = millis();
    
  }
}

void checkTheEEPROM() {

  const String
    NDEF_WIFIAPPL =           "application/vnd.wfa";

  const uint16_t
    SSID_FIELD_ID =           0x1045,
    NETWORK_KEY_FIELD_ID =    0x1027,
    MAC_ADDRESS_ID =          0x1020,
    AUTH_TYPE_FIELD_ID =      0x1003,
    ENCRYPTION_TYPE_ID =      0x100F,
    AUTH_TYPE_OPEN =          0x0001,
    AUTH_TYPE_WPA_PSK =       0x0002,
    AUTH_TYPE_WPA_EAP =       0x0008,
    AUTH_TYPE_WPA2_EAP =      0x0010,
    AUTH_TYPE_WPA2_PSK =      0x0020,
    ENCRYPT_TYPE_NONE =       0x0001,
    ENCRYPT_TYPE_WEP =        0x0002,
    ENCRYPT_TYPE_TKIP =       0x0004,
    ENCRYPT_TYPE_AES =        0x0008,
    ENCRYPT_TYPE_MMODE =      0x000c;   //AES/TKIP Mixed Mode  

  byte EEPROMdata[16];
  
  // Clear the NewSSID and NewPWD strings
  NewSSID = "";
  NewPWD = "";
  
  // Check the initial block to determine if NDEF of otherwise
  if (ntag.readEeprom(0,EEPROMdata,16)) {
    if (EEPROMdata[0] == 3) {
      //we will place a restriction to say that if size is 255 then this is too long
      if (EEPROMdata[1] < 255) {
        // This is NDEF so lets get how many bytes to decode + 4 headers
        const uint16_t BYTESTODECODE = EEPROMdata[1];
        // Now initialise an array to store the data according to number of bytes to decode
        byte readNDEFfromEeprom[BYTESTODECODE];
        byte NDEFcntr = 0;
        if (BYTESTODECODE < 14) {
          // can now transcribe across
          for (uint8_t i = 0; i < BYTESTODECODE; i++) {
            readNDEFfromEeprom[i] = EEPROMdata[i+2];
          }
          _ndefMessage = new NdefMessage(readNDEFfromEeprom, BYTESTODECODE);

          uint8_t NoRecords = _ndefMessage->getRecordCount();
          NdefRecord record = _ndefMessage->getRecord(0);
          byte TNFval = byte(record.getTnf());
          
        }
        else {
          memcpy(readNDEFfromEeprom, EEPROMdata+2, 14);
          NDEFcntr = 14;
          // We limit outselves up to 255 bytes
          for (uint8_t i = 1; i < 16; i++) {
            if (ntag.readEeprom(0 + i*16, EEPROMdata,16)) {
              if ((NDEFcntr + 16) < BYTESTODECODE) {
                memcpy(readNDEFfromEeprom+NDEFcntr, EEPROMdata, 16);
                NDEFcntr += 16;
              }
              else {
                memcpy(readNDEFfromEeprom+NDEFcntr, EEPROMdata, (BYTESTODECODE - NDEFcntr));
                NDEFcntr += (BYTESTODECODE - NDEFcntr);
                // ----------------------------------------------------------
                // Now let's create out new NDEFMESSAGE object
                // ==========================================================
                
                _ndefMessage = new NdefMessage(readNDEFfromEeprom, NDEFcntr);
                
                uint8_t NoRecords = _ndefMessage->getRecordCount();

                for (int i = 0; i < NoRecords; i++) {
                  NdefRecord record = _ndefMessage->getRecord(i);
                  byte TNFval = byte(record.getTnf());

                  String RTDstr = record.getType();
                  byte RTDval = 0;
                  if (RTDstr.length() == 1) {
                    RTDval = byte(RTDstr.charAt(0));
                  }
                  else if (RTDstr.length() > 1) {
                    // This is likely to be a media type
                    // but it could also be a "special poster" type
                    if (RTDstr.equals("Sp")) {
                      RTDval = byte(RTDstr.charAt(0));    // capture as "S"
                    }
                    else {
                      // Checking common types
                      if (RTDstr.indexOf(NDEF_WIFIAPPL) == 0) RTDval = 1;
                    }
                  }
                  // The TNF and Type should be used to determine how your application processes the payload
                  // There's no generic processing for the payload, it's returned as a byte[]
                  int payloadLength = record.getPayloadLength();
                  byte payload[payloadLength];
                  record.getPayload(payload);
          
                  // Print the Hex and Printable Characters
                  Serial.print("  Payload (HEX): ");
                  PrintHexChar(payload, payloadLength);
          
                  // Force the data into a String (might work depending on the content)
                  // Real code should use smarter processing
                  String payloadAsString = "";

                  // Parse the wifi data payload ---------------------------
                  if (TNFval == 2 && RTDval == 1) {
                    // Here the il flag is 1 so the ID will be set.
                    Serial.print("  ID: ");
                    String TNFid = record.getId();
                    Serial.print(TNFid); Serial.print(" (len:");
                    Serial.print(record.getIdLength(),DEC); Serial.println(")");
                    // Let's parse and conver to uint16 as most data fields are in uint16_t mode
                    uint16_t wifiFieldID = 0;
                    uint16_t wifiFieldLen = 0;
                    for (int c = 3; c < payloadLength; c++) {
                      wifiFieldID = (payload[c-3]<< 8) | payload[c-2];
                      wifiFieldLen = (payload[c-1]<< 8) | payload[c];

                      if (wifiFieldID == SSID_FIELD_ID) {
                        Serial.print(F("  - SSID: (")); Serial.print(c, DEC); Serial.print(") 0x");
                        Serial.print(wifiFieldID, HEX); Serial.print(" [size "); 
                        Serial.print(wifiFieldLen, DEC); Serial.print("] ");
                        if (wifiFieldLen) {
                          byte wifi_c[wifiFieldLen];
                          memcpy(wifi_c, payload+c+1, wifiFieldLen);
                          for (uint8_t i = 0; i < wifiFieldLen; i++) NewSSID += (char)wifi_c[i];
                          Serial.println(NewSSID);
                        }
                      }
                      else if (wifiFieldID == NETWORK_KEY_FIELD_ID) {
                        Serial.print(F("  - Network Key (PWD): (")); Serial.print(c, DEC); Serial.print(") 0x");
                        Serial.print(wifiFieldID, HEX); Serial.print(" [size "); 
                        Serial.print(wifiFieldLen, DEC); Serial.print("] ");
                        if (wifiFieldLen) {
                          byte wifi_c[wifiFieldLen];
                          memcpy(wifi_c, payload+c+1, wifiFieldLen);
                          // Hide the password
                          String Secret;
                          for (uint8_t i = 0; i < wifiFieldLen; i++) {
                            NewPWD += (char)wifi_c[i];
                            Secret += "*";
                          }
                          Serial.println(Secret);
                        }
                      }
                      else if (wifiFieldID == MAC_ADDRESS_ID) {
                        Serial.print(F("  - WLAN MAC Address: (")); Serial.print(c, DEC); Serial.print(") 0x");
                        Serial.print(wifiFieldID, HEX); Serial.print(" [size "); 
                        Serial.print(wifiFieldLen, DEC); Serial.print("] ");
                        if (wifiFieldLen) {
                          Serial.println(MACHexChar(payload+c+1, wifiFieldLen));
                        }
                      }
                      else if (wifiFieldID == AUTH_TYPE_FIELD_ID) {
                        Serial.print(F("  - Auth Type: (")); Serial.print(c, DEC); Serial.print(") 0x");
                        Serial.print(wifiFieldID, HEX); Serial.print(" [size "); 
                        Serial.print(wifiFieldLen, DEC); Serial.print("] ");
                        // Use wifiFieldID to capture the Auth Type 16bit value
                        if (wifiFieldLen == 2) {
                          wifiFieldID = (payload[c+1]<< 8) | payload[c+2];
                          if (wifiFieldID & AUTH_TYPE_WPA_PSK || wifiFieldID & AUTH_TYPE_WPA2_PSK) {
                            Serial.println("WPA_PSK");
                          } 
                          else if (wifiFieldID & AUTH_TYPE_WPA_EAP || wifiFieldID & AUTH_TYPE_WPA2_EAP) {
                            Serial.println("WPA_EAP");
                          } 
                          else if (wifiFieldID & AUTH_TYPE_OPEN) {
                            Serial.println("NONE (Open)");
                          }
                          else {
                            Serial.print("[0x");Serial.print(wifiFieldID, HEX);Serial.println("]");
                          }
                        }
                        else {
                          Serial.println();
                        }
                      }
                      else if (wifiFieldID == ENCRYPTION_TYPE_ID) {
                        Serial.print(F("  - Encryption Type: (")); Serial.print(c, DEC); Serial.print(") 0x");
                        Serial.print(wifiFieldID, HEX); Serial.print(" [size "); 
                        Serial.print(wifiFieldLen, DEC); Serial.print("] ");
                        if (wifiFieldLen == 2) {
                          wifiFieldID = (payload[c+1]<< 8) | payload[c+2];
                          if (wifiFieldID == ENCRYPT_TYPE_NONE) Serial.println(F("No Encryption Type"));
                          else if (wifiFieldID == ENCRYPT_TYPE_WEP) Serial.println(F("WEP Encryption Type - deprecated"));
                          else if (wifiFieldID == ENCRYPT_TYPE_TKIP) Serial.println(F("TKIP Encryption Type"));
                          else if (wifiFieldID == ENCRYPT_TYPE_AES) Serial.println(F("AES Encryption Type"));
                          else if (wifiFieldID == ENCRYPT_TYPE_MMODE) Serial.println(F("AES/TKIP Mixed Mode Type"));  
                        }
                        else {
                          Serial.println();
                        }
                      }
                      //if (payload[c] > 31 && payload[c] < 127) payloadAsString += (char)payload[c];
                      //else payloadAsString += ("[" + String(payload[c],HEX) + "]");
                    }
                    // Check if any new details for us
                    if (NewSSID.length() > 1) establishWiFiConnection();
                  }
                }
                break;
              }
            }
          }
        }  
      }  
      else {
        Serial.println(F("Sorry, the NDEF message is too long for this firmware"));
      }
    }
    else {
      Serial.println(F("Tag not formatted as NDEF"));
    }
  }
  else {
    Serial.println(F("NO DATA found"));
  }
}

// Borrowed from Adafruit_NFCShield_I2C / NDEF library and modified
String MACHexChar(const byte * data, uint8_t numBytes)
{
  String NewMAC = "";
  uint8_t revPos = numBytes-1;
  // Assume MAC address length < 255 (size of uint8_t)
  for (uint8_t x=0; x < numBytes; x++)
  {
    // Append leading 0 for small values
    if (data[revPos-x] <= 0xF) NewMAC += "0";
    NewMAC += String(data[revPos-x], HEX);
    if ((numBytes > 1) && (x != numBytes - 1)) NewMAC += ":";
    
  }
  NewMAC.toUpperCase();
  return NewMAC;
}

void establishWiFiConnection() {

  bool
    DiffWiFidetails = false;

  // Let's if we have anything in the newSSID etc.
  if (NewSSID.length() > 1) {
    Serial.println(F("We have New SSID details. Let's check if different to what we have already"));
    if (strlen(ssid)<= 2) {
      Serial.println(F("The saved runtime SSID is empty."));
      NewSSID.trim();
      ssid = (char *) malloc(sizeof(NewSSID.length())+1);
      NewSSID.toCharArray(ssid, NewSSID.length()+1);
      //ssid = (char *) malloc(sizeof(NewSSID.c_str())+1);
      Serial.print(F("The updated SSID is: ")); Serial.println(ssid);
      if (strlen(ssid)<= 2) {
        DiffWiFidetails = false;     // Did not update
        Serial.print(F("Problem with updated length of SSID. It's too short: ")); Serial.println(strlen(ssid));
      }
    }
    else {
      if (strcmp(ssid,NewSSID.c_str()) != 0) {
        // Different SSID
        Serial.println(F("The stored SSID is different to new one."));
        DiffWiFidetails = true;
        
      }
      else {
        Serial.println(F("The stored SSID is the same as the new one."));
      }
    }
    
    if (NewPWD.length() > 1) {
      Serial.println(F("We have New Password details. Let's check if different to what we have already"));
      if (strlen(pass)<= 2) {
        Serial.println(F("The saved runtime Password is empty."));
        NewPWD.trim();
        pass = (char *) malloc(sizeof(NewPWD.length())+1);
        NewPWD.toCharArray(pass, NewPWD.length()+1);
        Serial.print(F("The size of updated PWD is: ")); Serial.println(strlen(pass));
      }
      else {
        if (strcmp(pass,NewPWD.c_str()) != 0) {
          // Different PWD
          Serial.println(F("The stored Password is different to new one."));
          DiffWiFidetails = true;
        }
        else {
          Serial.println(F("The stored PWD is the same as the new one."));
          DiffWiFidetails = false;
          
        }
      }
    }

    if (DiffWiFidetails) {
      if (ConnectedToWiFi) {
        // We need to check if the WiFi credentials have changed. If so, we must disconnect and then reconnect with the new Credentials
        Serial.print("Now disconnecting from to SSID: "); Serial.println(WiFi.SSID());
        WiFi.disconnect();
        do {
          delay(200);
          WiFiStatus = WiFi.status();
          showMeWiFiScanStatus();
        } while (WiFiStatus != WL_DISCONNECTED);
        
        ConnectedToWiFi = false;
      }
      Serial.println("Updating the new runtime connection details");
      NewSSID.trim();
      memset(ssid, 0, strlen(ssid));
      ssid = (char *) malloc(sizeof(NewSSID.length())+1);
      NewSSID.toCharArray(ssid, NewSSID.length()+1);
      NewPWD.trim();
      memset(pass, 0, strlen(pass));
      pass = (char *) malloc(sizeof(NewPWD.length())+1);
      NewPWD.toCharArray(pass, NewPWD.length()+1);
      
    }
  }


  if (!ConnectedToWiFi && strlen(ssid) > 1) {
    Serial.println();
    Serial.print(F("Attempting to connect to WiFi network."));
    t_wifi = millis();
    // attempt to connect to WiFi network:
    while (WiFiStatus != WL_CONNECTED) {
      if ((millis() - t_wifi) > WIFITIMEOUTDELAY) {
        Serial.print(F("WiFi network connection timeout."));
        t_wifi = 0L;
        break;
      }
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
      WiFiStatus = WiFi.begin(ssid, pass);
      Serial.print(".");
      // wait for connection:
      delay(800);
      showMeWiFiScanStatus();
    }
    if (WiFiStatus == WL_CONNECTED) {
      ConnectedToWiFi = true;
      Serial.println();
      Serial.print("Now connected to SSID: ");
      Serial.println(WiFi.SSID());
      IPAddress ip = WiFi.localIP();
      Serial.print(F("and connected with IP: "));
      Serial.println(ip);
      
      // We now clear the NewSSID and NewPWD strings
      NewSSID = "";
      NewPWD = "";
    }
  }
}

void showMeWiFiScanStatus() {
  switch (WiFiStatus) {
    case WL_IDLE_STATUS:
      Serial.println(F("WiFi Idle Status"));
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println(F("WiFi No SSID Available"));
      break;
    case WL_SCAN_COMPLETED:
      Serial.println(F("WiFi Scan Completed"));
      break;
    case WL_CONNECTED:
      Serial.println(F("WiFi Connected"));
      break;
    case WL_CONNECT_FAILED:
      Serial.println(F("WiFi Connect Failed"));
      break;
    case WL_CONNECTION_LOST:
      Serial.println(F("WiFi Connection Lost"));
      break;
    case WL_DISCONNECTED:
      Serial.println(F("WiFi Disconnected"));
      break;
    case WL_AP_LISTENING:
      Serial.println(F("WiFi AP Listening"));
      break;
    case WL_AP_CONNECTED:
      Serial.println(F("WiFi AP Connected"));
      break;
    case WL_AP_FAILED:
      Serial.println(F("WiFi AP Failed"));
      break;
    case WL_PROVISIONING:
      Serial.println(F("WiFi Provisioning"));
      break;
    case WL_PROVISIONING_FAILED:
      Serial.println(F("WiFi Provisioning Failed"));
      break;
  }
}
