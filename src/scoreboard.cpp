#include "WiFi.h"
#include "ESPmDNS.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"

/***************************************************************************/
/******************************** DEFINES **********************************/
#define MAX_CONNECTIONS 1
#define ALARM_MAX_LEN 5
#define DEBUG 0

//#define DAC_ALARM
#ifdef DAC_ALARM
#include "driver/dac.h"
#include "audio.h"
#else
#include "pins_arduino.h"
#endif

/***************************************************************************/
/******************************** GLOBAL ***********************************/
const byte DATA_FRAME_ROWS = 20;   // Filas de la matriz dataFrame a enviar a placa controladora. Filas -> header, comando, dato, ...
#ifdef DAC_ALARM
const byte DAC_SAMPLE_VALUES = 100;
extern unsigned int bell_data_length;
extern unsigned char bell_data[];
unsigned int SampleIdx = 0;   // Para DAC en la interrupcion de Timer_1
const int TICKS_FOR_ALARM_TIMER = 312; // TICKS_FOR_ALARM_TIMER = 80MHz / (wav_file_sample_rate / 8)
#else
const int TICKS_FOR_ALARM_TIMER = 312;
const byte ALARM_PIN = 18;
#endif
const byte TX_MAX_LENGHT = 50;
const byte DOT_VALUE = 0x80;
const int SECOND_IN_MICROS = 1000000;
const byte DECREASE = 0;
const byte INCREASE = 1;
const byte LOCAL = 0;
const byte VISITOR = 1;
bool timerValueUpdate = false;
bool cmdReceived = false, newBoardConfig = false;

// AP - WiFi
//IPAddress local_IP(192, 168, 1, 5);
//IPAddress gateway(192, 168, 1, 1);
//IPAddress subnet(255, 255, 255, 0);
const char *ssid = "PoloPoints";
const char *dnsDomain = "polopoints";
const char *password = "12345678";
AsyncWebServer server(80);

/***************************************************************************/
/********************************* TIMER ***********************************/
hw_timer_t *Timer0_cfg = NULL;
hw_timer_t *Timer1_cfg = NULL;

/***************************************************************************/
/****************************** DATA TYPES *********************************/
struct alarm_t{
  bool active = false;
  byte bell_count = 1;        // para señal tipo DAC
  byte secondsActive = 0;     // para señal con bocina
} alarm_obj;

struct _timer_t{
  int mm;
  int ss;
};

struct scoreboard_t{
  int score[2] = {0, 0};
  int chukker = 1;
  struct __timer_t
  {
    _timer_t value = { 7,00 };
    _timer_t initValue = { 7,00 };    // se usa para el comando reset_timer
    _timer_t halftime = { 3,0 };      // 3' 0'' de descanso en intervalos
    _timer_t extendedtime = { 0,30 };  // 30'' de tiempo exetendido
  } timer;
  byte brightness = 0x0A;
  byte alarmLen = 0x01;
  byte alarmEnabled = 1;
} scoreboard;

enum main_state_t{
  IDLE,
  REFRESH_SCOREBOARD,
  INIT
};
main_state_t main_state = INIT;

enum timer_state_t{
  STOPPED,
  RUNNING
};
timer_state_t timer_state = STOPPED;

enum game_state_t{
  IN_PROGRESS,
  EXTENDED_TIME,
  HALFTIME
};
game_state_t game_state = IN_PROGRESS;

enum command_t{
  INC_SCORE_LOCAL     = 1,
  INC_SCORE_VISITOR   = 2,  
  DEC_SCORE_LOCAL     = 3,
  DEC_SCORE_VISITOR   = 4,
  INC_CHUKKER         = 5,
  DEC_CHUKKER         = 6,
  START_TIMER         = 7,
  STOP_TIMER          = 8,
  RESET_CHUKKER       = 9,
  SET_CURRENT_TIMER   = 10,
  SET_DEFAULT_TIMER   = 11,
  SET_EXTENDED_TIMER  = 12,
  SET_HALFTIME_TIMER  = 13,
  RESET_ALL           = 14,
  SET_CONFIG          = 15,
  GET_CONFIG          = 16,
  TRIGGER_HORN        = 17
};

enum request_status_t{
  STATUS_OK = 200,
  STATUS_ACCEPTED = 202,
  STATUS_BAD_REQUEST = 400,
  STATUS_NOT_FOUND = 404
};

