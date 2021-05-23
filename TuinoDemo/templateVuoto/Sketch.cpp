﻿#include <Arduino.h>
#ifndef F_CPU               // if F_CPU was not defined in Project -> Properties
#define F_CPU 8000000UL    // define it now as 8 MHz unsigned long
#endif

/*********************************************************************************************/
#include <avr/io.h>       // this is always included in AVR programs
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
/*********************************************************************************************/
#include <SPI.h>
#include <Ethernet2.h>
#include <Keypad.h>
#include <Wire.h>
#include "NFC_PN532.h"
#include <LiquidCrystal_I2C.h>
#include <DS3231M.h>
#include "PCA9534.h"
#include "my_EEPROM.h"
/*********************************************************************************************/

static const uint8_t D42 = 42;

static const uint8_t IRQ_Module = PORTB1;
static const uint8_t IRQ_TASTIERA = PORTB2;
static const uint8_t IRQ_RTC = PORTC5;
static const uint8_t IRQ_NFC = PORTD4;
/*********************************************************************************************/
static const uint8_t DTR_GSM = PORTB0;
static const uint8_t MOS_GSM = PORTD5;
static const uint8_t EN_WIFI = PORTB3;
static const uint8_t SS_LORA = PORTA4;
static const uint8_t SS_FLASH = PORTB4;
static const uint8_t CS_W5500 = PORTC4;
/*********************************************************************************************/
static const uint8_t Distributore1 = PORTD7;
static const uint8_t Distributore2 = PORTD6;
static const uint8_t PISTOLA_1 = PORTA1;
static const uint8_t PISTOLA_2 = PORTA2;
static const uint8_t BUZZER = PORTC6;
static const uint8_t ADC_IN = PORTA0;
static const uint8_t DIGITAL_OUT = PORTA3;
/*********************************************************************************************/
static const uint8_t PULSER1 = PORTA5;
static const uint8_t PULSER2 = PORTA6;
/*********************************************************************************************/
static const uint8_t RELE1 = PORTC7;
static const uint8_t RELE2 = PORTA7;
/*********************************************************************************************/
static inline void initSS_FLASH()  {
  DDRB |= (1 << PB4);  // set DDRB bit 4, sets PB4 for output
}
static inline void initSS_ETH()    {
  DDRC |= (1 << PC4);  // set DDRC bit 4, sets PC4 for output
}
static inline void enable_ETH()    {
  PORTC &= ~(1 << PC4);  // Set 0 Bit 4 PORTC Register
}
static inline void enable_FLASH()  {
  PORTB &= ~(1 << PB4);  // Set 0 Bit 4 PORTB Register
}
static inline void disable_ETH()   {PORTC |= (1 << PC4);   // Set 1 Bit 4 PORTC Register
}
static inline void disable_FLASH() {PORTB |= (1 << PB4);   // Set 1 Bit 4 PORTB Register
}
/***********************************************************************************************/

#define BIT_IS_SET(byte, bit) (byte & (1 << bit))
#define BIT_IS_CLEAR(byte, bit) (!(byte & (1 << bit)))
#define SET_BIT(byte, bit) (byte |= (1 << bit))
#define CLEAR_BIT(byte, bit) (byte &= ~(1 << bit))
#define TOGGLE_BIT(byte, bit) (byte ^= (1 << bit))

#define PN532_IRQ   (4)
#define PN532_RESET (3)  // Not connected by default on the NFC Shield
#define NUM_OF_CONSECUTIVE_PRESSES 1
#define NUM_OF_CONSECUTIVE_NON_PRESSES 2

const uint8_t I2C_PCA9534_ADDR = 0x20;

volatile int intConsecutivePresses = 0;
volatile int intConsecutiveNonPresses = 0;

struct mezzoType {
  String Carb;
  String TARGA;
  String KM;
};

struct erogazioni {
  uint16_t n;
  String erogazioni[30];
};

mezzoType mezzo;

uint8_t success;
uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };       // Buffer dove memorizzo la UID del Badge
uint8_t uidLength;                             // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
uint8_t uidMezzo[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer dove memorizzo la UID del Badge Mezzo
boolean alreadyTimbrata = false;

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address
uint8_t Clessidra[8] = {0x1F, 0x11, 0x0A, 0x04, 0x04, 0x0E, 0x1F, 0x1F,};
NFC_PN532 nfc(PN532_IRQ, PN532_RESET);

String StatoAttuale = "START...";
String strURLAPI = "";

unsigned long updatedisplayLCD = 0;

int Litri = 0;              // Variabile per il conteggio dei litri erogati
int Connected = 0;
int Autenticato = LOW;      // Semaforo Autenticazione Operatore
int InviaTarga = LOW;       // Semaforo Validazione Targa/Km Mezzo
int SgancioPistola = HIGH;  // Stato Contatto Pistola
int GO = LOW;               // Semaforo Generale
int stato_procedura = 0;    // Stato del Sistema
volatile int impulsi = 0;   // Variabile per il conteggio degli impulsi generati dal pulser

/*** GESTIONE HTTP REQUEST ***/

int HTTP_len_response = 12;
String RispostaHTTP = "";
// Ate;TARGA;CARB;LT;KM;
String RaccoltaDati[] = {"", "", "", "", "", ""};
String Carburante = "X";
String Risposta = "";
String Messaggio = "";
String righeDisplay[] = {"X", "X", "X", "X"};
char MessaggioToServer[100] = "";

/************* PULSER *************************/
/**/    int ImpulsiLitro = 50;              /**/
/**/    double debounceDelay = 4.20;        /**/   // ms  debounce time; incrementare se l'output oscilla troppo
/**/    double debounceDelayBenzina = 4.20; /**/   // ms  debounce time; incrementare se l'output oscilla troppo
/**********************************************/
char CodSede[] = "SA1001";
/********************************************************************************************/
/*                    Configurazione Rete                       */
/********************************************************************************************/

IPAddress ipCCEC(192, 168, 0, 50);
IPAddress myDns(192, 168, 1, 21); // DNS
IPAddress gateway(192, 168, 0, 1); // GATEWAY
IPAddress subnet(255, 255, 0, 0); // SUBNET

char serverATE[]  = "wbpate-test.dipvvf.it";
char serverGAC[]  = "gacweb-test.dipvvf.it";
char serverREST[] = "ccec.sa.dipvvf.it";

EthernetClient clientLOCAL;
EthernetClient clientATE;

byte mac[] = {0x00, 0x0E, 0x0C, 0xB0, 0x25, 0x6F};
/********************************************************************************************/
/************ GESTIONE RTC **********/
DS3231M_Class DS3231M;
const uint8_t SPRINTF_BUFFER_SIZE =     32;
char          inputBuffer[SPRINTF_BUFFER_SIZE];
unsigned long secs;                            // store the seconds value
unsigned long UltimoPassaggioStato = 0;        // Timer Stati Procedura
unsigned long Timer = 0;                       // Timer
DateTime nowTimer;

// Timer avanzamento stati
/********************************************************************************************/
unsigned long TverificaBadge = 60;        // 5 secondi
unsigned long TinputTarga = 60;          // 30 Secondi
unsigned long TselDistributore = 30;     // 30 Secondi
unsigned long TsgancioPistola = 60;      // 60 secondi
unsigned long TmaxErogazione = 180;      // 3 minuti
unsigned long TmaxInviodati = 10;        // 10 Secondi
unsigned long TmaxProgrammingMode = 15;  // 15 Secondi
unsigned long TmaxSalvataggio = 15;      // 15 Secondi
/********************************************************************************************/

const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns

String TARGA = "";
String KM = "";

/******** MAPPA TASTIERINO ************/

char MappaKeys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', '.'}
};

