/*
дисплей для смарт БМС
   Разработано:   espirans.msk@gmail.com   
Протокол для БМС JBD
*/

#include <Arduino.h>
#include <avr/sleep.h>
#include <U8x8lib.h> //https://github.com/olikraus/u8g2/wiki/u8x8reference


#define max_buf0  64   
uint8_t buffer0[max_buf0] = {0};

uint8_t zapros[3][4] =
{
  {0x05,0x00,0xFF,0xFB}, 
  {0x03,0x00,0xFF,0xFD}, 
  {0x04,0x00,0xFF,0xFC} 
};

uint8_t cels_count  = 16;
uint16_t V_cels[16] = {3123};
byte b_cels[16] = {1};
int16_t NTC[8] = {-234,345,-243,145,-334,445};
uint16_t Vbat = 4812;
int16_t Abat = -10000;
uint16_t RemCapacity = 11234;
uint16_t TypCapacity = 100000;
uint16_t ProtectStatus =  0;
uint8_t RSOC= 10;
uint8_t balstatL = 0; //Balance status 1-8
uint8_t balstatH = 0; //Balance status 9- 16
uint8_t NTC_numb = 6;
uint8_t FET_stat = 1;
volatile boolean BMS_conect = false;
char msg[64];
float Pbat = 10;
uint8_t h_bat = 37;
uint8_t m_bat = 59;
uint8_t num_byte=0;
uint8_t n_scr = 0;
uint8_t n_scr_b = 0;
uint8_t t_clmn, t_rov = 0;
bool key_flag = false;
bool prtct_flag = false;
volatile bool sleep_flag = false;
unsigned long time_bat = 0;
unsigned long sleep_timer, dspl_timer, answ_t_aut_timer, chng_timer, opros_timer, rst_timer = 0; 
volatile boolean zapros_flag = true;
volatile uint8_t curr_zapr = 0;
String b_status = "err";

// ---- кнопка ----
#define key_pin 2  // PD2 кнопка INT0

#define slp_time 300000    //5 минут  переход в спящий режим если ток = 0 и не нажималась кнопка кнопка
#define answ_t_aut 3000   // max время ожидания ответа  
#define opros_time  1000  // период опроса БМС
#define dspl_time 500     // интервал обновления дисплея 
#define chng_time  5000  //  интервал смены экранов дисплея


U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE);


void setup() {
  pinMode(key_pin, INPUT);
  attachInterrupt(0,key_down, FALLING); // прерывание INT0 по переднему фронту в 1

  Serial.begin(9600);
  
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);

  u8x8.begin();
  u8x8.setPowerSave(0);
  u8x8.setFont(u8x8_font_8x13_1x2_f);
  u8x8.drawString(2,3,"Hello BMS!");
  delay(2000);
  u8x8.clearDisplay();

  sleep_timer = millis();
  chng_timer = millis();
  dspl_timer = millis();
  answ_t_aut_timer = millis();
  opros_timer = millis();
  rst_timer = millis();
}


void loop() {

  if ( millis() < rst_timer ){//таймер провернулся.
   asm("JMP 0");  // перезапускаем ардуинку
  }  

  rst_timer = millis();

  if(Abat != 0){  //abs(Abat) > 0
    sleep_timer = millis();
  }

  if ((millis() - sleep_timer) > slp_time & !zapros_flag){  //переходим в режим сна если настало время и не идет обмен с БМС
    u8x8.setPowerSave(1);   // спать OLED
    sleep_flag = true;
    sleep_mode();           // спать arduino
  }

  if(key_flag & sleep_flag){ // если нажата кнопка и в режиме сна, то выходим из сна
    //asm("JMP 0");  // перезапускаем ардуинку
    sleep_flag = false;
    zapros_flag = true;
    curr_zapr = 0;
    answ_t_aut_timer = millis();
    chng_timer = millis();
    u8x8.setPowerSave(0); // разбудить OLED 
    u8x8.setFont(u8x8_font_8x13_1x2_f);
    start_zapros();
    n_scr = 0;
    key_flag = false;
  }

   delay(50);

  if( Serial.available() ){ // если в порт пришли какие-то данные 
    opros_timer = millis();   
    delay(50);// немного ждём, чтобы вся пачка данных была принята портом
    while( Serial.available() ){
      buffer0[num_byte++] = Serial.read(); // считываем данные и записываем их в буфер
    } 
      rashet(num_byte); // запускам разбор полученных данных
      zapros_flag = true;
      num_byte = 0;
  }


  if (millis() - dspl_timer > dspl_time){ // обновляем дисплей каждые 0.5 сек
    if(key_flag){
      n_scr++;
      u8x8.clearDisplay();
      key_flag = false;
      chng_timer = millis();
    }
    dspl_timer = millis();
    print_lcd();
  }

  if ((millis() - answ_t_aut_timer) > answ_t_aut){ // Если 3 сек нет ответа от БМС перезапускаем опрос.
    //asm("JMP 0");  // перезапускаем ардуинку
    zapros_flag = true;
    BMS_conect = false;
    u8x8.clearDisplay();
    curr_zapr = 0;
    n_scr = 0;
    answ_t_aut_timer = millis();
  }
  
  if ((millis() - chng_timer) > chng_time){ // смена экранов
    n_scr ++;
    chng_timer = millis();
    u8x8.clearDisplay();
  }

  if (millis() - opros_timer > opros_time){ // раз в 1 сек. разрешить опрос БМС 
    opros_timer  = millis();
    curr_zapr = 0; 
  }

  start_zapros(); // Опрос БМС
}