enum data_frame_index_t{
  HEADER,
  COMMAND,
  ADDRESS,
  RESPONSE,
  RESERVED_1,
  FLASH,
  RESERVED_2,
  RESERVED_3,
  SCORE_LOCAL_DECENA,
  SCORE_LOCAL_UNIDAD,
  TIMER_MM_DECENA,
  TIMER_MM_UNIDAD,
  CHUKKER,
  TIMER_SS_DECENA,
  TIMER_SS_UNIDAD,
  SCORE_VISITOR_DECENA,
  SCORE_VISITOR_UNIDAD,
  DATA_END,
  CHECKSUM,
  FRAME_END
};

/***************************************************************************/
/***************************** DECLARATIONS ********************************/
byte genChecksum(byte *dataFrame, byte offset, byte end);
void setDataFrame(scoreboard_t *scoreboard, byte *dataFrame);
unsigned int setBufferTx(byte *bufferTx, byte *dataFrame);
void startAlarm();
void stopAlarm();
timer_state_t refreshTimer();
void startTimer();
void stopTimer();
bool setTimerValue(int mm, int ss, int action);
void resetChukker();
void updateScores(int action, int team);
void updateChukker(int action);
void resetScoreboard();
void setDataFrameHeaders(byte *dataFrame);
void refreshScoreboard(scoreboard_t *scoreboard, byte *dataFrame, byte *bufferTx);
String getScoreboard_toString(void);
String getConfig_toString(void);
bool updateAppConfig(int a_en, int a_len, int ch_mm, int ch_ss, int et_mm, int et_ss, int hf_mm, int hf_ss);
bool updateBoardConfig(int l_bri);
void scoreboard_setConfigs (scoreboard_t *scoreboard);

/***************************************************************************/
/****************************** IRQ_HANDLERS *******************************/
void IRAM_ATTR Timer0_ISR()
{
  if(timer_state == RUNNING) timerValueUpdate = true;
}

void IRAM_ATTR Timer1_ISR()
{
  #ifdef DAC_ALARM
    // Setear valor del DAC segun tabla de valores de señal senoidal
    dac_output_voltage(DAC_CHANNEL_1, bell_data[SampleIdx++]);
    if(SampleIdx >= bell_data_length){
      if(alarm_obj.bell_count >= scoreboard.alarmLen) stopAlarm();
      else{
        alarm_obj.bell_count++;
        SampleIdx = 0;
      }
    }
  #else
    if(++alarm_obj.secondsActive == scoreboard.alarmLen){
      stopAlarm();
      alarm_obj.secondsActive = 0;
    }
  #endif
}