PCA9534 gpio;
/********************************************************************************************/
void InizializzaEthernet()
{
  /***********************************/
  // inizializzo ethernet MICRO W5500
  /**********************************/
  Ethernet.begin(mac, ipCCEC, myDns, gateway, subnet);
}

/************************************************************/
uint32_t addr_erog = 16101;

#define TRUE 1
#define FALSE 0

void my_delay_ms(int ms)
{
  while (0 < ms)
  {
    _delay_ms(1);
    --ms;
  }
}
/********************UTILITY x DEBUG Seriale ****************************************/
void printLine() {
  Serial.println();
  for (uint8_t i = 0; i < 125; i++) {
    Serial.print("-");
  }
  Serial.println();
}

void printTab(uint8_t _times) {
  for (uint8_t i = 0; i < _times; i++) {
    Serial.print("\t");
  }
}

void pass(bool _status) {
  printTab(1);
  Serial.print("   ");
  if (_status) {
    Serial.print("PASS");
  }
  else {
    Serial.print("FAIL");
  }
  printTab(1);
}
/************************************************************/

void setup() {

  _delay_ms(100);

  initSS_ETH();
  _delay_ms(5);
  disable_ETH();

  // Serial.begin(9600);
  Serial.println(" inizio Setup ......");

  /*******************************************************************************************/
  ParametriCCEC = Parametri;

  ErogazioniEEPROM = Erogazioni;

  Serial.begin(9600);
  Serial.print("EEPROM utilizzata (byte): ");
  Serial.print(EEPROM.length());
  Serial.println(" ");

  String app = "";

  //    if (write_eeprom_string_struct(ParametriCCEC[0])) { Serial.println("WRITE OK");}
  //    if (write_eeprom_string_struct(ParametriCCEC[1])) { Serial.println("WRITE OK");}
  //    if (write_eeprom_string_struct(ParametriCCEC[2])) { Serial.println("WRITE OK");}

  app = read_eeprom_string_struct(ParametriCCEC[0]);
  app.toCharArray(serverREST,app.length());
  
  app = read_eeprom_string_struct(ParametriCCEC[1]);
  app = read_eeprom_string_struct(ParametriCCEC[2]);
  app = read_erogazione_eeprom(ErogazioniEEPROM[0]);


  /*******************************************************************************************/
  DDRC |= (1 << BUZZER); // set BUZZER (PC6) for output
  DDRC |= (1 << RELE1);  // Rele1
  DDRA |= (1 << RELE2);  // Rele2   // set PA7 e PC7 come output
  _delay_ms(10);
  SET_BIT(PORTC, RELE1); // Apri RELE1
  _delay_ms(10);
  SET_BIT(PORTA, RELE2); // Apri RELE2
  printLine();

  /***************************LCD******************************/

  lcd.begin(20, 4);        // Inizializza display LCD 20x4 e accendi e spegni 2 volte

  // ------- 2 blinks -------------
  for (int i = 0; i < 2; i++)
  {
    lcd.backlight();
    _delay_ms(250);
    lcd.noBacklight();
    _delay_ms(250);
  }

  lcd.createChar(1, Clessidra); // Creo CHAR Clessidra
  _delay_ms(20);
  lcd.backlight();

  for (int r = 0; r < 4; r++)
    righeDisplay[r] = "";

  printLine();

  /***************************NFC*************************/

  nfc.begin();

  _delay_ms(50);

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Modulo PN532 non trovato");
    lcd.backlight();
    _delay_ms(250);
    lcd.clear();
    lcd.print("Modulo PN532 non trovato ");
    _delay_ms(2000);
    StatoAttuale  = " CHIAMA ASSISTENZA";
    //while (1); // halt  // Aspetta per sempre
  } else {
    Serial.println("Modulo NFC OK ......");
  }

  Serial.print("Found chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);

  nfc.setPassiveActivationRetries(0xFF);
  nfc.SAMConfig();
  printLine();
  /***************************SPY FLASH*************************/

  /*************************** RTC ************************/
  while (!DS3231M.begin()) {
    Serial.println(F("Unable to find DS3231MM. Checking again in 3s."));
    _delay_ms(1000);
  }
  _delay_ms(50);

  Serial.println(F("RTC chip DS3231M initialized."));
  DS3231M.adjust();
  printLine();
  /*************************** POTENZIOMETRI ************************/
  Serial.println("Inizializzo POTENZIOMETRI per livello pulser.......");

  Wire.begin(); // join i2c bus (address optional for master)

  Wire.beginTransmission(0x28);  // (0x50) POTENZIOMETRO U11
  Wire.write(byte(0x00));        // Wiper Register
  Wire.write(50);                // Valore del potenziomentro circa 6 volt
  Wire.endTransmission();

  _delay_ms(50);

  Wire.beginTransmission(0x52);  // (0x52) POTENZIOMETRO U12
  Wire.write(byte(0x00));        // Wiper Register
  Wire.write(50);                // Valore del potenziomentro circa 6 volt
  Wire.endTransmission();

  Wire.end();

  Serial.println("POTENZIOMETRI OK");
  printLine();
  /*************************KEYPAD*********************/
  gpio.begin(I2C_PCA9534_ADDR);

  // set REG IOexpander OPREG 11000011,INVREG 00000000,CONFREG 00111100
  gpio.setporteIoExp(0xC3, 0x00, 0x3C);
  /**************** SETTING INIZIALI ******************/

  stato_procedura = - 2; // set stato di partenza

  StatoAttuale = "Starting ....";

  Serial.println("Stato Iniziale" + StatoAttuale);

  printLine();
}