void key_down(){  // int0  нажата кнопка
  key_flag = true;
  sleep_timer = millis();
}


void print_lcd(){ // вывод на дисплей
  if (!BMS_conect){
    u8x8.drawString(0,3,"no communication");
    u8x8.drawString(4,5,"with BMS");
    return;
  }

  switch (n_scr)
  {
    case 0:
      if(prtct_flag){
        prtct_flag = false;
        u8x8.clearDisplay();
      }
      if (ProtectStatus){
        u8x8.clearDisplay();
        u8x8.setCursor(0,4);
        u8x8.print(ProtectStat());
        prtct_flag = true;
      }else{
        Pbat = (float)Abat/100 * (float)Vbat/100;
        dtostrf((float)Abat/100, 6, 2, msg);
        u8x8.drawString(3,2,msg);
        u8x8.setCursor(10,2);
        u8x8.print(" A ");

        dtostrf((float)Pbat, 7, 2, msg);
        u8x8.drawString(2,4,msg);
        u8x8.setCursor(10,4);
        u8x8.print(" Wt ");

        sprintf(msg, "%3d%%", RSOC);
          u8x8.drawString(0,6,msg);
        time_rscht();
          u8x8.drawString(10,6,msg);
      }
      u8x8.setCursor(0,0);
        u8x8.print((float)Vbat/100); u8x8.print("v ");
      
        dtostrf((float)RemCapacity/100, 5, 1, msg);
        u8x8.drawString(8,0,msg);
        u8x8.setCursor(14,0);
        u8x8.print("Ah");
        chng_timer = millis();
    break;

    case 1:
    case 2:
    case 3:
    case 4:
      if(n_scr > (cels_count/4)){
        n_scr = 5; 
      }else{
        n_scr_b = (n_scr - 1) * 4;
        for(uint8_t k = 0; k < 4; k++){
          sprintf(msg, "%2d", n_scr_b + k + 1);
          u8x8.drawString(1,k*2,msg);
          dtostrf((float)V_cels[n_scr_b + k]/1000, 1, 3, msg);
          u8x8.drawString(5,k*2,msg);
          if(b_cels[n_scr_b + k]){
            u8x8.drawString(12,k*2,"b");
          }
        }
      }
    break;

    case 5:
      for(uint8_t k = 0; k < NTC_numb; k++){
        t_clmn = k/4 * 8 + 1;
        t_rov = k;
        if(t_rov >= 4){
          t_rov = t_rov - 4;
        } 
        sprintf(msg, "T%1d:% 2d  ", k+1, NTC[k]/10);
        u8x8.drawString(t_clmn,t_rov*2,msg);
      }
    break;
    
    default:
      n_scr = 0;
      u8x8.clearDisplay();
    break;
  }  

}


void time_rscht(){  // расчет времени работы/заряда
  if(abs(Abat) > 0){
    if(Abat < 0){
      time_bat = (long)RemCapacity * 60 / abs(Abat);
    }else{
      time_bat = (long)(TypCapacity - RemCapacity) * 60 / abs(Abat);
    } 
    h_bat = time_bat / 60;
    m_bat = time_bat % 60;
    sprintf(msg, "%02d:%02d", h_bat, m_bat);
  }else{
    sprintf(msg, "--:--");
  }
}


