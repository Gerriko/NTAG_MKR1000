/* ===================================================================================
 *
 * Copyright Gerrikoio, June 2019
 * This code example reads the NTAG EEPROM to see if the data stored is in NDEF format
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
#include <nfc_ntag.h>
#include <NdefMessage.h>
#include <Bounce2.h>

#define FD_PIN			        6         // Field detection pin (an input for the MKR1000)

const uint16_t
  EEPROMWRITETIMEDELAY =    100;      // We allows for a short delay to allow the EEPROM write to complete

uint32_t 
  FDPinDetected =           0L;       // A timer for monitoring Field Detect pin and state

uint8_t 
  FDstate =                 0;

// NFC Tag Objects
NFC_ntag ntag(NTAG_I2C_1K, FD_PIN);    // instantiate the NTAG object

NdefMessage* _ndefMessage;             // instaniate the ndefMessage object

// Instantiate a Bounce object
Bounce debouncer = Bounce(); 

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial) {}
  delay(500);
  Serial.println(F("NXP NFC NTAG - Read EEPROM to get NDEF details"));
  delay(500);

  // Setup the Bounce instance :
  debouncer.attach(FD_PIN);
  debouncer.interval(25); // interval in ms
 
  ntag.begin();

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
    NDEF_WIFIAPPL =           "application/vnd.wfa",
    NDEF_BLUEAPPL =           "application/vnd.bluetooth",
    NDEF_VCARDTEXT =          "text/vcard";

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

  String 
    NewSSID =                 "",
    NewPWD =                  "";
    
  byte EEPROMdata[16];
  

  Serial.println("");
  Serial.println(F("Reading EEPROM User Memory"));
  Serial.println(F("--------------------------"));
  
  // Check the initial block to determine if NDEF of otherwise
  Serial.println(F("Reading initial block to determine if formatted as NDEF"));
  if (ntag.readEeprom(0,EEPROMdata,16)) {
    if (EEPROMdata[0] == 3) {
      Serial.println(F("Yes Tag is formatted as NDEF"));
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
          Serial.print(F("\r\nFound an NDEF Message on NTAG with "));
          Serial.print(NoRecords);
          Serial.print(F(" NDEF Record"));
          if (NoRecords != 1) Serial.print("s");
          Serial.println(".");
          NdefRecord record = _ndefMessage->getRecord(0);
          byte TNFval = byte(record.getTnf());
          Serial.print(F("  TNF: ")); showTFN(TNFval);
          
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
                Serial.print(F("\r\nFound an NDEF Message on NTAG with "));
                Serial.print(NoRecords);
                Serial.print(F(" NDEF Record"));
                if (NoRecords != 1) Serial.print("s");
                Serial.println(".");
              

                for (int i = 0; i < NoRecords; i++) {
                  Serial.print(F("\r\nNDEF Record ")); Serial.println(i+1);
                  NdefRecord record = _ndefMessage->getRecord(i);
                  byte TNFval = byte(record.getTnf());
                  Serial.print(F("  TNF: ")); showTFN(TNFval);

                  String RTDstr = record.getType();
                  byte RTDval = 0;
                  Serial.print(F("  Type: ")); Serial.print(RTDstr); // will be "" for TNF_EMPTY
                  if (RTDstr.length() == 1) {
                    RTDval = byte(RTDstr.charAt(0));
                    Serial.print(" .[0x"); Serial.print(RTDval, HEX); Serial.println("]");
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
                      else if (RTDstr.indexOf(NDEF_BLUEAPPL) == 0) RTDval = 2;
                      else if (RTDstr.indexOf(NDEF_VCARDTEXT) == 0) RTDval = 3;
                    }
                    Serial.print(" ..[0x"); Serial.print(RTDval, HEX); Serial.println("]");
                    
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

                  // Parse the "special poster" here ---------------------------
                  if (TNFval == 1 && RTDval == 0x53) {
                    for (int c = 0; c < payloadLength; c++) {
                      if (payload[c] > 31 && payload[c] < 127) payloadAsString += (char)payload[c];
                      else {
                        payloadAsString += ("[" + String(payload[c], HEX) + "]");
                      }
                    }
                    
                  }
                  // Parse the text message here ------------------------------
                  else if (TNFval == 1 && RTDval == 0x54) {
                    // Check bit 7 of payload[0]
                    bool UTF16 = bitRead(payload[0], 7);
                    String UTFcoding = "";
                    String textLang = "";
                    if (UTF16) UTFcoding = "[utf16]";
                    else UTFcoding = "[utf8]";
                    //Now check the length of "language code"
                    byte langlen = payload[0] & 0x0F;
                    if (langlen) {
                      textLang = "[";
                      for (byte c = 0; c < langlen; c++) {
                        textLang += (char)payload[c+1];
                      }
                      textLang += "]";
                      for (int c = langlen+1; c < payloadLength; c++) {
                        payloadAsString += (char)payload[c];
                      }
                    }
                    else {
                      for (int c = 1; c < payloadLength; c++) {
                        payloadAsString += (char)payload[c];
                      }
                    }
                    Serial.print(UTFcoding); Serial.println(textLang);
                  }
                  // Parse the URL here -------------------------------------
                  else if (TNFval == 1 && RTDval == 0x55) {
                    // We check the first value
                    switch(payload[0]) {
                      case 0x01:        // HTTPWWW
                      payloadAsString = "http://www.";
                      break;
                      case 0x02:        // HTTPSWWW
                      payloadAsString = "https://www.";
                      break;
                      case 0x03:        // NDEF_HTTP
                      payloadAsString = "http://";
                      break;
                      case 0x04:        // NDEF_HTTPS
                      payloadAsString = "https://";
                      break;
                      case 0x05:        // TEL:
                      payloadAsString = "tel:";
                      break;
                      case 0x06:        // MAILTO:
                      payloadAsString = "mailto:";
                      break;
                      case 0x07:        // ftp anon@
                      payloadAsString = "ftp://anonymous:anonymous@";
                      break;
                      case 0x08:        // ftp:
                      payloadAsString = "ftp://ftp.";
                      break;
                      case 0x09:        // ftps:/
                      payloadAsString = "ftps:/";
                      break;
                      case 0x0A:        // sftp:
                      payloadAsString = "sftp://";
                      break;
                      case 0x0D:        // ftp:
                      payloadAsString = "ftp://";
                      break;
                      case 0x1D:        // file:
                      payloadAsString = "file://";
                      break;
                    }
                    
                    for (int c = 1; c < payloadLength; c++) {
                      payloadAsString += (char)payload[c];
                    }
                  }
                  // Parse the wifi data payload ---------------------------
                  else if (TNFval == 2 && RTDval == 1) {
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
                  }
                  // This is a bluetooth data payload -----------------------------------------------
                  else if (TNFval == 2 && RTDval == 2) {
                     if (payload[0] > 0) {
                      
                      // Here the il flag is 1 so the ID will be set.
                      Serial.print("  ID: ");
                      String TNFid = record.getId();
                      Serial.print(TNFid); Serial.print(" (len:");
                      Serial.print(record.getIdLength(),DEC); Serial.println(")");
                      payloadAsString = "";
                      // First 2 bytes provide the data length
                      Serial.println("[OOB data length: " + String((payload[0] | payload[1] << 8), DEC) + "]");
                      //payloadAsString = "[OOB data length: " + String((payload[0] | payload[1] << 8), DEC) + "]";
                      //payloadAsString += "[MAC: ";
                      //payloadAsString += MACHexChar(payload+2, 6);
                      //payloadAsString += "]";
                      Serial.println("[MAC: " + MACHexChar(payload+2, 6) + "]");
                      // Let's check payload[8]. Tells us a bit about the UUID's. If 0x07 then it tells us a service UUID is 128bit
                      switch (payload[8]) {
                        case 0x02:
                          Serial.println(F("[incomplete list 16-bit UUID's provided]"));
                          break;
                        case 0x03:
                          Serial.println(F("[complete list 16-bit UUID's provided]"));
                          break;
                        case 0x04:
                          Serial.println(F("[incomplete list 32-bit UUID's provided]"));
                          break;
                        case 0x05:
                          Serial.println(F("[complete list 32-bit UUID's provided]"));
                          break;
                        case 0x06:
                          Serial.println(F("[incomplete list 128-bit UUID's provided]"));
                          break;
                        case 0x07:
                          Serial.println(F("[complete list 128-bit UUID's provided]"));
                          break;
                        default:
                          Serial.println("[" + String(payload[8], HEX) + "]");
                      }
                      // Let's check payload[9]. If 0x08 then SHORT_NAME or if 0x09 then COMPLETE_NAME
                      if (payload[9] == 0x08 ) {
                        Serial.println(F("[SHORT NAME shown]"));
                      }
                      else if (payload[9] == 0x09 ) {
                        Serial.println(F("[COMPLETE NAME shown]"));
                      }
                      else {
                        Serial.println("[" + String(payload[9], HEX) + "]");
                      }
                      
                      for (int c = 10; c < payloadLength; c++) {
                        if (payload[c] > 31 && payload[c] < 127) payloadAsString += (char)payload[c];
                        else payloadAsString += ("[" + String(payload[c],HEX) + "]");
                      }
                    }
                  }
                 // This is the Vcard RTD details ------------------------------------------
                 else if (TNFval == 2 && RTDval == 3) {
                     payloadAsString = "\r\n\r\n";
                    for (int c = 0; c < payloadLength; c++) {
                      if (payload[c] > 31 && payload[c] < 127) payloadAsString += (char)payload[c];
                      else {
                        if (payload[c] == 0xd) payloadAsString += "\r";
                        else if (payload[c] == 0xa) payloadAsString += "\n";
                        else payloadAsString += ("[" + String(payload[c], HEX) + "]");
                        
                      }
                    }
                  }
                  else {
                    for (int c = 0; c < payloadLength; c++) {
                      payloadAsString += (char)payload[c];
                    }
                  }
                  Serial.print("  Payload (as String): ");
                  Serial.println(payloadAsString);
          
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

void showTFN(uint8_t tnf) {

  // For decoding the 3-bit TNF
  switch (tnf) {
    case 0x00:
      Serial.println(F("EMPTY RECORD (0x00)"));
      break;
    case 0x01:
      Serial.println(F("NFC Forum well-known type [NFC RTD] (0x01)"));
      break;
    case 0x02:
      Serial.println(F("Media-type as defined in RFC 2046 [RFC 2046] (0x02)"));
      break;
    case 0x03:
      Serial.println(F("Absolute URI as defined in RFC 3986 [RFC 3986] (0x03)"));
      break;
    case 0x04:
      Serial.println(F("NFC Forum external type [NFC RTD] (0x04)"));
      break;
    case 0x05:
      Serial.println(F("RECORD UNKNOWN (0x05)"));
      break;
    case 0x06:
      Serial.println(F("RECORD UNCHANGED - this is Part Chunked Payload (0x06)"));
      break;
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