/********************************END SETUP ***************************************/

void Buzzer(uint8_t p_ripeti, uint32_t p_delay_suono) {

  uint32_t del = p_delay_suono;

  for (int volte = 0; volte < p_ripeti; volte++)
  {
    // TOGGLE_BIT(PORTC,BUZZER);
    my_delay_ms(p_delay_suono);
    // TOGGLE_BIT(PORTC,BUZZER);
  }
}

char getCharKeypad(int _ioexpanderByte)
{
  int r = 0;
  int c = 0;

  switch (_ioexpanderByte) {

    /**********RIGA 1*************/
    case (5): {
        Serial.print(MappaKeys[0, 0][0]);
        r = 0;
        c = 0;
      } break;
    case (9): {
        Serial.print(MappaKeys[0, 0][1]);
        r = 0;
        c = 1;
      } break;
    case (17): {
        Serial.print(MappaKeys[0, 0][2]);
        r = 0;
        c = 2;
      } break;
    case (33): {
        Serial.print(MappaKeys[0, 0][3]);
        r = 0;
        c = 3;
      } break;
    /****************************/
    /**********RIGA 2*************/
    case (6): {
        Serial.print(MappaKeys[0, 1][0]);
        r = 1;
        c = 0;
      } break;
    case (10): {
        Serial.print(MappaKeys[0, 1][1]);
        r = 1;
        c = 1;
      } break;
    case (18): {
        Serial.print(MappaKeys[0, 1][2]);
        r = 1;
        c = 2;
      } break;
    case (34): {
        Serial.print(MappaKeys[0, 1][3]);
        r = 1;
        c = 3;
      } break;
    /****************************/
    /**********RIGA 3*************/
    case (132): {
        Serial.print(MappaKeys[0, 2][0]);
        r = 2;
        c = 0;
      } break;
    case (136): {
        Serial.print(MappaKeys[0, 2][1]);
        r = 2;
        c = 1;
      } break;
    case (144): {
        Serial.print(MappaKeys[0, 2][2]);
        r = 2;
        c = 2;
      } break;
    case (160): {
        Serial.print(MappaKeys[0, 2][3]);
        r = 2;
        c = 3;
      } break;
    /****************************/
    /**********RIGA 4*************/
    case (68): {
        Serial.print(MappaKeys[0, 3][0]);
        r = 3;
        c = 0;
      } break;
    case (72): {
        Serial.print(MappaKeys[0, 3][1]);
        r = 3;
        c = 1;
      } break;
    case (80): {
        Serial.print(MappaKeys[0, 3][2]);
        r = 3;
        c = 2;
      } break;
    case (96): {
        Serial.print(MappaKeys[0, 3][3]);
        r = 3;
        c = 3;
      } break;
    /****************************/
    default: {
        return 'N';
      } break;
  }
  // _delay_ms(20);
  return MappaKeys[0, r][c];
}

void displayLCD(String righe[], int stato, int delay_lcd)                                             // get the current time             //
{
  //DateTime now = DS3231M.now();
  lcd.clear();
  //sprintf(inputBuffer,"%04d-%02d-%02d %02d:%02d:%02d", now.year(),       // Use sprintf() to pretty print    //
  //now.month(), now.day(), now.hour(), now.minute(), now.second());      // date/time with leading zeros     //
  //Serial.println(inputBuffer);                        // Display the current date/time    //
  //lcd.print(inputBuffer);

  //if (stato > 1)
  // lcd.print("Tempo: " + String((UltimoPassaggioStato+Timer-secs-1))+ " sec ");

  lcd.print((char)1);  // STAMPA LA CLESSIDRA
  lcd.setCursor(0, 1);
  lcd.print(righe[1]);
  lcd.setCursor(0, 2);
  lcd.print(righe[2]);
  lcd.setCursor(0, 3);
  lcd.print(righe[3]);

  my_delay_ms(delay_lcd);
}

void avanzaStato(unsigned long p_timer) {
  displayLCD(righeDisplay, stato_procedura, 10);
  Timer = p_timer;
  UltimoPassaggioStato = nowTimer.secondstime();
  stato_procedura++;
}

String leggiTAG_Mezzo(bool scrivi)
{
  String app;

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uidMezzo, &uidLength, 200);

  if (success) {

    Buzzer(2, 30);

    if (uidLength == 4)
    {
      uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
      success = nfc.mifareclassic_AuthenticateBlock(uidMezzo, uidLength, 4, 0, keya);

      if (success)
      {
        uint8_t data[16];

        // Se il parametro scrivi è true scriviamo sul blocco 4 del TAG
        if (scrivi)
        { memcpy(data, (const uint8_t[]) {
            '2', '8', '5', '3', '0', 'D', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
          }, sizeof data); // 1000030442
          success = nfc.mifareclassic_WriteDataBlock (4, data);
        }

        // Try to read the contents of block 4
        success = nfc.mifareclassic_ReadDataBlock(4, data);

        if (success)
        {
          Serial.println("LETTURA TARGA");

          String TargaRFID = (char*)data;

          Serial.println(TargaRFID);

          if (TargaRFID.length() == 6)
          {
            return TargaRFID;
          }
          else
          {}
          // Aspetta prima di leggere una nuova CARD NFC
          _delay_ms(100);
        }
        else
        {
          lcd.clear();
          lcd.setCursor(0, 1);
          lcd.print("Blocco KO");
        }
      }
      else
      {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Auth KO");
      }
    }
    Serial.println("");
  }
  return app;
}

String GetCodeRfidATe()
{
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;
  String Codice = "ERRORE";

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  //success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength,300);

  if (success) {

    String hexCode = nfc.GetHexCode(uid, uidLength);
    hexCode.trim();
    Codice = hexCode.substring(0, 8);
    alreadyTimbrata = true;
  }

  return Codice;
}