/**************************************************************** SETUP ****************************************************************/
void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);

  // Timer 0: timer del tablero
  Timer0_cfg = timerBegin(0, 80, true);
  timerAttachInterrupt(Timer0_cfg, &Timer0_ISR, true);
  timerAlarmWrite(Timer0_cfg, SECOND_IN_MICROS, true);
  timerAlarmEnable(Timer0_cfg);

  // Alarma: (DAC + Timer 1) o (GPIO + Timer1) dependiendo del tipo del alarma
  #ifdef DAC_ALARM
    Timer1_cfg = timerBegin(1, 8, true);
    timerAlarmWrite(Timer1_cfg, TICKS_FOR_ALARM_TIMER, true);    // timer_interrup 1MHz -> DAC 100KSPS (con arreglo de 100 muestras)
    dac_output_disable(DAC_CHANNEL_1);
  #else
    pinMode(ALARM_PIN, OUTPUT);
    digitalWrite(ALARM_PIN, HIGH);
    Timer1_cfg = timerBegin(1, 80, true);
    timerAlarmWrite(Timer1_cfg, SECOND_IN_MICROS, true);
  #endif
  timerAttachInterrupt(Timer1_cfg, &Timer1_ISR, true);

  timerAlarmEnable(Timer1_cfg);
  // Se inhabilitan componentes, solo se habilitan en determinado momento
  timerStop(Timer1_cfg);

  /****************************** WIFI - AP **********************************/
  if(DEBUG) Serial.println("Setting WiFi App Mode ...");
  WiFi.mode(WIFI_AP);
  //WiFi.softAPConfig(local_IP, gateway, subnet); // Antes no ocasionaba errores, pero ahora genera reconexiones constantemente
  if (!WiFi.softAP(ssid, password, 1, false, MAX_CONNECTIONS)){
    if(DEBUG) Serial.println("\nError setting WiFi App. Rebooting ...");
    ESP.restart();
  }

  // Set DNS server
  if(!MDNS.begin(dnsDomain)){
    if(DEBUG) Serial.println("\nError setting DNS server. Rebooting ...");
    ESP.restart();
  }
  
  // Inicializar SPIFFS
  if (!SPIFFS.begin(true)){
    if(DEBUG){ Serial.println("Something went wrong mountint SPIFFS."); }
    return;
  }

  //MDNS.addService("http", "tcp", 80);
  /***************************************************************************/
  /***************************** HTTP REQUEST ********************************/
  // Ruta a index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false);
  });

  // Ruta a style.css
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });

  // Ruta a index.js
  server.on("/index.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.js", "text/html");
  });

  // Ruta a worker.js
  server.on("/worker.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/worker.js", "text/html");
  });

  // Timer
  server.on("/timer/set", HTTP_GET, [](AsyncWebServerRequest * request){
    const int paramQty = request->params();
    String data;
    if(DEBUG){ Serial.println("Request to set timer value."); }
    if(paramQty < 3){
      if(DEBUG){ Serial.println("Not enought parameters."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    if(!(request->hasParam("mm") && request->hasParam("ss") && request->hasParam("cmd"))){
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    AsyncWebParameter* p_0 = request->getParam("mm");  
    AsyncWebParameter* p_1 = request->getParam("ss");
    AsyncWebParameter* p_2 = request->getParam("cmd");
    int mm = (p_0->value()).toInt();
    int ss = (p_1->value()).toInt();
    int cmd = (p_2->value()).toInt();
    if(setTimerValue(mm, ss, cmd)){
      if(DEBUG){ Serial.println("Timer updated."); }
      data = getScoreboard_toString();
      request->send(STATUS_ACCEPTED, "text/plain", data);
      cmdReceived = true;
    }
    else{
      if(DEBUG){ Serial.println("Error: timer not updated."); }
      request->send(STATUS_BAD_REQUEST);
    }
  });
  
  server.on("/timer", HTTP_GET, [](AsyncWebServerRequest *request){
    const int paramQty = request->params();
    String data;
    if(paramQty < 1){
      if(DEBUG){ Serial.println("Not enought parameters."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    if(!(request->hasParam("cmd"))){
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    AsyncWebParameter* p_0 = request->getParam("cmd");
    int cmd = (p_0->value()).toInt();
    if(cmd == START_TIMER){
      if(DEBUG){ Serial.println("Request to start the timer."); }
      startTimer();
      cmdReceived = true;
      request->send(STATUS_ACCEPTED);
    }
    else if(cmd == STOP_TIMER){
      if(DEBUG){ Serial.println("Request to stop the timer."); }
      stopTimer();
      cmdReceived = true;
      request->send(STATUS_ACCEPTED);
    }
    else{
      if(DEBUG){ Serial.println("Parameter error -> cmd"); }
      request->send(STATUS_BAD_REQUEST);
    }
  });
  
  // Puntajes
  server.on("/score", HTTP_GET, [](AsyncWebServerRequest *request){
    const int paramQty = request->params();
    String data;
    if(paramQty < 1){
      if(DEBUG){ Serial.println("Not enought parameters."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    if(!(request->hasParam("cmd"))){
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    AsyncWebParameter* p_0 = request->getParam("cmd");
    int cmd = (p_0->value()).toInt();
    if(cmd == INC_SCORE_VISITOR){
        if(DEBUG){ Serial.println("Request to increase visitor score."); }
        updateScores(INCREASE, VISITOR);
    }
    else if(cmd == INC_SCORE_LOCAL){
      if(DEBUG){ Serial.println("Request to increase local score."); }
      updateScores(INCREASE, LOCAL);
    }
    else if(cmd == DEC_SCORE_VISITOR){
      if(DEBUG){ Serial.println("Request to decrease visitor score."); }
      updateScores(DECREASE, VISITOR);
    }
    else if(cmd == DEC_SCORE_LOCAL){
      if(DEBUG){ Serial.println("Request to decrease local score."); }
      updateScores(DECREASE, LOCAL);
    }
    else{
      if(DEBUG){ Serial.println("Parameter error -> cmd"); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    cmdReceived = true;
    data = getScoreboard_toString();
    request->send(STATUS_ACCEPTED, "text/plain", data);
  });

  // Chukker
  server.on("/chukker", HTTP_GET, [](AsyncWebServerRequest *request){
    const int paramQty = request->params();
    String data;
    if(paramQty < 1){
      if(DEBUG){ Serial.println("Not enought parameters."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    if(!(request->hasParam("cmd"))){
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    AsyncWebParameter* p_0 = request->getParam("cmd");
    int cmd = (p_0->value()).toInt();
    if(cmd == INC_CHUKKER){
      if(DEBUG){ Serial.println("Request to increase chukker."); }
      updateChukker(INCREASE);
    }
    else if(cmd == DEC_CHUKKER){
      if(DEBUG){ Serial.println("Request to decrease chukker."); }
      updateChukker(DECREASE);
    }
    else{
      if(DEBUG){ Serial.println("Parameter error -> cmd"); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    cmdReceived = true;
    data = getScoreboard_toString();
    request->send(STATUS_ACCEPTED, "text/plain", data);
  });

  // Reset tablero a valores default
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    const int paramQty = request->params();
    String data;
    if(paramQty < 1){
      if(DEBUG){ Serial.println("Not enought parameters."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    if(!(request->hasParam("cmd"))){
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    AsyncWebParameter* p_0 = request->getParam("cmd");
    int cmd = (p_0->value()).toInt();
    if(cmd == RESET_ALL){
        if(DEBUG){ Serial.println("Request to reset all scoreboard values."); }
        resetScoreboard();
        cmdReceived = true;
        data = getScoreboard_toString();
        request->send(STATUS_ACCEPTED, "text/plain", data);
    }
    else if(cmd == RESET_CHUKKER){
      if(DEBUG){ Serial.println("Request to reset the timer."); }
      cmdReceived = true;
      resetChukker();
      data = getScoreboard_toString();
      request->send(STATUS_ACCEPTED, "text/plain", data);
    }
    else{
      if(DEBUG){ Serial.println("Invalid command."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
  });

  // Obtener datos de tablero como string
  server.on("/scoreboard", HTTP_GET, [](AsyncWebServerRequest *request){
    if(DEBUG){ Serial.println("Sending board data ..."); }
    String data = getScoreboard_toString();
    request->send(STATUS_OK, "text/plain", data);
  });

  // Solicitud para corroborar conexión
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request){
    if(DEBUG){ Serial.println("HTTP ping received ..."); }
    request->send(STATUS_OK);
  });

  // Configuraciones del cartel
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    const int paramQty = request->params();
    if(DEBUG){ Serial.println("Request for settings."); }
    if(!request->hasParam("cmd")){
      if(DEBUG){ Serial.println("No cmd parameter."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    AsyncWebParameter* p_0 = request->getParam("cmd"); ;
    int cmd = (p_0->value()).toInt();
    if(cmd == GET_CONFIG){
      String data = getConfig_toString();
      request->send(STATUS_ACCEPTED, "text/plain", data);
      return;
    }
    else if(cmd == SET_CONFIG){
      if(paramQty < 9) {
        if(DEBUG){ Serial.println("Not enought parameters."); }
        request->send(STATUS_BAD_REQUEST);
        return;
      }
      if(!(request->hasParam("l_bri") && request->hasParam("a_en") && request->hasParam("a_len")
            && request->hasParam("ch_mm") && request->hasParam("ch_ss")
            && request->hasParam("et_mm") && request->hasParam("et_ss")
            && request->hasParam("hf_mm") && request->hasParam("hf_ss"))){
        request->send(STATUS_BAD_REQUEST);
        return;
      }
      AsyncWebParameter* p_1 = request->getParam("l_bri");
      AsyncWebParameter* p_2 = request->getParam("a_en");
      AsyncWebParameter* p_3 = request->getParam("a_len");
      AsyncWebParameter* p_4 = request->getParam("ch_mm");
      AsyncWebParameter* p_5 = request->getParam("ch_ss");
      AsyncWebParameter* p_6 = request->getParam("et_mm");
      AsyncWebParameter* p_7 = request->getParam("et_ss");
      AsyncWebParameter* p_8 = request->getParam("hf_mm");  
      AsyncWebParameter* p_9 = request->getParam("hf_ss");
      int l_bri = (p_1->value()).toInt();
      int a_en = (p_2->value()).toInt();
      int a_len = (p_3->value()).toInt();
      int ch_mm = (p_4->value()).toInt();
      int ch_ss = (p_5->value()).toInt();
      int et_mm = (p_6->value()).toInt();
      int et_ss = (p_7->value()).toInt();
      int hf_mm = (p_8->value()).toInt();
      int hf_ss = (p_9->value()).toInt();
      if(!updateBoardConfig(l_bri)){
        if(DEBUG){ Serial.println("Error: board config not updated."); }
        request->send(STATUS_BAD_REQUEST);
      }
      if(!updateAppConfig(a_en, a_len, ch_mm, ch_ss, et_mm, et_ss, hf_mm, hf_ss)){
        if(DEBUG){ Serial.println("Error: app config not updated."); }
        request->send(STATUS_BAD_REQUEST);
      }
      String data = getConfig_toString();
      request->send(STATUS_ACCEPTED, "text/plain", data);
      cmdReceived = true;
      if(DEBUG){ Serial.println("Settings updated."); }
      return;
    }
    else{
      if(DEBUG){ Serial.println("Invalid command."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
  });

  // Disparar señal sonora
  server.on("/horn", HTTP_GET, [](AsyncWebServerRequest *request){
    const int paramQty = request->params();
    if(paramQty < 1){
      if(DEBUG){ Serial.println("Not enought parameters."); }
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    if(!(request->hasParam("cmd"))){
      request->send(STATUS_BAD_REQUEST);
      return;
    }
    AsyncWebParameter* p_0 = request->getParam("cmd");
    int cmd = (p_0->value()).toInt();
    if(cmd == TRIGGER_HORN){
        if(DEBUG){ Serial.println("Request to trigger horn."); }
        startAlarm();
        request->send(STATUS_ACCEPTED);
    }
  });

  // Rutas no definidas
  server.onNotFound([](AsyncWebServerRequest *request){
    if(DEBUG){ Serial.println("Request for undefined route."); }
    request->send(STATUS_NOT_FOUND, "text/plain", "Page not found.");
  });

  server.begin();
  delay(100);
}

/************************************************************ INFINITE LOOP ************************************************************/
void loop()
{
  byte bufferTx[TX_MAX_LENGHT];
  byte dataFrame[DATA_FRAME_ROWS];
  
  /*********************************************************************************/
  /****************************** STATE MACHINE LOOP *******************************/
  while (1)
  {
    switch (main_state)
    {
    case IDLE:
      // Pasado un segundo, con el timer activo, se actualiza el tablero
      if (timerValueUpdate){
        timerValueUpdate = false;
        timer_state = refreshTimer();
        main_state = REFRESH_SCOREBOARD;
      }
      // A la espera de comando por HTTP Request
      if (cmdReceived){
        cmdReceived = false;
        if(newBoardConfig) {
          scoreboard_setConfigs(&scoreboard);
          newBoardConfig = false;
        }
        else main_state = REFRESH_SCOREBOARD;
      }
      break;
    case REFRESH_SCOREBOARD:
      refreshScoreboard(&scoreboard, dataFrame, bufferTx);
      main_state = IDLE;
      break;
    case INIT:
      if(DEBUG){ Serial.println("Inicializando tablero ..."); }
      setDataFrameHeaders(dataFrame);
      delay(5000);
      refreshScoreboard(&scoreboard, dataFrame, bufferTx);
      scoreboard_setConfigs(&scoreboard);
      //do{
      //  refreshScoreboard(&scoreboard, dataFrame, bufferTx); // nuevo intento de inicializar tablero
      //  if(Serial.available()){
      //    if(Serial.read() == 0xCC){ init = true; }
      //  }
      //  else{ delay(1000); }
      //}while(!init);
      //if(DEBUG){ Serial.println("Inicialización exitosa!"); }
      dataFrame[RESPONSE] = 0x01;   // comunicacion con placa controladora sin código
      main_state = IDLE;            // de retorno (solo necesario para inicializacion)
      break;
    default:
      main_state = IDLE;
      break;
    }
    delay(10);
  }
}

/***************************************************************************/
/******************************** FUNCTIONS ********************************/
// Se inicializa el dataframe con los headers para comunicarse con la placa controladora
void setDataFrameHeaders(byte *dataFrame){
  dataFrame[HEADER] = 0x7F;
  dataFrame[COMMAND] = 0xDD;    // enviar data
  dataFrame[ADDRESS] = 0x00;    // broadcast
  dataFrame[RESPONSE] = 0x00;   // codigo de retorno habilitado (solo para inicializacion, confirmando comunicacion)
  dataFrame[RESERVED_1] = 0x00;
  dataFrame[FLASH] = 0x01;      // no guardar en flash
  dataFrame[RESERVED_2] = 0x00;
  dataFrame[RESERVED_3] = 0x00;
  dataFrame[DATA_END] = 0xFF;
  dataFrame[FRAME_END] = 0x7F;
}

// Genera el checksum para el data frame a enviar a la placa controladora de leds
byte genChecksum(byte *dataFrame, byte offset, byte end)
{
  int checksum = 0;
  byte i;
  // Para el checksum, se suman los bytes de dataFrame desde el byte COMMAND hasta el DATA_END inclusive
  for (i = offset; i <= end; i++)
  {
    checksum += dataFrame[i];
  }
  return (byte)(checksum & 0xFF); // el checksum es byte menos significativo
}

// Copia los datos de data frame a buffer a transmitir por serial
unsigned int setBufferTx(byte *bufferTx, byte *dataFrame)
{
  byte i = 0;
  unsigned int bytes_to_transfer = 0;
  for (i = 0; i < DATA_FRAME_ROWS; i++)
  {
    bufferTx[i] = dataFrame[i];
    bytes_to_transfer++;
  }
  return bytes_to_transfer;
}

// Iniciar alarma
void startAlarm(){
  if(scoreboard.alarmEnabled) {
    if(alarm_obj.active == false) {
      if(DEBUG) Serial.println("Triggering horn ...");
      #ifdef DAC_ALARM
        alarm_obj.bell_count = 1;
        SampleIdx = 0;
        dac_output_enable(DAC_CHANNEL_1);
      #else
        alarm_obj.secondsActive = 0;
        digitalWrite(ALARM_PIN, LOW);
      #endif
      timerStart(Timer1_cfg);
      alarm_obj.active = true;
    }
    else if(DEBUG) Serial.println("Horn already active.");
  }
}

// Detener alarma
void stopAlarm(){
  if(alarm_obj.active){
    timerStop(Timer1_cfg);
    #ifdef DAC_ALARM
      dac_output_disable(DAC_CHANNEL_1);
      alarm_obj.bell_count = 1;
      SampleIdx = 0;
    #else
      timerWrite(Timer1_cfg, 0);
      digitalWrite(ALARM_PIN, HIGH);
    #endif
    alarm_obj.active = false;
    if(DEBUG) Serial.println("Horn stopped.");
  }
}

// Actualiza timer, transcurrido un segundo
timer_state_t refreshTimer()
{
  // Actualizar timer
  scoreboard.timer.value.ss--;
  if (scoreboard.timer.value.mm == 0 && scoreboard.timer.value.ss == 0)
  {
    if(game_state == IN_PROGRESS){
      game_state = EXTENDED_TIME;
      scoreboard.timer.value.mm = scoreboard.timer.extendedtime.mm;
      scoreboard.timer.value.ss = scoreboard.timer.extendedtime.ss;
      startAlarm();
    }
    else if(game_state == EXTENDED_TIME){
      game_state = HALFTIME;
      scoreboard.timer.value.mm = scoreboard.timer.halftime.mm;
      scoreboard.timer.value.ss = scoreboard.timer.halftime.ss;
      startAlarm();
    }
    else if(game_state == HALFTIME){
      // Detener y resetear el timer si finalizó el descanso
      scoreboard.chukker++;
      resetChukker();
      return STOPPED;
    }
  }
  else if (scoreboard.timer.value.ss < 0){
    // Si pasaron 60 segundos, decrementar minutos y resetear segundos.
    scoreboard.timer.value.ss = 59;
    if (scoreboard.timer.value.mm > 0)
      scoreboard.timer.value.mm--;
  }
  return RUNNING;
}

// Iniciar el timer
void startTimer()
{
  if (timer_state == STOPPED && !(scoreboard.timer.value.mm == 0 && scoreboard.timer.value.ss == 0))
  {
    // Iniciar timer
    timerStart(Timer0_cfg);
    timer_state = RUNNING;
  }
}

// Frena el timer
void stopTimer()
{
  if (timer_state == RUNNING)
  {
    timerStop(Timer0_cfg);
    timer_state = STOPPED;
    stopAlarm();
  }
}

// Resetear el timer a valor default y frenarlo
void resetChukker()
{
  scoreboard.timer.value.mm = scoreboard.timer.initValue.mm;
  scoreboard.timer.value.ss = scoreboard.timer.initValue.ss;

  // Resetear y frenar el timer
  timerStop(Timer0_cfg);
  timerWrite(Timer0_cfg, 0);
  timer_state = STOPPED;
  game_state = IN_PROGRESS;

  stopAlarm();
}

// Setear valor en timer
bool setTimerValue(int mm, int ss, int action){
  if(timer_state == RUNNING){ return false; }
  else if(mm == 0 && ss == 0){ return false; }
  else if(mm < 0 || mm > 59){ return false; }
  else if(ss < 0 || ss > 59){ return false; }
  if(action == SET_DEFAULT_TIMER){
    scoreboard.timer.initValue.mm = mm;
    scoreboard.timer.initValue.ss = ss;
    scoreboard.timer.value.mm = mm;
    scoreboard.timer.value.ss = ss;
  }
  else if (action == SET_CURRENT_TIMER){
    scoreboard.timer.value.mm = mm;
    scoreboard.timer.value.ss = ss;
  }
  else if (action == SET_EXTENDED_TIMER){
    scoreboard.timer.extendedtime.mm = mm;
    scoreboard.timer.extendedtime.ss = ss;
  }
  else if (action == SET_HALFTIME_TIMER){
    scoreboard.timer.halftime.mm = mm;
    scoreboard.timer.halftime.ss = ss;
  }
  else{ return false; }
  return true;
}

// Actualizar puntajes, con accion y equipo que corresponda
void updateScores(int action, int team)
{
  if (action == INCREASE)
  {
    if (scoreboard.score[team] < 99){ scoreboard.score[team]++; }
    else{ scoreboard.score[team] = 0; }
  }
  else if (scoreboard.score[team] > 0){ scoreboard.score[team]--; }
}

// Actualizar chukker, segun la accion solicitada
void updateChukker(int action)
{
  if (action == INCREASE)
  {
    if (scoreboard.chukker < 9) { scoreboard.chukker++; }
    else{ scoreboard.chukker = 0; }
  }
  else if (scoreboard.chukker > 0){ scoreboard.chukker--; }
}

// Llevar tablero a valor default
void resetScoreboard()
{
  scoreboard.score[VISITOR] = 0;
  scoreboard.score[LOCAL] = 0;
  scoreboard.chukker = 1;
  scoreboard.timer.value.mm = scoreboard.timer.initValue.mm;
  scoreboard.timer.value.ss = scoreboard.timer.initValue.ss;

  // Resetear y frenar el timer
  timerStop(Timer0_cfg);
  timerWrite(Timer0_cfg, 0);
  timer_state = STOPPED;
  game_state = IN_PROGRESS;
}

// Actualizar datos en tablero, enviando los datos por serial a placa controladora
void refreshScoreboard(scoreboard_t *scoreboard, byte *dataFrame, byte *bufferTx)
{
  unsigned int DATA_FRAME_BYTES = 0;
  setDataFrame(scoreboard, dataFrame);                 // setear la trama de datos a enviar
  DATA_FRAME_BYTES = setBufferTx(bufferTx, dataFrame); // setear buffer para enviar por serial
  Serial.write(bufferTx, DATA_FRAME_BYTES);            // enviar data
}

// Se convierten valores numéricos a valores ASCII y se asignan al dataFrame a enviar al tablero
void setDataFrame(scoreboard_t *scoreboard, byte *dataFrame)
{
  dataFrame[TIMER_MM_DECENA] = (byte)(scoreboard->timer.value.mm / 10 + '0');
  dataFrame[TIMER_MM_UNIDAD] = (byte)(scoreboard->timer.value.mm % 10 + '0') + DOT_VALUE;
  dataFrame[TIMER_SS_DECENA] = (byte)(scoreboard->timer.value.ss / 10 + '0') + DOT_VALUE;
  dataFrame[TIMER_SS_UNIDAD] = (byte)(scoreboard->timer.value.ss % 10 + '0');
  dataFrame[SCORE_VISITOR_UNIDAD] = (byte)(scoreboard->score[VISITOR] % 10 + '0');
  dataFrame[SCORE_VISITOR_DECENA] = (byte)(scoreboard->score[VISITOR] / 10 + '0');
  dataFrame[SCORE_LOCAL_UNIDAD] = (byte)(scoreboard->score[LOCAL] % 10 + '0');
  dataFrame[SCORE_LOCAL_DECENA] = (byte)(scoreboard->score[LOCAL] / 10 + '0');
  dataFrame[CHUKKER] = (byte)(scoreboard->chukker % 10 + '0');
  dataFrame[CHECKSUM] = genChecksum(dataFrame, COMMAND, DATA_END);
}

// Transformar los datos del tablero en una cadena concatenada, para enviar a front-end
String getScoreboard_toString(void){
  String s = String(scoreboard.score[0]);
  s += "," + String(scoreboard.score[1]);
  s += "," + String(scoreboard.chukker);
  s += "," + String(scoreboard.timer.value.mm);
  s += "," + String(scoreboard.timer.value.ss);
  s += "," + String(timer_state);
  s += "," + String(game_state);
  return s;
}

// Transformar la configuración del tablero en cadena concatenada
String getConfig_toString(void){
  String s = String(scoreboard.brightness);
  s += "," + String(scoreboard.alarmEnabled);
  s += "," + String(scoreboard.alarmLen);
  s += "," + String(scoreboard.timer.initValue.mm);
  s += "," + String(scoreboard.timer.initValue.ss);
  s += "," + String(scoreboard.timer.extendedtime.mm);
  s += "," + String(scoreboard.timer.extendedtime.ss);
  s += "," + String(scoreboard.timer.halftime.mm);
  s += "," + String(scoreboard.timer.halftime.ss);
  return s;
}

// Actualizar estructura scoreboard con configuraciones
bool updateAppConfig(int a_en, int a_len, int ch_mm, int ch_ss, int et_mm, int et_ss, int hf_mm, int hf_ss){
  stopTimer(); stopAlarm();

  if(!setTimerValue(ch_mm, ch_ss, SET_DEFAULT_TIMER)) return false;
  if(!setTimerValue(et_mm, et_ss, SET_EXTENDED_TIMER)) return false;
  if(!setTimerValue(hf_mm, hf_ss, SET_HALFTIME_TIMER)) return false;

  // Señal sonora
  if(a_en < 0) return false;
  else if(a_en > 0) scoreboard.alarmEnabled = 1;
  else scoreboard.alarmEnabled = 0;
  
  if(a_len < 0x01) return false;
  if(a_len > ALARM_MAX_LEN) scoreboard.alarmLen = ALARM_MAX_LEN;
  else scoreboard.alarmLen = a_len;

  if(game_state == IN_PROGRESS) {
    scoreboard.timer.value.mm = scoreboard.timer.initValue.mm;
    scoreboard.timer.value.ss = scoreboard.timer.initValue.ss;
  }
  else if(game_state == EXTENDED_TIME){
    scoreboard.timer.value.mm = scoreboard.timer.extendedtime.mm;
    scoreboard.timer.value.ss = scoreboard.timer.extendedtime.ss;
  }
  else if(game_state == HALFTIME){
    scoreboard.timer.value.mm = scoreboard.timer.halftime.mm;
    scoreboard.timer.value.ss = scoreboard.timer.halftime.ss;
  }

  if(DEBUG) Serial.println("App settings updated.");

  return true;
}

// Actualizar configuraciones en placa controladora de LEDs
bool updateBoardConfig(int l_bri){
   if(l_bri < 0x01) return false;
   
   // Brillo de leds
  if(l_bri != scoreboard.brightness) newBoardConfig = true;
  if(l_bri > 0x0A) scoreboard.brightness = 0x0A;
  else scoreboard.brightness = l_bri;

  if(DEBUG) Serial.println("Board settings updated.");
  return true;
}

// Mandar configuracion a tablero
void scoreboard_setConfigs(scoreboard_t *scoreboard){
  static byte brightnessDataFrame[] = { 0x7F, 0xD3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0A,   \
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                          0xDE, 0x7F };
  static byte brightnessByteIndex = 0x09;
  static byte checksumIndex = 30;
  brightnessDataFrame[brightnessByteIndex] = scoreboard->brightness;
  brightnessDataFrame[checksumIndex] = genChecksum(brightnessDataFrame, COMMAND, brightnessByteIndex);
  Serial.write(brightnessDataFrame, sizeof(brightnessDataFrame));
  if(DEBUG) Serial.println("Board new settings sent.");
}