void start_zapros(){  // Опрос БМС
  
 if (zapros_flag & (curr_zapr <=2)){ 
   zapros_flag = false;
    send_zapros(0xDD); 
    send_zapros(0xA5);
    for (uint8_t k = 0; k < 4; k++) {
      send_zapros(zapros[curr_zapr][k]);
    } 
    send_zapros(0x77);
    delay(50);
 }
}


void send_zapros(byte b){
    Serial.write(b);
    Serial.flush();   
}


void rashet(uint8_t b){  // расчет банных от БМС
  if ((buffer0[b-1] == 0x77) && (buffer0[0] == 0xDD) && (buffer0[2] == 0x00) ){
    if (crc_chk()){ 
      BMS_conect = true;
      switch (buffer0[1]){
        case 0x03:
          Vbat = buffer0[4]<<8 | buffer0[5];
          Abat = buffer0[6]<<8 | buffer0[7];
          RemCapacity = buffer0[8]<<8 | buffer0[9];
          TypCapacity = buffer0[10]<<8 | buffer0[11];
          ProtectStatus = buffer0[20]<<8 | buffer0[21];
          RSOC = buffer0[23];
          FET_stat = buffer0[24];
          cels_count = buffer0[25];
          for (uint8_t k = 0; k < 8; k++){
           b_cels[k] = buffer0[17]  >> k;  //сдвигаем на k 
           b_cels[k] = b_cels[k] & 0b00000001; //  берем только D0 
          }

          for (uint8_t k = 0; k < 8; k++){
           b_cels[8+k] = buffer0[16]  >> k;  //сдвигаем на k 
           b_cels[8+k] = b_cels[8+k] & 0b00000001; //  берем только D0 
          }

          NTC_numb = buffer0[26];
          if (NTC_numb > 8){
            NTC_numb = 8;
          }
          
          for (uint8_t k = 0; k < NTC_numb; k++){
            NTC[k]  =  buffer0[(27 + k*2)]<<8 | buffer0[(28 + k*2)];
            NTC[k] = NTC[k]- 2731;
          }  
        break;

        case 0x04:
          for (uint8_t k = 0; k < cels_count; k++){
            V_cels[k]  =  buffer0[(4 + k*2)]<<8 | buffer0[(5 + k*2)];
          } 
        break;
        
        default:
        break;
      } 
      curr_zapr++;
      answ_t_aut_timer = millis();   
    }
  }
}


boolean crc_chk(){  // Расечет CRC
  uint8_t l = buffer0[3] + 3;

  uint32_t crc=0x10000;
    for (uint8_t i=3; i<=l;i++){
      crc = crc - buffer0[i];
    }
    uint8_t c1 = (uint32_t)crc>>8;
     // Serial.write(c1);  
    uint8_t c2 = crc;
     // Serial.write(c2);
  if ((c1 == buffer0[l+1]) && (c2 == buffer0[l+2])){
    //Serial.write(c1); 
    //Serial.write(c2); 
    return 1;
  } else {
    return 0;
  }
  
}


String ProtectStat(){ // "расшифровка статусов БМС"
  uint16_t stat=0;
  b_status = "ERR";
  for (uint8_t k = 0; k < 13; k++){
    stat = ProtectStatus  >> k;  //сдвигаем на k 
    stat = stat & 0b0000000000000001; //  берем только D0 
      if (stat){
        switch (k){       
          case 0:
          b_status = "Over-charge cell";
          break;
          
          case 1:
          b_status = "Over-DSchrg cell";
          break;

          case 2:
          b_status = "Over-voltage bat";
          break;

          case 3:
          b_status = "Low-voltage batt";
          break;

          case 4:
          b_status = "High temp chrg";
          break;

          case 5:
          b_status = "Low temp of chrg";
          break;

          case 6:
          b_status = "High temp Dschrg";
          break;

          case 7:
          b_status = "Low temp Dischrg";
          break;

          case 8:
          b_status = "Over-curr. chrg";
          break;

          case 9:
          b_status = "Over-curr. Dchrg";
          break;

          case 10:
          b_status = "Short protect";
          break;

          case 11:
          b_status = "    IC err";
          break;

          case 12:
          b_status = "MOS lock by soft";
          break;
        }
      }
  }
  return b_status;
}