String GetHTTPResponseCode(String HttpResp)
{
  String Cod_Conn_KO = "CONN KO";

  printLine();
  Serial.println("VerificaHTTPResponse...");
  if (HttpResp.length() == HTTP_len_response) {
    String CodHTTP = HttpResp.substring(HttpResp.length() - 3);
    //pass(true);
    Serial.println("CodHTTP:" + CodHTTP);
    return CodHTTP;
  }
  else
  {
    pass(false);
    printLine();
    return Cod_Conn_KO;
  }
}

uint8_t GetAteValidation(int Port, char serverWEB[], EthernetClient ClientHTTP, String ATeCode)
{
  int valida = 0;

  printLine();
  printTab(1);
  Serial.print("Calling Webservice ATe ..............");

  if ( (ClientHTTP.connect(serverWEB, Port)))
  {
    strURLAPI = "GET /api/ATe/GetAssociazioneByRfidCode?rfidCode=" + ATeCode + "  HTTP/1.1\r\n";
    strURLAPI += "Host: wbpate-test.dipvvf.it";
    strURLAPI += "\r\n";
    strURLAPI += "Accept: application/json";
    strURLAPI += "\r\n";
    strURLAPI += "accept-encoding: gzip, deflate";
    strURLAPI += "\r\n";
    strURLAPI += "accept-language: en-US,en;q=0.8";
    strURLAPI += "\r\n";
    strURLAPI += "user-agent: Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) advanced-rest-client/12.1.4 Chrome/61.0.3163.100 Electron/2.0.2 Safari/537.36";
    strURLAPI += "\r\n";
    strURLAPI += "content-type: application/json";
    strURLAPI += "\r\n";

    ClientHTTP.print(strURLAPI);

    _delay_ms(80);
    ClientHTTP.println("Connection: close");
    ClientHTTP.println();
  }
  else
  {
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Connessione Fallita.");
    lcd.setCursor(0, 3);
    lcd.print("Verificare....");
    _delay_ms(1000);
  }

  _delay_ms(100);

  while (ClientHTTP.available()) {
    char c = ClientHTTP.read();
    RispostaHTTP = RispostaHTTP + c;
    if (RispostaHTTP.length() == HTTP_len_response)
    {
      String rispostaGetTimbrature = GetHTTPResponseCode(RispostaHTTP);
      _delay_ms(80);

      /******************
        lcd.clear();
        lcd.setCursor(0,2);
        lcd.print("COD HTTP:");
        lcd.print(rispostaGetTimbrature);
        *******************/

      if (rispostaGetTimbrature == "200") {
        valida = 1;
        pass(true);
      }
      _delay_ms(80);
    }
  }
  printLine();
  return valida;
}

bool PostErogazioneGAC(int Port, char serverREST[], EthernetClient ClientHTTP, String _erogazione)
{
  bool valida = false;

  printLine();
  printTab(1);

  Serial.print("Call Webservice POST Erogazione ..............");

  if ( (ClientHTTP.connect(serverREST, Port)))  // Chiamata al Rest server per interfacciamento al GAC
  {
    _delay_ms(100);
    strURLAPI = "POST /Rifornimento.php HTTP/1.1\r\n";
    strURLAPI += "Host: ccec.sa.dipvvf.it";
    strURLAPI += "\r\n";
    strURLAPI += "user-agent: Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) advanced-rest-client/12.1.4 Chrome/61.0.3163.100 Electron/2.0.2 Safari/537.36";
    strURLAPI += "\r\n";
    strURLAPI += "content-type: application/json";
    strURLAPI += "\r\n";
    strURLAPI += "Accept: */*";
    strURLAPI += "\r\n";
    //strURLAPI += "Content-Length: 37";
    strURLAPI += "Content-Length: " + String(_erogazione.length() + 21);
    strURLAPI += "\r\n";
    strURLAPI += "\r\n";
    strURLAPI += _erogazione;
    strURLAPI += "\r\n";

    ClientHTTP.print(strURLAPI);
    _delay_ms(80);
    ClientHTTP.println("Connection: close");
    ClientHTTP.println();

    printLine();
    Serial.println(strURLAPI);
  }
  else
  {
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Connessione Fallita.");
    lcd.setCursor(0, 3);
    lcd.print("Verificare....");
    _delay_ms(1000);
  }

  _delay_ms(100);

  while ((ClientHTTP.available()) && (RispostaHTTP.length() < HTTP_len_response)) {
    String rispostaGetTimbrature = "99999";
    char c = ClientHTTP.read();
    RispostaHTTP = RispostaHTTP + c;

    printLine();
    Serial.println(RispostaHTTP);

    if ((RispostaHTTP.length() == HTTP_len_response) && (rispostaGetTimbrature == "99999"))
    {
      rispostaGetTimbrature = GetHTTPResponseCode(RispostaHTTP);
      _delay_ms(80);

      if (rispostaGetTimbrature == "200") {
        valida = true;
        pass(true);
      }
      _delay_ms(80);
    }
  }
  _delay_ms(50);

  return valida;
}

uint8_t GetMezzoValidation(int Port, char serverWEB[], EthernetClient ClientHTTP, String P_Targa)
{
  int valida = 0;

  if ( (ClientHTTP.connect(serverWEB, Port)))
  {

    //_delay_ms(80);

    strURLAPI = "GET /gac-servizi/integrazione/SO115/AnagraficaMezzi/prova HTTP/1.0\r\n";
    strURLAPI += "Host: gacweb-test.dipvvf.it";
    strURLAPI += "\r\n";
    strURLAPI += "Authorization: fjhkhk";
    strURLAPI += "\r\n";
    strURLAPI += "Accept: application/json";
    strURLAPI += "\r\n";
    strURLAPI += "accept-encoding: gzip, deflate";
    strURLAPI += "\r\n";
    strURLAPI += "accept-language: en-US,en;q=0.8";
    strURLAPI += "\r\n";
    strURLAPI += "user-agent: Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) advanced-rest-client/12.1.4 Chrome/61.0.3163.100 Electron/2.0.2 Safari/537.36";
    strURLAPI += "\r\n";
    strURLAPI += "content-type: application/json";
    strURLAPI += "\r\n";

    ClientHTTP.print(strURLAPI);

    _delay_ms(80);
    ClientHTTP.println("Connection: close");
    ClientHTTP.println();
  }
  else
  {
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Connessione Fallita.");
    lcd.setCursor(0, 3);
    lcd.print("Verificare....");
    _delay_ms(1000);
  }

  _delay_ms(100);

  while (ClientHTTP.available()) {
    char c = ClientHTTP.read();
    RispostaHTTP = RispostaHTTP + c;
    if (RispostaHTTP.length() == HTTP_len_response)
    {
      String rispostaGetTimbrature = GetHTTPResponseCode(RispostaHTTP);
      _delay_ms(80);

      if (rispostaGetTimbrature == "200") {
        valida = 1;
      }
      _delay_ms(80);
    }
  }
  return valida;
}

