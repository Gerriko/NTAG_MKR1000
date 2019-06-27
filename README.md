# NTAG_MKR1000
Arduino Examples decoding different NDEF messages received via NTAG

1. NFC_Ntag_ReadEEPROM_NDEFlib.ino
This example is the generic case which will read various NDEF types and decode the detail

2. NFC_Ntag_ReadEEPROM_ConnectToWiFi.ino
This example only handles the WiFi details from NDEF. It will attempt to connect with the new details.
You need to add in connection.h in the same folder as this example. This allows for a default SSID/password to be hardcoded into the firmware.
