/*
 * Arduino UNO R4 Oscilloscope using a 160x80 IPS TFT Version 1.02
 * The max sampling rates is 346ksps with single channel, 141ksps with 2 channels.
 * + Pulse Generator
 * + DAC DDS Function Generator (23 waveforms)
 * Copyright (c) 2025, Siliconvalley4066
 */
/*
 * Arduino Oscilloscope using a graphic LCD
 * The max sampling rates are 4.3ksps with 2 channels and 8.6ksps with a channel.
 * Copyright (c) 2009, Noriaki Mitsunaga
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include "FreqCount_R4.h"

#define TFT_CS    6
#define TFT_RST   -1
#define TFT_DC    5
//#define TFT_SCLK  13
//#define TFT_MOSI  11
Adafruit_ST7735 display = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define BUTTON5DIR
#define EEPROM_START 0
#ifdef EEPROM_START
#include <EEPROM.h>
#endif
#include "arduinoFFT.h"
#define FFT_N 128
double vReal[FFT_N]; // Real part array, actually float type
double vImag[FFT_N]; // Imaginary part array
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_N, 1.0);  // Create FFT object

#define txtLINE0   1
#define txtLINE1   11
#define txtLINE2   21
#define txtLINE3   31
#define txtLINE4   41
#define txtLINE5   51
#define txtLINE6   61
#define txtLINE7   71
#define DISPTXT 136

float waveFreq[2];             // frequency (Hz)
float waveDuty[2];             // duty ratio (%)
int dataMin[2];                // buffer minimum value (smallest=0)
int dataMax[2];                //        maximum value (largest=4095)
int dataAve[2];                // 10 x average value (use 10x value to keep accuracy. so, max=40950)
int saveTimer;                 // remaining time for saving EEPROM
int timeExec;                  // approx. execution time of current range setting (ms)
extern byte duty;
extern byte p_range;
extern unsigned short count;
extern long ifreq;
extern byte wave_id;

const int LCD_WIDTH = 160;
const int LCD_HEIGHT = 80;
const int LCD_YMAX = 79;
const int SAMPLES = 160;
const int NSAMP = 512;
const int DISPLNG = 130;
const int DOTS_DIV = 10;
const byte ad_ch0 = A1;                 // Analog pin for channel 0
const byte ad_ch1 = A2;                 // Analog pin for channel 1
const long VREF[] = {50, 100, 250, 500, 1000};  // reference voltage 5.0V -> 50 : 1V/div range (100mV/dot)
                                        //                        -> 100 : 0.5V/div
                                        //                        -> 250 : 0.2V/div
                                        //                        -> 500 : 100mV/div
                                        //                        -> 1000 :  50mV/div
                                        // 5.0V / attn * DOTS_DIV / vdiv
//const int MILLIVOL_per_dot[] = {100, 50, 20, 10, 5}; // mV/dot
//const int ac_offset[] PROGMEM = {410, -819, -1556, -1802, -1925};  // 3 div offset
//                            = 3 * vdiv / 5.0 * 4096 - 2048
const int ac_offset[] PROGMEM = {1229, -410, -1393, -1720, -1884}; // 4 div offset
//                            = 4 * vdiv / 5.0 * 4096 - 2048
const int MODE_ON = 0;
const int MODE_INV = 1;
const int MODE_OFF = 2;
const char Modes[3][4] PROGMEM = {" ON", "INV", "OFF"};
const int TRIG_AUTO = 0;
const int TRIG_NORM = 1;
const int TRIG_SCAN = 2;
const int TRIG_ONE  = 3;
const char TRIG_Modes[4][5] PROGMEM = {"Auto", "Norm", "Scan", "One "};
const int TRIG_E_UP = 0;
const int TRIG_E_DN = 1;
#define RATE_MIN 0
#define RATE_MAX 17
#define RATE_NUM 18
#define RATE_DMA 0
#define RATE_DUAL 1
#define RATE_SLOW 7
#define RATE_ROLL 13
#define ITEM_MAX 30
const char Rates[RATE_NUM][5] PROGMEM = {"30us", "70us", "100u", "200u", "500u", " 1ms", " 2ms", " 5ms", "10ms", "20ms", "50ms", "0.1s", "0.2s", "0.5s", " 1s ", " 2s ", " 5s ", " 10s"};
const unsigned long HREF[] PROGMEM = {30, 70, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000};
const float dmahref[2] = {28.8, 70.7};
#define RANGE_MIN 0
#define RANGE_MAX 4
#define VRF 5.0
const char Ranges[5][5] PROGMEM = {" 1V ", "0.5V", "0.2V", "0.1V", "50mV"};
byte range0 = RANGE_MIN;
byte range1 = RANGE_MIN;
byte ch0_mode = MODE_ON, ch1_mode = MODE_ON, rate = 0, orate, wrate = 0;
byte trig_mode = TRIG_AUTO, trig_lv = 10, trig_edge = TRIG_E_UP, trig_ch = ad_ch0;
bool Start = true;  // Start sampling
byte item = 0;      // Default item
byte menu = 0;      // Default menu
short ch0_off = 0, ch1_off = 400;
byte data[4][SAMPLES];                  // keep twice of the number of channels to make it a double buffer
uint32_t cap_buf32[NSAMP / 2], cap_buf33[NSAMP / 4];
uint16_t *cap_buf = (uint16_t *)&cap_buf32, *cap_buf1 = (uint16_t *)&cap_buf33;
byte odat00, odat01, odat10, odat11;    // old data buffer for erase
byte sample=0;                          // index for double buffer
bool fft_mode = false, pulse_mode = false, dds_mode = false, fcount_mode = false;
bool full_screen = false;
byte info_mode = 3; // Text information display mode
int trigger_ad;
const double sys_clk = (double)F_CPU;
volatile bool wfft, wdds;
byte time_mag = 1;  // magnify timebase: 1, 2, 5 or 10
double compensation = 1.0;  // compensation for frequency counter
boolean calib = false;      // calibrate flag for frequency counter

#define LEFTPIN   3     // LEFT
#define RIGHTPIN  9     // RIGHT
#define UPPIN     7     // UP
#define DOWNPIN   8     // DOWN
#define CH0DCSW   A3    // DC/AC switch ch0
#define CH1DCSW   4     // DC/AC switch ch1

#define BGCOLOR   ST77XX_BLACK
#define GRIDCOLOR 0x4208
#define CH1COLOR  ST77XX_GREEN
#define CH2COLOR  ST77XX_YELLOW
#define FRMCOLOR  0xE71C
#define TXTCOLOR  ST77XX_WHITE
#define TRGCOLOR  ST77XX_MAGENTA
#define HIGHCOLOR ST77XX_CYAN

void setup(){
  pinMode(CH0DCSW, INPUT_PULLUP);   // CH1 DC/AC
  pinMode(CH1DCSW, INPUT_PULLUP);   // CH2 DC/AC
  pinMode(UPPIN, INPUT_PULLUP);     // up
  pinMode(DOWNPIN, INPUT_PULLUP);   // down
  pinMode(RIGHTPIN, INPUT_PULLUP);  // right
  pinMode(LEFTPIN, INPUT_PULLUP);   // left
  display.initR(INITR_MINI160x80_PLUGIN);  // Init ST7735S mini display
  display.setRotation(3);
  display.fillScreen(BGCOLOR);

//  Serial.begin(115200); while(!Serial);
#ifdef EEPROM_START
  loadEEPROM();                         // read last settings from EEPROM
#else
  set_default();
#endif
  menu = item >> 3;
  wfft = fft_mode;
  wdds = dds_mode;
//  DrawGrid();
//  DrawText();
  if (pulse_mode)
    pulse_init();                       // calibration pulse output
  if (dds_mode)
    dds_setup();
  orate = RATE_DMA + 1;                 // old rate befor change
  analogReadResolution(12);
  analogRead(A2); // dummy read for setup
  analogRead(A1); // dummy read for setup
}

#ifdef DOT_GRID
void DrawGrid() {
  int disp_leng;
  if (full_screen) disp_leng = SAMPLES;
  else disp_leng = DISPLNG;
  for (int x=0; x<=disp_leng; x += 2) { // Horizontal Line
    for (int y=LCD_YMAX; y>=0; y -= DOTS_DIV) {
      display.drawPixel(x, y, GRIDCOLOR);
//      CheckSW();
    }
  }
  for (int x=0; x<=disp_leng; x += DOTS_DIV ) { // Vertical Line
    for (int y=LCD_YMAX; y>=0; y -= 2) {
      display.drawPixel(x, y, GRIDCOLOR);
//      CheckSW();
    }
  }
}
#else
void DrawGrid() {
  int disp_leng;
  if (full_screen) disp_leng = SAMPLES;
  else disp_leng = DISPLNG;
//  display.drawFastVLine(0, 0, LCD_YMAX, FRMCOLOR);          // left vertical line
//  display.drawFastVLine(SAMPLES, 0, LCD_YMAX, FRMCOLOR);  // right vertical line
//  display.drawFastHLine(0, 0, SAMPLES, FRMCOLOR);           // top horizontal line
//  display.drawFastHLine(0, LCD_YMAX, SAMPLES, FRMCOLOR);  // bottom horizontal line

  for (int y = 9; y <= LCD_YMAX; y += DOTS_DIV) {
    if (y > 0){
      display.drawFastHLine(1, y, disp_leng - 1, GRIDCOLOR);  // Draw 9 horizontal lines
    }
    for (int i = 5; i < DOTS_DIV; i += 5) {
      display.drawFastHLine(LCD_WIDTH / 2 - 2, y - i, 5, GRIDCOLOR);  // Draw the vertical center line ticks
    }
  }
  for (int x = 0; x < disp_leng; x += DOTS_DIV) {
    display.drawFastVLine(x, 0, LCD_YMAX, GRIDCOLOR); // Draw 11 vertical lines
    for (int i = 5; i < DOTS_DIV; i += 5) {
      display.drawFastVLine(x + i, LCD_YMAX / 2 -2, 5, GRIDCOLOR);  // Draw the horizontal center line ticks
    }
  }
  if (!full_screen) display.drawFastVLine(DISPLNG, 0, LCD_YMAX, GRIDCOLOR);
}
#endif

void fcount_disp() {
  unsigned long count;
  static double dfreq = 0.0;
  uint8_t status;

  if (!fcount_mode) return;
  if (status = FreqCount.available()) { // wait finish restart
    count = FreqCount.read();
    dfreq = (double) count;
    if (calib) calibrate(dfreq);
    calib = false;
  }
  displayfreq(round(dfreq * compensation));
}

void displayfreq(unsigned long freq) {
  display.setTextColor(TXTCOLOR, BGCOLOR);
  String ss = String(freq);
  int l = ss.length();
  if (l > 6) {  // greater than or equal to 1,000,000Hz
    ss = ss.substring(0, l - 6) + "," + ss.substring(l - 6);
    ++l;
  }
  if (l > 3) {  // greater than or equal to 1,000Hz
    ss = ss.substring(0, l - 3) + "," + ss.substring(l - 3);
    ++l;
  }
  display.setCursor(DISPLNG - 6 * l - 12, txtLINE6);
  display.print(ss); display.print("Hz");
}

void calibrate(double freq) {
  float references[] = {24e6, 20e6, 16e6, 12e6, 10e6, 8e6, 6e6, 5e6,
          4e6, 3e6, 2e6, 1e6, 1e5, 32768.0};
  int num = sizeof(references) / sizeof(float);
  for (int i = 0; i < num; ++i) {
    double ref = (double) references[i];
    if ((ref * 0.998) < freq && freq < (ref * 1.002)) { // 2000ppm
      compensation = ref / (double) freq;
      break;
    }
  }
}

void display_range(byte rng) {
  display.print(Ranges[rng]);
}

void display_rate(void) {
  display.print(Rates[rate]);
}

void display_mode(byte chmode) {
  display.print(Modes[chmode]); display.print(' ');
}

void display_trig_mode(void) {
  display.print(TRIG_Modes[trig_mode]); 
}

void display_ac(byte pin) {
  if (digitalRead(pin) == LOW) display.print('~');
  else display.print(' ');
}

void set_line_color(byte line) {
  if ((item & 0x7) == line) display.setTextColor(BGCOLOR, TXTCOLOR);  // highlight
  else display.setTextColor(TXTCOLOR, BGCOLOR);           // normal
  display.setCursor(DISPTXT, 10 * line + 1);  // locate curser for printing text
}

void DrawGrid(int x) {
  if ((x % DOTS_DIV) == 0) {
    display.drawFastVLine(x, 0, LCD_HEIGHT, GRIDCOLOR); // Draw 14 vertical lines
  } else {
    if (((x + 5) % DOTS_DIV) == 0)
      display.drawFastVLine(x, LCD_YMAX / 2 -2, 5, GRIDCOLOR);  // Draw the horizontal center line ticks
    for (int y=9; y<=LCD_YMAX; y += DOTS_DIV)
      display.drawPixel(x, y, GRIDCOLOR);
    if (x > LCD_WIDTH / 2 - 3 && x < LCD_WIDTH / 2 + 3)
      for (int y=4; y<=LCD_YMAX; y += DOTS_DIV)
        display.drawPixel(x, y, GRIDCOLOR);
  }
}

void ClearAndDrawGraph() {
  int clear;
  byte *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
  int disp_leng;
  if (full_screen) disp_leng = SAMPLES-1;
  else disp_leng = DISPLNG-1;
  bool ch1_active = ch1_mode != MODE_OFF && !(rate < RATE_DUAL && ch0_mode != MODE_OFF);
  if (sample == 0)
    clear = 2;
  else
    clear = 0;
  p1 = data[clear+0];
  p2 = p1 + 1;
  p3 = data[sample+0];
  p4 = p3 + 1;
  p5 = data[clear+1];
  p6 = p5 + 1;
  p7 = data[sample+1];
  p8 = p7 + 1;
#if 0
  for (int x=0; x<disp_leng; x++) {
    display.drawPixel(x, LCD_YMAX-data[sample+0][x], CH1COLOR);
    display.drawPixel(x, LCD_YMAX-data[sample+1][x], CH2COLOR);
  }
#else
  for (int x=0; x<disp_leng; x++) {
    if (ch0_mode != MODE_OFF) {
      display.drawLine(x, LCD_YMAX-*p1++, x+1, LCD_YMAX-*p2++, BGCOLOR);
      display.drawLine(x, LCD_YMAX-*p3++, x+1, LCD_YMAX-*p4++, CH1COLOR);
    }
    if (ch1_active) {
      display.drawLine(x, LCD_YMAX-*p5++, x+1, LCD_YMAX-*p6++, BGCOLOR);
      display.drawLine(x, LCD_YMAX-*p7++, x+1, LCD_YMAX-*p8++, CH2COLOR);
    }
  }
#endif
}

void ClearAndDrawDot(int i) {
  DrawGrid(i);
#if 0
  for (int x=0; x<DISPLNG; x++) {
    display.drawPixel(i, LCD_YMAX-odat01, BGCOLOR);
    display.drawPixel(i, LCD_YMAX-odat11, BGCOLOR);
    display.drawPixel(i, LCD_YMAX-data[sample+0][i], CH1COLOR);
    display.drawPixel(i, LCD_YMAX-data[sample+1][i], CH2COLOR);
  }
#else
  if (i < 1) {
    return;
  }
  if (ch0_mode != MODE_OFF) {
    display.drawLine(i-1, LCD_YMAX-odat00,   i, LCD_YMAX-odat01, BGCOLOR);
    display.drawLine(i-1, LCD_YMAX-data[0][i-1], i, LCD_YMAX-data[0][i], CH1COLOR);
  }
  if (ch1_mode != MODE_OFF) {
    display.drawLine(i-1, LCD_YMAX-odat10,   i, LCD_YMAX-odat11, BGCOLOR);
    display.drawLine(i-1, LCD_YMAX-data[1][i-1], i, LCD_YMAX-data[1][i], CH2COLOR);
  }
#endif
}

void scaleDataArray(byte ad_ch, int trig_point)
{
  byte *pdata, ch_mode, range, ch;
  short ch_off;
  uint16_t *idata, *qdata;
  long a;

  if (ad_ch == ad_ch1) {
    ch_off = ch1_off;
    ch_mode = ch1_mode;
    range = range1;
    idata = &cap_buf1[trig_point];
    ch = 1;
  } else {
    ch_off = ch0_off;
    ch_mode = ch0_mode;
    range = range0;
    idata = &cap_buf[trig_point];
    ch = 0;
  }
  pdata = data[sample+ch];
  for (int i = 0; i < SAMPLES; i++) {
    a = ((*idata++ + ch_off) * VREF[range] + 2048) >> 12;
    if (a > LCD_YMAX) a = LCD_YMAX;
    else if (a < 0) a = 0;
    if (ch_mode == MODE_INV)
      a = LCD_YMAX - a;
    *pdata++ = (byte) a;
  }
  if (rate < RATE_ROLL) {
    switch (time_mag) {
    case 2:
    case 5:
    case 10:
      mag(data[sample+ch], time_mag); // magnify timebase for display
      break;
    default:  // do nothing
      time_mag = 1;   // fix odd value
      break;
    }
  }
}

byte adRead(byte ch, byte mode, int off, int i)
{
  int16_t aa = analogRead(ch);
  long a = (((long)aa+off)*VREF[ch == ad_ch0 ? range0 : range1]+2048) >> 12;
  if (a > LCD_YMAX) a = LCD_YMAX;
  else if (a < 0) a = 0;
  if (mode == MODE_INV)
    a = LCD_YMAX - a;
  if (ch == ad_ch1) {
    cap_buf1[i] = aa;
  } else {
    cap_buf[i] = aa;
  }
  return a;
}

int advalue(int value, long vref, byte mode, int offs) {
  if (mode == MODE_INV)
    value = LCD_YMAX - value;
  return ((long)value << 12) / vref - offs;
}

void set_trigger_ad() {
  if (trig_ch == ad_ch0) {
    trigger_ad = advalue(trig_lv, VREF[range0], ch0_mode, ch0_off);
  } else {
    trigger_ad = advalue(trig_lv, VREF[range1], ch1_mode, ch1_off);
  }
}

void loop() {
  int oad, ad;
  unsigned long auto_time;

  timeExec = 100;
  if (rate > RATE_DMA) {
    set_trigger_ad();
    auto_time = pow(10, rate / 3) + 5;
    if (trig_mode != TRIG_SCAN) {
      unsigned long st = millis();
      oad = analogReadf(trig_ch);
      for (;;) {
        ad = analogReadf(trig_ch);

        if (trig_edge == TRIG_E_UP) {
          if (ad > trigger_ad && trigger_ad > oad)
            break;
        } else {
          if (ad < trigger_ad && trigger_ad < oad)
            break;
        }
        oad = ad;

        if (rate > RATE_SLOW)
          CheckSW();      // no need for fast sampling
        if (trig_mode == TRIG_SCAN)
          break;
        if (trig_mode == TRIG_AUTO && (millis() - st) > auto_time)
          break; 
      }
    }
  }

  // sample and draw depending on the sampling rate
  if (rate < RATE_ROLL && Start) {
    // change the index for the double buffer
    if (sample == 0)
      sample = 2;
    else
      sample = 0;

    if (rate <= RATE_DMA) {         // min 2.89us sampling (346ksps)
      sample_usf();                 // single channel sampling
    } else if (rate < RATE_SLOW) {  // dual channel 7us, 10us, 20us, 50us, 100us, 200us sampling
      sample_dual_usf(HREF[rate] / 10);
    } else {                // dual channel .5ms, 1ms, 2ms, 5ms, 10ms, 20ms sampling
      sample_dual_ms(HREF[rate] / 10);
    }
    draw_screen();
  } else if (Start) { // 50ms - 1000ms sampling
    timeExec = 5000;
    static const unsigned long r_[] PROGMEM = {50000, 100000, 200000, 500000, 1000000};
    unsigned long r;
    int disp_leng;
    if (full_screen) disp_leng = SAMPLES;
    else disp_leng = DISPLNG;
//    unsigned long st0 = millis();
    unsigned long st = micros();
    for (int i=0; i<disp_leng; i ++) {
      if (!full_screen && i >= DISPLNG) break;
      r = r_[rate - RATE_ROLL];  // rate may be changed in loop
      while((st - micros())<r) {
        CheckSW();
        if (rate < RATE_ROLL)
          break;
      }
      if (rate<RATE_ROLL) { // sampling rate has been changed
        display.fillScreen(BGCOLOR);
        break;
      }
      st += r;
      if (st - micros()>r)
          st = micros(); // sampling rate has been changed to shorter interval
      if (!Start) {
         i --;
         continue;
      }
      odat00 = odat01;      // save next previous data ch0
      odat10 = odat11;      // save next previous data ch1
      odat01 = data[0][i];  // save previous data ch0
      odat11 = data[1][i];  // save previous data ch1
      if (ch0_mode != MODE_OFF) data[0][i] = adRead(ad_ch0, ch0_mode, ch0_off, i);
      if (ch1_mode != MODE_OFF) data[1][i] = adRead(ad_ch1, ch1_mode, ch1_off, i);
      ClearAndDrawDot(i);
    }
    DrawGrid(disp_leng);  // right side grid
    // Serial.println(millis()-st0);
    if (!full_screen) DrawText();
  } else {
    DrawText();
  }
  if (trig_mode == TRIG_ONE)
    Start = false;
  CheckSW();
#ifdef EEPROM_START
  saveEEPROM();                         // save settings to EEPROM if necessary
#endif
}

void draw_screen() {
  if (wfft != fft_mode) {
    fft_mode = wfft;
    display.fillScreen(BGCOLOR);
  }
  if (fft_mode) {
    DrawText();
    plotFFT();
  } else {
    DrawGrid();
    ClearAndDrawGraph();
    if (!full_screen) DrawText();
  }
}

#define textINFO (DISPLNG-48)
void measure_frequency(int ch) {
  byte y = 1;
  freqDuty(ch);
  TextBG(&y, textINFO, 8);
  float freq = waveFreq[ch];
  if (freq < 999.5)
    display.print(freq);
  else if (freq < 999999.5)
    display.print(freq, 0);
  else {
    display.print(freq/1000.0, 0);
    display.print('k');
  }
  display.print("Hz");
  if (fft_mode) return;
  TextBG(&y, textINFO+18, 5);
  float duty = waveDuty[ch];
  if (duty > 99.9499) duty = 99.9;
  display.print(duty, 1);  display.print('%');
}

void measure_voltage(int ch) {
  int x;
  if (fft_mode) return;
  float vavr = VRF * dataAve[ch] / 40950.0;
  float vmax = VRF * dataMax[ch] / 4095.0;
  float vmin = VRF * dataMin[ch] / 4095.0;
  display.setCursor(textINFO, txtLINE2);
  display.print("max");  display.print(vmax); if (vmax >= 0.0) display.print('V');
  display.setCursor(textINFO, txtLINE3);
  display.print("avr");  display.print(vavr); if (vavr >= 0.0) display.print('V');
  display.setCursor(textINFO, txtLINE4);
  display.print("min");  display.print(vmin); if (vmin >= 0.0) display.print('V');
}

void sample_us(unsigned int r) { // single channel
  byte ch;
  uint16_t *p;
  if (ch0_mode == MODE_OFF && ch1_mode != MODE_OFF) {
    ch = ad_ch1;
    p = cap_buf1;
  } else {
    ch = ad_ch0;
    p = cap_buf;
  }
  unsigned long st = micros();
  for (int i=0; i<SAMPLES; i ++) {
    while(micros() - st < r) ;
    p[i] = analogReadf(ch);
    st += r;
  }
  scaleDataArray(ch, 0);
}

void sample_dual_ms(unsigned int r) { // dual channel. r > 500
// .5ms, 1ms or 2ms sampling
  unsigned long st = micros();
  for (int i=0; i<SAMPLES; i ++) {
    while(micros() - st < r) ;
    st += r;
    if (ch0_mode != MODE_OFF) {
      cap_buf[i] = analogRead(ad_ch0);
    }
    if (ch1_mode != MODE_OFF) {
      cap_buf1[i] = analogRead(ad_ch1);
    }
  }
  scaleDataArray(ad_ch0, 0);
  scaleDataArray(ad_ch1, 0);
}

void plotFFT() {
  byte *lastplot, *newplot;
  int ylim = LCD_HEIGHT - 8;

  int clear = (sample == 0) ? 2 : 0;
  for (int i = 0; i < FFT_N; i++) {
    vReal[i] = cap_buf[i];
    vImag[i] = 0.0;
  }
  FFT.dcRemoval();
  FFT.windowing(FFTWindow::Hann, FFTDirection::Forward);  // Weigh data
  FFT.compute(FFTDirection::Forward);                     // Compute FFT
  FFT.complexToMagnitude();                               // Compute magnitudes
  newplot = data[sample];
  lastplot = data[clear];
  for (int i = 1; i < FFT_N/2; i++) {
    float db = log10(vReal[i]);
    int dat = constrain((int)(20.0 * (db - 1.6)), 0, ylim);
    display.drawFastVLine(i * 2, ylim - lastplot[i], lastplot[i], BGCOLOR); // erase old
    display.drawFastVLine(i * 2, ylim - dat, dat, CH1COLOR);
    newplot[i] = dat;
  }
  draw_scale();
}

void draw_scale() {
  int ylim = LCD_HEIGHT - 8;
  float fhref, nyquist;
  display.setTextColor(TXTCOLOR);
  display.setCursor(0, ylim); display.print("0Hz"); 
  fhref = freqhref();
  nyquist = 5.0e6 / fhref; // Nyquist frequency
  if (nyquist > 999.0) {
    nyquist = nyquist / 1000.0;
    if (nyquist > 99.5) {
      display.setCursor(58, ylim); display.print(nyquist/2,0);display.print('k');
      display.setCursor(104, ylim); display.print(nyquist,0);
    } else if (nyquist > 9.95) {
      display.setCursor(58, ylim); display.print(nyquist/2,0);display.print('k');
      display.setCursor(110, ylim); display.print(nyquist,0);
    } else {
      display.setCursor(52, ylim); display.print(nyquist/2,1);display.print('k');
      display.setCursor(104, ylim); display.print(nyquist,1);
    }
    display.print('k');
  } else {
    display.setCursor(58, ylim); display.print(nyquist/2,0);
    display.setCursor(110, ylim); display.print(nyquist,0);
  }
}

float freqhref() {
  float fhref;
  if (rate <= RATE_DMA) {   // DMA sampling
    fhref = dmahref[rate];
  } else {
    fhref = (float) HREF[rate];
  }
  return fhref;
}

#ifdef EEPROM_START

void saveEEPROM() {                   // Save the setting value in EEPROM after waiting a while after the button operation.
  uint16_t p = EEPROM_START;
  if (saveTimer > 0) {                // If the timer value is positive
    saveTimer = saveTimer - timeExec; // Timer subtraction
    if (saveTimer <= 0) {             // if time up
      EEPROM.write(p++, range0);      // save current status to EEPROM
      EEPROM.write(p++, ch0_mode);
      EEPROM.write(p++, lowByte(ch0_off));  // save as Little endian
      EEPROM.write(p++, highByte(ch0_off));
      EEPROM.write(p++, range1);
      EEPROM.write(p++, ch1_mode);
      EEPROM.write(p++, lowByte(ch1_off));  // save as Little endian
      EEPROM.write(p++, highByte(ch1_off));
      EEPROM.write(p++, rate);
      EEPROM.write(p++, trig_mode);
      EEPROM.write(p++, trig_lv);
      EEPROM.write(p++, trig_edge);
      EEPROM.write(p++, trig_ch);
      EEPROM.write(p++, fft_mode);
      EEPROM.write(p++, info_mode);
      EEPROM.write(p++, item);
      EEPROM.write(p++, pulse_mode);
      EEPROM.write(p++, duty);
      EEPROM.write(p++, p_range);
      EEPROM.write(p++, lowByte(count));  // save as Little endian
      EEPROM.write(p++, highByte(count));
      EEPROM.write(p++, dds_mode);
      EEPROM.write(p++, wave_id);
      EEPROM.write(p++, ifreq & 0xff);
      EEPROM.write(p++, (ifreq >> 8) & 0xff);
      EEPROM.write(p++, (ifreq >> 16) & 0xff);
      EEPROM.write(p++, (ifreq >> 24) & 0xff);
      EEPROM.write(p++, time_mag);
      byte *q = (byte *) &compensation;
      for (int i = 0; i < 8; ++i)
        EEPROM.write(p++, *q++);
    }
  }
}
#endif

void set_default() {
  range0 = RANGE_MIN;
  ch0_mode = MODE_ON;
  ch0_off = 0;
  range1 = RANGE_MIN;
  ch1_mode = MODE_ON;
  ch1_off = 683;
  rate = 6;
  trig_mode = TRIG_AUTO;
  trig_lv = 20;
  trig_edge = TRIG_E_UP;
  trig_ch = ad_ch0;
  fft_mode = false;
  info_mode = 1;  // display frequency and duty.  Voltage display is off
  item = 0;       // menu item
  pulse_mode = true;
  duty = 128;     // PWM 50%
  p_range = 0;    // PWM range
  count = 35999;  // PWM 1kHz
  dds_mode = true;
  wave_id = 0;    // sine wave
  ifreq = 23841;  // 238.41Hz
  time_mag = 1;   // magnify timebase
  compensation = 1.0; // frequency counter
}

extern const byte wave_num;

#ifdef EEPROM_START
void loadEEPROM() { // Read setting values from EEPROM (abnormal values will be corrected to default)
  uint16_t p = EEPROM_START, error = 0;

  range0 = EEPROM.read(p++);                // range0
  if ((range0 < RANGE_MIN) || (range0 > RANGE_MAX)) ++error;
  ch0_mode = EEPROM.read(p++);              // ch0_mode
  if (ch0_mode > 2) ++error;
  *((byte *)&ch0_off) = EEPROM.read(p++);     // ch0_off low
  *((byte *)&ch0_off + 1) = EEPROM.read(p++); // ch0_off high
  if ((ch0_off < -4096) || (ch0_off > 8191)) ++error;

  range1 = EEPROM.read(p++);                // range1
  if ((range1 < RANGE_MIN) || (range1 > RANGE_MAX)) ++error;
  ch1_mode = EEPROM.read(p++);              // ch1_mode
  if (ch1_mode > 2) ++error;
  *((byte *)&ch1_off) = EEPROM.read(p++);     // ch1_off low
  *((byte *)&ch1_off + 1) = EEPROM.read(p++); // ch1_off high
  if ((ch1_off < -4096) || (ch1_off > 8191)) ++error;

  rate = EEPROM.read(p++);                  // rate
  if ((rate < RATE_MIN) || (rate > RATE_MAX)) ++error;
//  if (ch0_mode == MODE_OFF && rate < 5) ++error;  // correct ch0_mode
  trig_mode = EEPROM.read(p++);             // trig_mode
  if (trig_mode > TRIG_SCAN) ++error;
  trig_lv = EEPROM.read(p++);               // trig_lv
  if (trig_lv > LCD_YMAX) ++error;
  trig_edge = EEPROM.read(p++);             // trig_edge
  if (trig_edge > 1) ++error;
  trig_ch = EEPROM.read(p++);               // trig_ch
  if (trig_ch != ad_ch0 && trig_ch != ad_ch1) ++error;
  fft_mode = EEPROM.read(p++);              // fft_mode
  info_mode = EEPROM.read(p++);             // info_mode
  if (info_mode > 7) ++error;
  item = EEPROM.read(p++);                  // item
  if (item > ITEM_MAX) ++error;
  pulse_mode = EEPROM.read(p++);            // pulse_mode
  duty = EEPROM.read(p++);                  // duty
  p_range = EEPROM.read(p++);               // p_range
  if (p_range > 5) ++error;
  *((byte *)&count) = EEPROM.read(p++);     // count low
  *((byte *)&count + 1) = EEPROM.read(p++); // count high
  dds_mode = EEPROM.read(p++);              // DDS mode
  wave_id = EEPROM.read(p++);               // DDS wave id
  if (wave_id >= wave_num) ++error;
  *((byte *)&ifreq) = EEPROM.read(p++);     // ifreq low
  *((byte *)&ifreq + 1) = EEPROM.read(p++); // ifreq
  *((byte *)&ifreq + 2) = EEPROM.read(p++); // ifreq
  *((byte *)&ifreq + 3) = EEPROM.read(p++); // ifreq high
  if (ifreq > 999999L) ++error;
  time_mag = EEPROM.read(p++);               // magnify timebase
  byte *q = (byte *) &compensation;
  for (int i = 0; i < 8; ++i)
    *q++ = EEPROM.read(p++);
  if (compensation < 1.002 && compensation > 0.998) ; // OK
  else ++error;
  if (error > 0) {
    set_default();
  }
}
#endif