void abilitaPulsanti() {
  /****************ABILITO PULSANTI**************/
  DDRD &= ~(1 << PD7);    // sets PD7 for input Distributore1
  DDRD &= ~(1 << PD6);    // sets PD6 for input Distributore2
  PCICR =  0b00001000;
  PCMSK3 = 0b11000000;
  sei();            // enable interrupts
  /**********************************************/
}

void abilitaContattiPistola() {
  /****************ABILITO PULSANTI**************/
  DDRA &= ~(1 << PISTOLA_1);    // sets PA1 for input
  DDRA &= ~(1 << PISTOLA_2);    // sets PA2 for input
  //PCICR =  0b00000001;
  //PCMSK0 = 0b00000110;
  //sei();            // enable interrupts
  /**********************************************/
}

void abilitaPulser(char p_carburante)
{
  PCICR =  0b00000001;

  if (p_carburante == 'D')
  {
    DDRA &= ~(1 << PA5);  // PULSER 1 clear DDRA bit 5, sets PA5 for input
    PCMSK0 = 0b00100000;  // pulser 1 PCINT5
  }
  else
  {
    DDRA &= ~(1 << PA6);  // PULSER 2 clear DDRA bit 5, sets PA5 for input
    PCMSK0 = 0b01000000;  // pulser 2 PCINT6
  }

  sei();            // enable interrupts
};

void ContaImpulsi()
{
  //if (((millis() - lastDebounceTime) > debounceDelay))
  {
    impulsi++;
    //lastDebounceTime = millis();
    _delay_ms(4);
  }
}

double impulsiToLitri(int P_impulsi)
{
  //double imp = (double)(P_impulsi-1);
  double imp = (double)(P_impulsi);
  if (imp < 0) {
    imp = 0;
  }
  double lt = (imp / ImpulsiLitro);
  double totale = lt;
  return totale;
}

void Rele_Abilitazione1(int p_azione, int p_bit) {

  DDRC |= (1 << PC7);  // Rele1

  switch (p_azione) {
    case 0: // chiudi relè
      {
        CLEAR_BIT(PORTC, PC7); // Rele1*/
        _delay_ms(50);
      }
      break;
    case 1: // apri relè
      {
        SET_BIT(PORTC, p_bit); // Rele1
        _delay_ms(50);
      }
      break;
    case 2: // chiudi e apri relè
      {
        CLEAR_BIT(PORTC, p_bit); // Rele1
        _delay_ms(300);
        SET_BIT(PORTC, p_bit); // Rele1
      }
      break;
  }
}

void Rele_Abilitazione2(int p_azione, int p_bit) {

  DDRA |= (1 << PA7);  // Rele2

  switch (p_azione) {
    case 0:
      {
        CLEAR_BIT(PORTA, p_bit); // Rele2
        _delay_ms(50);
      }
      break;
    case 1:
      {
        SET_BIT(PORTA, p_bit); // Rele2
        _delay_ms(50);
      }
      break;
    case 2:
      {
        CLEAR_BIT(PORTA, p_bit); // Rele2
        _delay_ms(300);
        SET_BIT(PORTA, p_bit); // Rele2
      }
      break;
  }
}

void Control_WIFI(int azione) {
  DDRB |= (1 << PB3);  // set DDRB bit 3,  sets PB3 for output
  _delay_ms(30);
  if (azione == 1) {
    SET_BIT(PORTB, 3);
  } else if (azione == 0) {
    CLEAR_BIT(PORTB, 3);
  }
}

void Azzera()
{
  RispostaHTTP = "";
  impulsi = 0;
  alreadyTimbrata = false;

  Carburante = "X";

  Rele_Abilitazione1(1, 7);
  Rele_Abilitazione2(1, 7);
  Control_WIFI(0);

  //clientToServizio.flush();
  //clientToServizio.stop();

  _delay_ms(5);
  clientATE.flush();
  clientATE.stop();
  _delay_ms(5);
  clientLOCAL.flush();
  clientLOCAL.stop();
  Connected = false;
  _delay_ms(5);
  enable_FLASH();
  _delay_ms(5);
  disable_FLASH();
  _delay_ms(5);
  disable_ETH();
  _delay_ms(5);
  enable_ETH();

  SET_BIT(PORTA, A1);
  SET_BIT(PORTA, A2);

  righeDisplay[1] =  "";
  righeDisplay[2] =  "";
  righeDisplay[2] =  "";

  lcd.noBacklight();
  lcd.noDisplay();
  secs = 0;
  UltimoPassaggioStato = 0;

  printLine();
  Serial.println("Azzera....... OK");
  printLine();
  TARGA = "";
  stato_procedura = -2;
}

void inputTarga(char T) {

  switch (T) {
    case ('N'): {
        // Serial.print("NIENTE");
      }
      break;
    case ('A'): {
        TARGA = "";
        righeDisplay[1] =  "AVVICINA TAG MEZZO"; // Set display per stato successivo
        righeDisplay[2] =  "";
        righeDisplay[3] = "";
        avanzaStato(65);
      }
      break;
    case ('B'): {
        String mezzoString = leggiTAG_Mezzo(false); // con TRUE scrive sul blocco 4 della card NFC DEL MEZZO
        //String mezzoString = leggiTAG_Mezzo(true); // con TRUE scrive sul blocco 4 della card NFC DEL MEZZO
        _delay_ms(10);
      }
      break;
    case ('C'): {
        if (TARGA.length() > 0) {
          TARGA = TARGA.substring(0, TARGA.length() - 1);
          righeDisplay[1] =  "****** TARGA ******";
          righeDisplay[2] = "TARGA:" + TARGA;
          righeDisplay[3] = "#:Conferma A:Usa TAG";
          displayLCD(righeDisplay, stato_procedura, 10);
        }
      }
      break;
    case ('#'): {
        if (TARGA.length() == 5) {
          mezzo.TARGA = TARGA;
          RaccoltaDati[1] = mezzo.TARGA;
          avanzaStato(65);
        }
      }
      break;
    default:  {
        TARGA += String(T);
        // _delay_ms(20);
        righeDisplay[1] =  "****** TARGA ******";
        righeDisplay[2] = "TARGA:" + TARGA;
        righeDisplay[3] = "#:Conferma A:Usa TAG";
        displayLCD(righeDisplay, stato_procedura, 10);
      }
      break;
  }
}

void inputKM(char T) {

  switch (T) {
    case ('N'): {
        //  Serial.print("NIENTE");
      }
      break;
    case ('C'): {
        if (KM.length() > 0)
          KM = KM.substring(0, KM.length() - 1);
        righeDisplay[1] =  "****** KM ******";
        righeDisplay[2] = "KM:" + KM;
        righeDisplay[3] = "#:Conferma";
        displayLCD(righeDisplay, stato_procedura, 10);
      }
      break;
    case ('#'): {
        if (KM.length() == 4) {
          mezzo.KM = KM;
          RaccoltaDati[4] = mezzo.KM;
          righeDisplay[1] = "LITRI : 0.00";
          righeDisplay[2] = "imp :" + String(impulsi);
          righeDisplay[3] = "Erogazione: " + StatoAttuale;
          /*****************************************************************/
          disable_ETH();
          _delay_ms(2);
          enable_ETH();
          /*****************************************************************/
          avanzaStato(120);
        }
      }
      break;
    default:  {
        KM += String(T);
        //_delay_ms(20);
        righeDisplay[1] =  "****** KM ******";
        righeDisplay[2] = "KM:" + KM;
        righeDisplay[3] = "#:Conferma";
        displayLCD(righeDisplay, stato_procedura, 10);
      }
      break;
  }
}

String read_eeprom_string_struct(ParametriCCEC_TypeDef dato) {

  String Salvata = "OK";
  int lunBuffer = dato.da_memorizzare.length();
  uint8_t buf[lunBuffer];
  int i = 0;

  for (int ind = dato.startIND ; ind < (dato.startIND + lunBuffer); ind++) {
    buf[i] = EEPROM.read(ind);
    //if (buf[i] != 0) {
    //  Serial.print(" " +  String(buf[i]));
    // }
    i++;
  }

  Serial.println(" ");
  Serial.println(" ");
  Salvata = String((char *)buf);
  Serial.print(dato.descrizione + ": ");
  Serial.println(Salvata);
  return Salvata;
}


bool write_eeprom_string_struct(ParametriCCEC_TypeDef dato) {

  int lunBuffer = dato.da_memorizzare.length();
  char buf[lunBuffer];
  dato.da_memorizzare.toCharArray(buf, lunBuffer + 1);
  bool out = false;
  int i = 0;

  Serial.println(" len: " + String(lunBuffer));
  Serial.println("Eseguo scrittura nella EEPROM");
  Serial.println(" ");

  for (int ind = dato.startIND ; ind < (dato.startIND + lunBuffer); ind++) {
    if (buf[i] != 0) {
      EEPROM.write(ind, buf[i]);
      //Serial.print(" " + String(buf[i]));
    }
    i++;
  }
  Serial.println(" ");

  return true;
}

bool write_erogazione_eeprom(Erogazioni_TypeDef dato) {

  int lunBuffer = dato.da_memorizzare.length();
  char buf[lunBuffer];
  dato.da_memorizzare.toCharArray(buf, lunBuffer + 1);
  bool out = false;
  int i = 0;

  Serial.println(" len: " + String(lunBuffer));
  Serial.println("Eseguo scrittura nella EEPROM");
  Serial.println(" ");

  for (int ind = dato.startIND ; ind < (dato.startIND + lunBuffer); ind++) {
    if (buf[i] != 0) {
      EEPROM.write(ind, buf[i]);
      //Serial.print(" " + String(buf[i]));
    }
    i++;
  }
  Serial.println(" ");

  return true;
}

String read_erogazione_eeprom(Erogazioni_TypeDef dato) {

  String Salvata = "OK";
  int lunBuffer = dato.da_memorizzare.length();
  uint8_t buf[lunBuffer];
  int i = 0;

  for (int ind = dato.startIND ; ind < (dato.startIND + lunBuffer); ind++) {
    buf[i] = EEPROM.read(ind);
    //if (buf[i] != 0) {
    //  Serial.print(" " +  String(buf[i]));
    // }
    i++;
  }

  Serial.println(" ");
  Serial.println(" ");
  Salvata = String((char *)buf);
  Serial.println(Salvata);
  return Salvata;
}

/**************************LOOP PROCEDURA************************************/
void loop() {

  switch (stato_procedura) {
    case -2:
      { //cli(); // disable interrupt

        righeDisplay[1] =  "";
        righeDisplay[2] = "Setting....";
        righeDisplay[3] =  "";

        displayLCD(righeDisplay, stato_procedura, 100);
        _delay_ms(200);
        stato_procedura++;
      }
      break;
    case -1:
      {
        abilitaPulsanti();
        _delay_ms(20);
        abilitaContattiPistola();
        stato_procedura++;
      }
      break;
    case 0:
      {
        righeDisplay[1] =  "";
        righeDisplay[2] =  "";
        righeDisplay[3] =  "";

        //displayLCD(righeDisplay,stato_procedura,100);
        _delay_ms(2000);
        alreadyTimbrata = false;

        enable_ETH();

        /************************************************/
        righeDisplay[1] = " * AUTENTICAZIONE *";
        righeDisplay[2] = "";
        righeDisplay[3] = "    Avvicina ATE  ";

        displayLCD(righeDisplay, stato_procedura, 100);
        /************************************************/

        TinputTarga = 65;
        stato_procedura++;
        _delay_ms(1000);
      }
      break;
    case 1:
      {
        TARGA = "";
        KM = "";

        /*****************************************
          String ATe = "AABBCCDD";
          RaccoltaDati[0] = ATe;
          lcd.backlight();
          lcd.display();
          _delay_ms(10);
          avanzaStato(TinputTarga);
          /*****************************************/

        /*****************************************************/
        String ATe = "ERRORE";
        if (!alreadyTimbrata) {
          ATe = GetCodeRfidATe();
          Buzzer(2, 100);
        }

        if ((ATe != "ERRORE") && (BIT_IS_CLEAR(PORTC, 4)))
        {
          Serial.println("");
          Serial.print("***************************************************************");
          Serial.println(" Tessera ID : " + ATe);
          Serial.print("***************************************************************");
          Serial.println("Riconoscimento Tessera .............");

          //RaccoltaDati[0] = ATe;
          RaccoltaDati[0] = "DD92743A";
          RaccoltaDati[5] = "000";

          lcd.backlight();
          lcd.display();
          _delay_ms(10);

          righeDisplay[1] = "  RICONOSCIMENTO ";
          righeDisplay[2] = ".....In Corso.....";
          //righeDisplay[3] = "   Rfid: " + ATe;
          righeDisplay[3] = "Attendere.........";

          displayLCD(righeDisplay, stato_procedura, 100);

          InizializzaEthernet();

          _delay_ms(1000); // tempo per inizializzare la ethernet
        }

        // Effettua chiamata REST per validare CARD NFC

        righeDisplay[1] =  "****** TARGA ******";
        righeDisplay[2] = "TARGA:";
        righeDisplay[3] = "#:Conferma A:Avanti";

        avanzaStato(65);

        /*      if (1) //(GetAteValidation(80,serverATE,clientATE,ATe))
              {
                RaccoltaDati[5] = "000";
                SET_BIT(PORTC,PC4);
                Buzzer(1,400);
                //displayLCD(righeDisplay,stato_procedura,10);
                _delay_ms(50);
                avanzaStato(TinputTarga);
              }
              else
              {
                RaccoltaDati[5] = "111";
                Buzzer(3,200);
                //displayLCD(righeDisplay,stato_procedura,10);
                _delay_ms(50);
                avanzaStato(TinputTarga);
                //Azzera();
              }
        */
      }
      break;
    case 2:
      {
        lcd.setCursor(0, 0);
        lcd.print((char)1);  // STAMPA LA CLESSIDRA
        lcd.print("Tempo: " + String((UltimoPassaggioStato + Timer - secs - 1)) + " sec ");

        /*****************************************************************/
        // da commentare
        // Carburante = "D"; // Simulo Abilitazione Diesel
        // da commentare
        // Carburante = "B"; // Simulo Abilitazione Benzina
        /*****************************************************************/
        gpio.setCONFREG(0x3C);
        uint8_t c = gpio.Read_IP_REGISTER();
        char buf[8];
        itoa(c, buf, 2);
        gpio.setCONFREG(0xC3);
        uint8_t r = gpio.Read_IP_REGISTER();
        char bufr[8];
        itoa(r, bufr, 2);
        char ris[8];
        uint8_t z = (r ^ c);
        itoa(z, ris, 2);
        char T = getCharKeypad(int(z));
        _delay_ms(20);
        /*****************************************************************/

        inputTarga(T);
      }
      break;
    case 3:
      {
        lcd.setCursor(0, 0);
        lcd.print((char)1);  // STAMPA LA CLESSIDRA
        lcd.print("Tempo: " + String((UltimoPassaggioStato + Timer - secs - 1)) + " sec ");

        if (TARGA.length() == 5)
        {
          mezzo.Carb = "X";
          mezzo.TARGA = TARGA;
          mezzo.KM = "0";

          righeDisplay[1] =  "****** DISTRIBUTORE ******";
          righeDisplay[2] =  "";
          righeDisplay[3] = "**** SCEGLI ****";

          avanzaStato(TselDistributore);
        }
        else {
          //String mezzoString = leggiTAG_Mezzo(false); // con TRUE scrive sul blocco 4 della card NFC DEL MEZZO
          String mezzoString = leggiTAG_Mezzo(true); // con TRUE scrive sul blocco 4 della card NFC DEL MEZZO
          _delay_ms(10);

          Serial.println(mezzoString);

          mezzo.Carb = mezzoString.substring(5);
          mezzo.TARGA = mezzoString.substring(0, 5);
          mezzo.KM = "0";

          Serial.println("TIPO CARBURANTE: " + mezzo.Carb);
          Serial.println("TARGA: " + mezzo.TARGA);

          Carburante = mezzo.Carb;
          if ((mezzo.Carb == "B") || (mezzo.Carb == "D")) {
            RaccoltaDati[1] = mezzo.TARGA;
            RaccoltaDati[2] = mezzo.Carb;
            righeDisplay[3] = "TARGA: " +  mezzo.TARGA;
            displayLCD(righeDisplay, stato_procedura, 10);
            _delay_ms(500);
            avanzaStato(TselDistributore);
          }
        }
      }
      break;
    case 4:
      {
        righeDisplay[1] =  "****** DISTRIBUTORE ******";
        righeDisplay[2] =  "";
        righeDisplay[3] = "**** SCEGLI ****";

        //displayLCD(righeDisplay,stato_procedura,100);

        // Verifica scelta distributore

        if (mezzo.Carb == "B")
        {
          abilitaPulser('B');
          Rele_Abilitazione2(0, 7); // chiudi relè
          StatoAttuale = "POMPA 2";
          RaccoltaDati[2] = mezzo.Carb;

          righeDisplay[1] =  "****** KM ******";
          righeDisplay[2] = "KM:" + KM;
          righeDisplay[3] = "#:Conferma";
          avanzaStato(60);
        }
        else if (mezzo.Carb == "D")
        {
          abilitaPulser('D');
          Rele_Abilitazione1(0, 7); // chiudi relè
          StatoAttuale = "POMPA 1";
          RaccoltaDati[2] = mezzo.Carb;

          righeDisplay[1] =  "****** KM ******";
          righeDisplay[2] = "KM:" + KM;
          righeDisplay[3] = "#:Conferma";
          avanzaStato(60);
        }
      }
      break;
    case 5:
      {
        lcd.setCursor(0, 0);
        lcd.print((char)1);  // STAMPA LA CLESSIDRA
        lcd.print("Tempo: " + String((UltimoPassaggioStato + Timer - secs - 1)) + " sec ");

        // RaccoltaDati[4] = "1234";
        /*****************************************************************/
        gpio.setCONFREG(0x3C);
        uint8_t c = gpio.Read_IP_REGISTER();
        char buf[8];
        itoa(c, buf, 2);
        gpio.setCONFREG(0xC3);
        uint8_t r = gpio.Read_IP_REGISTER();
        char bufr[8];
        itoa(r, bufr, 2);
        char ris[8];
        uint8_t z = (r ^ c);
        itoa(z, ris, 2);
        char T = getCharKeypad(int(z));
        _delay_ms(20);
        /*****************************************************************/

        inputKM(T);
        impulsi = 0;
      }
      break;
    case 6:
      {
        lcd.setCursor(0, 0);
        lcd.print((char)1);  // STAMPA LA CLESSIDRA
        lcd.print("  Tempo: " + String((UltimoPassaggioStato + Timer - secs - 1)) + " sec ");

        /*****************************************************************
          disable_ETH();
          _delay_ms(2);
          enable_ETH();
          /*****************************************************************/

        double lt = impulsiToLitri(impulsi);

        righeDisplay[1] = "LITRI :" + String(lt);
        righeDisplay[2] = "imp :" + String(impulsi);
        righeDisplay[3] = "Erogazione: " + StatoAttuale;

        lcd.setCursor(0, 1);
        lcd.print(righeDisplay[1]);
        lcd.setCursor(0, 2);
        lcd.print(righeDisplay[2]);
        lcd.setCursor(0, 3);
        lcd.print(righeDisplay[3]);

        // CONTATTO PISTOLA DIESEL

        if ((PINA & _BV(PA1)) && (Carburante == "D"))
        {
          RaccoltaDati[3] = String(lt);
          StatoAttuale = "STOP EROGAZIONE";
          Rele_Abilitazione2(1, 7); //  apri relè
          Rele_Abilitazione1(1, 7); //  apri relè
          TOGGLE_BIT(PORTA, 1);
          avanzaStato(10);
        }

        // CONTATTO PISTOLA BENZINA

        if  ((PINB & _BV(PB1)) && (Carburante == "B"))
        {
          RaccoltaDati[3] = String(lt);

          StatoAttuale = "STOP EROGAZIONE";
          Rele_Abilitazione2(1, 7); //  apri relè
          Rele_Abilitazione1(1, 7); //  apri relè
          avanzaStato(TmaxInviodati);
        }


      }
      break;
    case 7 :
      {
        righeDisplay[1] =  "";
        righeDisplay[2] = "Invio........";
        righeDisplay[3] =  "";

        if (BIT_IS_CLEAR(PORTC, 4))
        {
          displayLCD(righeDisplay, stato_procedura, 10);

          Messaggio = "";

          for (int k = 0; k < 6; k++)
            Messaggio.concat(RaccoltaDati[k] + ";");

          Messaggio.concat(CodSede);
          Serial.println("Messaggio:" + Messaggio);

          // Messaggio = "DD92743A;28530;D;15.03;1234;000;SA10012";

          _delay_ms(1000);

          //if (PostErogazione(5001,serverREST,clientLOCAL,Messaggio))
          if (PostErogazioneGAC(80, serverREST, clientLOCAL, Messaggio))
          {
            disable_ETH();
            righeDisplay[1] = "";
            righeDisplay[2] = " Dati Inviati ";
            righeDisplay[3] =  "";
            displayLCD(righeDisplay, stato_procedura, 100);
            _delay_ms(20);
            Serial.println("PostErogazioneGAC - OK" );
          }
          else
          {
            ErogazioniEEPROM[0].da_memorizzare = Messaggio;
            if (write_erogazione_eeprom(ErogazioniEEPROM[0])) {
              Serial.println("WRITE OK");
            }
            int kkk = 0;
            while ((!PostErogazioneGAC(80, serverREST, clientLOCAL, Messaggio)) && (kkk < 5)) {
              kkk++;
              Serial.println("TENTATIVO " + String(kkk));
            }
            Serial.println("PostErogazioneGAC - KO" );
          }
          disable_ETH();
          avanzaStato(TmaxSalvataggio);
        }
      }
      break;
    case 8:
      {
        Azzera();
      }
      break;
    case 9:
      {
        Azzera();
      }
      break;
    case 100:
      {
      }
      break;
    case 101:
      {
      }
      break;
  }

  nowTimer = DS3231M.now();
  secs = nowTimer.secondstime();
  if ((UltimoPassaggioStato + Timer - secs) <= 1) Azzera();

}

/********************FINE LOOP PROCEDURA************************************/

// interrupt per conteggio impulsi
/***********************************************************************
  Esempio
  100 impulsi/litro
  73 litri al minuto
  73/60 = 1,22 lt/sec
  1,22*100 = 122 Hz
***********************************************************************/

ISR(PCINT0_vect) {
  if (PINA & _BV(PA5)) {
    impulsi++;
    _delay_ms(8.2);
  }
  if (PINA & _BV(PA6)) {
    impulsi++;
    _delay_ms(8.2);
  }
}
/***********************************************************************/

// interrupt per pulsanti abilitazione diesele benzina

ISR(PCINT3_vect) {

  if (stato_procedura == 4)
  {
    if (PIND & _BV(PD6))
    {
      intConsecutivePresses++;                      // increment counter for number of presses
      if (intConsecutivePresses >= NUM_OF_CONSECUTIVE_PRESSES)
      { // if enough presses to constitute a press
        abilitaPulser('B');
        Rele_Abilitazione2(0, 7); // chiudi relè
        Carburante = "B";
        RaccoltaDati[2] = Carburante;
        StatoAttuale = "POMPA 2";
        righeDisplay[1] =  "****** KM ******";
        righeDisplay[2] = "KM:";
        righeDisplay[3] = "#:Conferma";
        avanzaStato(10);
        intConsecutivePresses = 0;                    // and reset press counts
        intConsecutiveNonPresses = 0;
      }
    }
    else  {           // else if button is not pressed (logic low)
      intConsecutiveNonPresses++;
      if (intConsecutiveNonPresses >= NUM_OF_CONSECUTIVE_NON_PRESSES) {
        intConsecutivePresses = 0;                      // reset press counts
        intConsecutiveNonPresses = 0;
      }
    }

    if (PIND & _BV(PD7))
    {
      intConsecutivePresses++;                      // increment counter for number of presses
      if (intConsecutivePresses >= NUM_OF_CONSECUTIVE_PRESSES)
      { // if enough presses to constitute a press
        abilitaPulser('D');
        Rele_Abilitazione1(0, 7); // chiudi relè
        Carburante = "D";
        RaccoltaDati[2] = Carburante;
        StatoAttuale = "POMPA 1";
        righeDisplay[1] =  "****** KM ******";
        righeDisplay[2] = "KM:";
        righeDisplay[3] = "#:Conferma";
        avanzaStato(10);
        intConsecutivePresses = 0;                    // and reset press counts
        intConsecutiveNonPresses = 0;
      }
    }
    else  {            // else if button is not pressed (logic low)
      intConsecutiveNonPresses++;
      if (intConsecutiveNonPresses >= NUM_OF_CONSECUTIVE_NON_PRESSES) {
        intConsecutivePresses = 0;                     // reset press counts
        intConsecutiveNonPresses = 0;
      }
    }
  }
}