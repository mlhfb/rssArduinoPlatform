#define CONFIG_ESP_INT_WDT_TIMEOUT_MS 4000
#define CONFIG_ESP_INT_WDT_CHECK_CPU1 0
//#include "soc/rtc_wdt.h"
#include <time.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <TinyXML.h>
#include <Adafruit_GFX.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>
#include <AiEsp32RotaryEncoder.h>
#ifndef PSTR
#define PSTR // Make Arduino Due happy
#endif

#define PIN 5               // PIN assigned to LED scroll data out
#define mw 128               // width of display
#define mh 8                // height of display
#define NUMMATRIX (mw * mh) // total number of pixels of panel
#define ROTARY_ENCODER_A_PIN 32
#define ROTARY_ENCODER_B_PIN 21
#define ROTARY_ENCODER_BUTTON_PIN 25
#define ROTARY_ENCODER_VCC_PIN 27 /*put -1 of Rotary encoder Vcc is connected directly to 3,3V; else you can use declared output pin for powering rotary encoder */

int scrolldelay = 20;
//Global variables messy but it works.  Need array for all items title, description, pubDate, link, guid
enum color
{
  amber,
  aqua,
  red,
  green,
  blue,
  white,
  yellow,
  purple,
  michBlue,
  michMaize
};
enum feeds
{
  nba,
  mlb,
  nfl,
  nhl,
  npr,
  weather
};
feeds feed;
enum messageType
{
  weathers,
  times,
  bullshit
};

struct message
{ // Random shit tho throw out every fith message, including time and weather
  const messageType type;
  char message1[300];
  char message2[300];
  char message3[300];
  int color1;
  int color2;
  int color3;
} messages[] = {
    {weathers, "", "", ""},
    {times, "", "", ""},
    {bullshit, "HEY!!!!   ", "         Having fun???", "", amber, michBlue, white},
    {bullshit, "WATCH YOUR STEP!! !! !! !!", "", "", red, white, white},
    {bullshit, "GO  GREEN!!!  ", "GO   WHITE!!! ", "", green, white, white},
    {bullshit, "How's your drink ?!?!?                  ", "    Time to fill 'er up ?? ?? ?", "       Fuck man... Are you sure  ???? ?? ????     ", aqua, michMaize, white},
    {bullshit, "MICHIGAN  ", "GO  BLUE!!!!", "", michMaize, michBlue, michBlue}};
int displayCount = 0; //track items scrolled to insert a message
int messageIndex = 0; //keep track of message location
int maxMessage = 6;   //Max messages (0-4)
struct weatherFlags
{ // set flags when tag for values are found so when the value is parsed we know that it's of the item.
  char *tag;
  bool flag;
  char data[30];
  char *attrTag;
  char *pre;
  char *post;
} weatherFlag[] = {
    {"/current/city", false, "", "name", "Weather for ", " Michigan ...The Prison City... "},
    {"/current/temperature", false, "", "value", "Current Tempurature: ", "F "},
    {"/current/humidity", false, "", "value", "Humidity ", "% "},
    {"/current/pressure", false, "", "value", " Pressure ", " kpa "},
    {"/current/wind/speed", false, "", "value", " Wind ", " mph"},
    {"/current/wind/direction", false, "", "name", " from the ", "  "},
    {"/current/clouds", false, "", "name", " .... ", "...."}};
struct site_t
{ // Structure of websites to obtain items of interest.
  const char *title;
  const char *url;
  const feeds feed;
} sites[] = {
    {"scorepro.com", "https://api.openweathermap.org/data/2.5/weather?id=4997384&appid=63fb95ee3d23461df79f5cd0c0a4febc&mode=xml&units=imperial", weather},
    {"scorepro.com", "https://www.scorespro.com/rss2/live-baseball.xml", mlb},

    {"scorepro.com", "https://www.scorespro.com/rss2/live-hockey.xml", nhl},
    {"scorepro.com", "https://www.scorespro.com/rss2/live-basketball.xml", nba},
    {"scorepro.com", "https://feeds.npr.org/1001/rss.xml", npr}

    //{"CNN.com", "http://rss.cnn.com/rss/edition.rss", "title"},
    //{"BBC News", "http://feeds.bbci.co.uk/news/rss.xml", "description"},
};
struct rssFeed
{ // The web pages that get itterated over using 3 common RSS tags... it's all built around this so these three is what you got and the makeScroll only handles tite and description AUG 2020
  //char *currentTag;
  char title[300];
  char desc[300];
  char pubDate[300];
};
struct rssFeed rss[20];    // This is likely is not enough for NCAA, NBA, CBA when in full schedule... address by 2021 ;)
struct rssFeed scroll[20]; // likely enough as this only contains one sites worth of items of interest only NHL, MLB, 3 basketball teams....
const char *ssid = "MMM";
const char *password = "napieralski";
int httpGetChar();    // I'm not 100% sure, but the funtion httpGetChar is a very clever way to pass HTTP stream to TinyXML... well cleaver for a n00b
int rssIndex = 0;     // index of rss[] globally so the tinyXML callback has scope
int siteIndex = 0;    // index of sites[] globally so the tinyXML callback has scope
int scrollIndex = 0;  // index of scroll[] when extracting items of intrest from rss[]
int dataLenIndex = 0; // used in the tinyXml callback to concat weather items into rss[].title
int pass = 0;         // used to cycle display color [8]
int retry = 0;
const char *ntpServer = "pool.ntp.org"; // address for NTP time server
const long gmtOffset_sec = -18000;      // Offset in seconds from GMT (time zone)
const int daylightOffset_sec = 3600;    // daylight savings offset
time_t myTime;
char *tm_wday[7]{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char titleCopy[300];        // used in makeScroll to parse text to identify items of interest program crashes when scoped locally
char descCopy[300];         // used in makeScroll to parse text to identify items of interest program crashes when scoped locally
char pubCopy[300];          // used in makeScroll to parse text to identify items of interest program crashes when scoped locally
char printMe[300];          // used in makeScroll to parse text to identify items of interest program crashes when scoped locally
uint8_t buffer[2000];       // buffer used by TinyXML to parse xml pages
bool commitFlag = false;    // flag used in the TinyXML callback defined glabbaly for scope
HTTPClient http;            // initiate HTTPClient Object
WiFiMulti wifiMulti;        // initiate WiFi instance
WiFiClient *stream;         // initiate TCP stream... I think it's a TCP stream anyway
TinyXML xml;                // intiate TinyXML Object
CRGB matrixleds[NUMMATRIX]; // initiate fast LED ARRAY Object
// Define the LED MATRIX size,height,width per segment, count of defined panels,  1st LED location top/bottom, 1st LED location left/right, led connected as rows or columns, zigzag or proggresive
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(matrixleds, 8, mh, mw / 8, 1, NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
const uint16_t colors[] = { // Color cycle per item of interest
    matrix->Color(255, 195, 0), matrix->Color(0, 255, 255), matrix->Color(200, 0, 0), matrix->Color(0, 255, 0), matrix->Color(0, 0, 255), matrix->Color(245, 245, 245), matrix->Color(255, 255, 0), matrix->Color(255, 0, 255), matrix->Color(0, 50, 152), matrix->Color(255, 203, 5)};
//enum color {    amber,                    aqua,                 red,                            green,                  blue,             white,                                  yellow,                       purple,                       michBlue,           michMaize };
int maxColors = 10;
struct tm timeinfo;
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, ROTARY_ENCODER_BUTTON_PIN, ROTARY_ENCODER_VCC_PIN);
int test_limits = 2;
bool setdelay = false;
bool nextstory = false;
int bright = 15;
void rotary_onButtonClick() {
	//rotaryEncoder.reset();
	//rotaryEncoder.disable();
  Serial.println("Button");
	//rotaryEncoder.setBoundaries(-test_limits, test_limits, false);
  //test_limits *= 2;
  //setdelay = !setdelay;
  nextstory = true;
}
void rotary_loop() {
	//first lets handle rotary encoder button click
	if (rotaryEncoder.currentButtonState() == BUT_RELEASED) {
		//we can process it here or call separate function like:
		rotary_onButtonClick();
	}

	//lets see if anything changed
	int16_t encoderDelta = rotaryEncoder.encoderChanged();
	
	//optionally we can ignore whenever there is no change
	if (encoderDelta == 0) return;
	
	//for some cases we only want to know if value is increased or decreased (typically for menu items)
	if (encoderDelta>0) Serial.print("+");
	if (encoderDelta<0) Serial.print("-");

	//for other cases we want to know what is current value. Additionally often we only want if something changed
	//example: when using rotary encoder to set termostat temperature, or sound volume etc
	
	//if value is changed compared to our last read
	if (encoderDelta!=0) {
		//now we need current value
		int16_t encoderValue = rotaryEncoder.readEncoder();
		//process new value. Here is simple output.
		Serial.print("Value: ");
		Serial.println(encoderValue);
    if (setdelay) {
      if (encoderDelta>0 && scrolldelay < 1000) ++scrolldelay ;
	    if (encoderDelta<0 && scrolldelay > 0 ) --scrolldelay;
    }    
    if (!setdelay) {
      if (encoderDelta>0 && bright > 0) --bright;
      if (encoderDelta<0 && bright < 255) ++bright;  
      matrix->setBrightness(bright);
    }


    //scrolldelay = scrolldelay + encoderDelta;
    }
	} 
	

int findNULL(char *s)
{
  for (int i = 0; i < 300; i++)
  {
    if (s[i] == NULL)
    {
      return i;
    }
  }
  return 300;
}

char *removeString(char s[], size_t offset, size_t length)
{ // string to modify, where to start, how long
  if (memchr(s, '\0', offset))
  {
    return s;
  }
  char *dest = s + offset;
  if (memchr(dest, '\0', length))
  {
    *dest = '\0';
    return dest;
  }
  /* Fixed error pointed out by JS1 */
  for (const char *src = dest + length; *dest != '\0'; ++dest, ++src)
  {
    *dest = *src;
  }
  return s;
}

void getTime()
{
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
}

void poundKiller()
{
  int eos = findNULL(scroll[scrollIndex].title);
  for (int killIt = 0; 0 < eos; killIt++)
  {
    if (scroll[scrollIndex].title[killIt] == '#')
    {
      removeString(scroll[scrollIndex].title, killIt, 1);
      eos--;
    }
  }
}

void scrollMe()
{ //int dcolor = pass
//return;
  
  int xw = matrix->width();
  int printIndex = findNULL(printMe);

  while (true)
  {
    matrix->fillScreen(0);
    matrix->setCursor(xw, 0);
    // matrix->setTextColor(colors[dcolor]);
    matrix->print(F(printMe));
    if (--xw < (printIndex * 6) * (-1))
    {
      xw = matrix->width();
      if (++pass > maxColors)
        pass = 0;
      matrix->setTextColor(colors[pass]);
      return;
    }
    
   
    // Serial.print(pass);
    // Serial.print(" ");
    // Serial.println(xw);
    matrix->show();
    int delaymarker = millis() + scrolldelay;
    while(millis() < delaymarker){
        yield();
    }
    rotary_loop();
    if(nextstory){
      nextstory = false;
      return;
    }
  }
}

void scrollMe(int dcolor)
{ 
  //return;
  //int dcolor = pass
  Serial.println("Working with: ");
  Serial.println(dcolor);
  int xw = matrix->width();
  int printIndex = findNULL(printMe);
  while (true)
  {
    matrix->fillScreen(0);
    matrix->setCursor(xw, 0);
    matrix->setTextColor(colors[dcolor]);
    matrix->print(F(printMe));
    if (--xw < (printIndex * 6) * (-1))
    {
      xw = matrix->width();
      if (++pass > maxColors)
        pass = 0;
      matrix->setTextColor(colors[pass]);
      return;
    }
    
    // Serial.print(pass);
    // Serial.print(" ");
    // Serial.println(xw);
    matrix->show();
    int delaymarker = millis() + scrolldelay;
    while(millis() < delaymarker){
        yield();
    }
    rotary_loop();


    
  }
}

void scrollIt()
{ // itterate over scroll index combining title and description
  Serial.println("IN SCROLLIT()");
  for (int i = 0; i < scrollIndex; i++)
  {
    Serial.print(i);
    Serial.print(" ");
    int titleIndex = findNULL(scroll[i].title);
    int descIndex = titleIndex + findNULL(scroll[i].desc);
    for (int ii = 0; ii < titleIndex; ii++)
    {
      printMe[ii] = scroll[i].title[ii];
    }
    for (int spc = titleIndex; spc < titleIndex + 2; spc++)
    {
      printMe[spc] = ' ';
    }
    titleIndex = titleIndex + 2;
    descIndex = descIndex + 2;
    for (int xx = titleIndex; xx < descIndex + 1; xx++)
    {
      printMe[xx] = scroll[i].desc[xx - titleIndex];
    }
    Serial.println(printMe);
    scrollMe();
    Serial.print("displayCount: ");
    Serial.print(displayCount);
    Serial.print(" messageIndes: ");
    Serial.println(messageIndex);
    displayCount++;
    if (displayCount > 4)
    {
      displayCount = 0;
      switch (messages[messageIndex].type)
      {
      case weathers:
        memcpy(printMe, messages[messageIndex].message1, 300);
        scrollMe();
        break;
      case times:
        getTime();
        strftime(printMe, sizeof(printMe), "%A, %B %d %Y %H:%M:%S", &timeinfo);
        scrollMe();
        break;
      case bullshit:
        memcpy(printMe, messages[messageIndex].message1, 300);
        Serial.println(messages[messageIndex].color1);
        scrollMe(messages[messageIndex].color1);
        memcpy(printMe, messages[messageIndex].message2, 300);
        Serial.println(messages[messageIndex].color2);
        scrollMe(messages[messageIndex].color3);
        memcpy(printMe, messages[messageIndex].message3, 300);
        Serial.println(messages[messageIndex].color3);
        scrollMe(messages[messageIndex].color3); //(messages[messageIndex].color3)
        break;
      }
      messageIndex++;
      if (messageIndex > maxMessage)
      {
        messageIndex = 0;
      }
    }
  }
}

void XML_callback(uint8_t statusflags, char *tagName, uint16_t tagNameLen, char *data, uint16_t dataLen)
{ //This method should case the url type and produce a formatted scroll struct of items in the XML
  /*    
 STATUS FLAG structure      
      #define STATUS_START_TAG 0x01
      #define STATUS_TAG_TEXT  0x02
      #define STATUS_ATTR_TEXT 0x04
      #define STATUS_END_TAG   0x08
      #define STATUS_ERROR     0x10
*/
  yield();
  // Serial.print(statusflags==2);
  // Serial.print(statusflags==4);

  /*   DEBUG  
  Serial.println(!strcmp(tagName, "/rss/channel/item/title"));
  Serial.print(tagName);
  Serial.print(" ");
  Serial.print("statusflags:    ");
  Serial.print(statusflags);
  Serial.print("    tagName    :");
  Serial.print(tagName);
  Serial.print("    tagNameLen     :");
  Serial.print(tagNameLen);
  Serial.print("     data:     ");
  Serial.print(data);
  Serial.print("    dataLen:    ");
  Serial.print(dataLen);
*/
  if (statusflags == 2)
  {
    if (!strcmp(tagName, "/rss/channel/item/title") || !strcmp(tagName, "/rss/item/title"))
    {
      memcpy(rss[rssIndex].title, data, dataLen);
      rss[rssIndex].title[dataLen] = (char)0;
      //memcpy( rss[rssIndex].title, data, dataLen);
      //Serial.print("Stored record #: ");
      //Serial.print (rssIndex);
      //Serial.println(data);
      //Serial.println(rss[rssIndex].title);
      commitFlag = true;
    }
    if (!strcmp(tagName, "/rss/channel/item/description") || !strcmp(tagName, "/rss/item/description"))
    {

      memcpy(rss[rssIndex].desc, data, dataLen);
      rss[rssIndex].desc[dataLen] = (char)0;
      //Serial.print("Stored record #: ");
      //Serial.print (rssIndex);
      //Serial.println(data);
      //Serial.println(rss[rssIndex].desc);
      commitFlag = true;
    }
    if (!strcmp(tagName, "/rss/channel/item/pubDate") || !strcmp(tagName, "/rss/item/pubDate"))
    {
      memcpy(rss[rssIndex].pubDate, data, dataLen);
      rss[rssIndex].pubDate[dataLen] = (char)0;
      //Serial.print("Stored record #: ");
      //Serial.print (rssIndex);
      //Serial.print (rssIndex);
      //Serial.println(data);
      //Serial.println(rss[rssIndex].pubDate);
      commitFlag = true;
    }
  }

  ////////////////////////   WEATHER CONSTRUCTORS ////////////////////
  if (statusflags == 1 && sites[siteIndex].feed == weather)
  { // flags on the flag names for attributes
    for (int i = 0; i < sizeof(weatherFlag) / sizeof(struct weatherFlags); i++)
    {
      if (!strcmp(tagName, weatherFlag[i].tag))
      {
        weatherFlag[i].flag = true;
        // Serial.print(i);
        //Serial.print(" TRUE ");
      }
    }
  }

  if (statusflags == 2 && sites[siteIndex].feed == weather)
  { // flags on the flag names for attributes
    //Serial.println("IN WEATHER flag 2 do nothing");
  }

  if (statusflags == 4 && sites[siteIndex].feed == weather)
  {
    //  Serial.println("IN WEATHER flag 4 npr gets here?");
    for (int i = 0; i < sizeof(weatherFlag) / sizeof(struct weatherFlags); i++)
    {
      if (!strcmp(tagName, weatherFlag[i].attrTag) && weatherFlag[i].flag == true)
      {
        memcpy(weatherFlag[i].data, data, dataLen);
        weatherFlag[i].data[dataLen] = 0;
        // Serial.print(data);
      }
    }
  }
  if (statusflags == 8 && sites[siteIndex].feed == weather)
  { // flags off the flag names for attributes
    //  Serial.println("IN WEATHER flag 8");
    for (int i = 0; i < sizeof(weatherFlag) / sizeof(struct weatherFlags); i++)
    {
      if (!strcmp(tagName, weatherFlag[i].tag))
      {
        weatherFlag[i].flag = false;
        // Serial.println(" FALSE");
      }
    }
  }
  ////////////////////////   WEATHER CONSTRUCTORS ////////////////////

  if (statusflags == 8)
  {
    if (!strcmp(tagName, "/rss/channel/item") || !strcmp(tagName, "/rss/item/title"))
    {
      if (commitFlag)
      {
        rssIndex++;
        commitFlag = false;
      }
    }
  }
}
void parseWeather()
{
  int bufIndex = 0;
  int isNull = 0;
  for (int i = 0; i < 300; i++)
  {
    rss[rssIndex].title[i] = (int)0;
  }
  Serial.println("ParsingWeather");
  for (int i = 0; i < sizeof(weatherFlag) / sizeof(struct weatherFlags); i++)
  {
    Serial.print(weatherFlag[i].pre);
    Serial.print(weatherFlag[i].data);
    Serial.println(weatherFlag[i].post);
    isNull = findNULL(weatherFlag[i].pre);
    memcpy(rss[rssIndex].title + bufIndex, weatherFlag[i].pre, isNull);
    bufIndex = bufIndex + isNull;
    isNull = findNULL(weatherFlag[i].data);
    memcpy(rss[rssIndex].title + bufIndex, weatherFlag[i].data, isNull);
    bufIndex = bufIndex + isNull;
    isNull = findNULL(weatherFlag[i].post);
    memcpy(rss[rssIndex].title + bufIndex, weatherFlag[i].post, isNull);
    bufIndex = bufIndex + isNull;
    Serial.print(weatherFlag[i].pre);
    Serial.print(weatherFlag[i].data);
    Serial.println(weatherFlag[i].post);
    Serial.println(rss[rssIndex].title);
  }
  rss[rssIndex].desc[0] = (char)0;
  for (int i = 0; i < sizeof(messages) / sizeof(struct message); i++)
  {
    if (messages[i].type == weathers)
    {
      memcpy(messages[i].message1, rss[rssIndex].title, 300);
    }
  }
  rssIndex++;
}
void matchFinal()
{
  if (!strcmp(scroll[scrollIndex].desc, "Match Finished"))
  {
    memcpy(scroll[scrollIndex].desc, "Final", 300);
  }
}
void makeScroll(int i)
{
  ////  This section formats RSS feeds for display Should be a call.
  scrollIndex = 0;
  for (int ii = 0; ii < rssIndex; ii++)
  { //itterate over feed looking for things to display  hodge podge of checkers is bug prone... need some flags in sites struct builds scroll[x] rss feed
    //Serial.print("size of rssfeed : ");
    //Serial.println(sizeof(rss)/ sizeof(struct rssFeed));
    //  Serial.print(" count : ");
    //   Serial.println(ii);
    memcpy(descCopy, rss[ii].desc, 300);
    memcpy(pubCopy, rss[ii].pubDate, 300);
    memcpy(titleCopy, rss[ii].title, 300);
    switch (sites[i].feed)
    {
    case weather:
      memcpy(scroll[scrollIndex].title, titleCopy, 300);
      memcpy(scroll[scrollIndex].desc, descCopy, 300);
      memcpy(scroll[scrollIndex].pubDate, pubCopy, 300);
      scrollIndex++;
      break;

    case npr:
      memcpy(scroll[scrollIndex].title, titleCopy, 300);
      memcpy(scroll[scrollIndex].desc, descCopy, 300);
      memcpy(scroll[scrollIndex].pubDate, pubCopy, 300);
      scrollIndex++;
      break;

    case nhl:
      removeString(removeString(titleCopy, 0, 35), 3, 1);
      if (titleCopy[0] == 'U')
      { //  modifies title for NHL games
        memcpy(scroll[scrollIndex].title, removeString(titleCopy, 0, 4), 300);
        memcpy(scroll[scrollIndex].desc, descCopy, 300);
        memcpy(scroll[scrollIndex].pubDate, pubCopy, 300);
        matchFinal();
        poundKiller();
        scrollIndex++;
        break;
      }
      break;

    case nba:
      if (removeString(removeString(titleCopy, 0, 40), 3, 1)[0] == 'N')
      { //  modifies title for NBA games
        memcpy(scroll[scrollIndex].title, removeString(titleCopy, 0, 4), 300);
        memcpy(scroll[scrollIndex].desc, descCopy, 300);
        memcpy(scroll[scrollIndex].pubDate, pubCopy, 300);
        matchFinal();
        poundKiller();
        scrollIndex++;
        break;
      }
      break;

    case mlb:
      removeString(removeString(titleCopy, 0, 39), 3, 1);
      if (titleCopy[0] == 'L')
      { //  modifies title for NBA games
        memcpy(scroll[scrollIndex].title, removeString(titleCopy, 0, 4), 300);
        memcpy(scroll[scrollIndex].desc, descCopy, 300);
        memcpy(scroll[scrollIndex].pubDate, pubCopy, 300);
        matchFinal();
        poundKiller();
        scrollIndex++;
        break;
      }
      break;
    } // end of switch
    //Send title through the # killer
  }
}

void getXml(const char *url)
{ //take a URL and turn it into the line String for parsing
  xml.reset();
  bool successFlag = false;
  http.setReuse(false);
  http.setTimeout(5000);
  while (!successFlag)
  {
    if ((wifiMulti.run() == WL_CONNECTED))
    { //make sure connected
      Serial.println("Connected!!!");

      http.begin(url); // connect to site
      int httpCode = http.GET();
      if (httpCode > 0)
      {
        if (httpCode == HTTP_CODE_OK)
        {
          Serial.println("readWebPage");
          stream = http.getStreamPtr();
          Serial.println("Trying to read string");
          while (stream->available())
          {
            // Serial.println(httpCode);
            char hgc = httpGetChar();
            // Serial.print(hgc);
            //Serial.print(hgc);
            xml.processChar(hgc);
            // Serial.print(".");
            //line = stream->readString();
            // Serial.print(line);}
            //yield();
          }
        }
        else
        {
          Serial.println("Failed to reach HOST");
          http.end();
          //http.stop()
          delay(2000);
          retry++;
        }
      }
      Serial.println("httpCode at exit [0=EOF/NULL] ");
      Serial.print(httpCode);

      http.end();
      // http.stop();

      Serial.println("The End");
      successFlag = true;
    }
    else
    {
      wifiMulti.addAP(ssid, password);
      Serial.println("Waiting for INTERNET!!!!");
      delay(4000);
    }
  } //end of While
}
int httpGetChar()
{ // returns the page one byte? at a time?  Whole page into shoddyxml instance?
  if (http.connected())
  {
    if (stream->available())
    {
      char ch = stream->read();
      //Serial.print(ch);
      return ch;
    }
    else
    {
      return 0;
    }
  }
  return EOF;
}

void setup()
{

  //rtc_wdt_protect_off();
  //rtc_wdt_disable();

  xml.init((uint8_t *)buffer, sizeof(buffer), &XML_callback);

  Serial.begin(115200);
  FastLED.addLeds<NEOPIXEL, PIN>(matrixleds, NUMMATRIX);
  matrix->begin();
  matrix->setTextWrap(false);
  matrix->setBrightness(bright);
  matrix->setTextColor(colors[0]);
  wifiMulti.addAP(ssid, password);
  Serial.print("Connecting.");
  while (!(wifiMulti.run() == WL_CONNECTED))
  {
    delay(2000);
    Serial.print(",");
    if (!(wifiMulti.run() == WL_CONNECTED))
    {
      WiFi.disconnect(true);
      delay(1000);
      Serial.print("#");
      wifiMulti.addAP(ssid, password);
      Serial.print("*");
      delay(1000);
    }
  }
  /*
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
   if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    
  } else {
//    getLocalTime(&myTime);
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  }
  */
  rotaryEncoder.begin();
	rotaryEncoder.setup([]{rotaryEncoder.readEncoder_ISR();});
	//optionally we can set boundaries and if values should cycle or not
	rotaryEncoder.setBoundaries(-1000, 1000, false); //minValue, maxValue, cycle values (when max go to min and vice versa)
}

void loop()
{
  for (siteIndex = 0; siteIndex < sizeof(sites) / sizeof(struct site_t); siteIndex++)
  {
    Serial.println(sites[siteIndex].url);
    getXml(sites[siteIndex].url); //pull the page parse the XML.  XML callback itterates over XML file
    while (retry > 0)
    {
      if (retry == 5)
      {
        retry = 0;
        break;
      }
      Serial.print(sites[siteIndex].url);
      Serial.print(" retries: ");
      Serial.println(retry);
      delay(2000);
      getXml(sites[siteIndex].url);
    }

    if (sites[siteIndex].feed == weather)
    {
      parseWeather();
    }
    yield();
    makeScroll(siteIndex);
    scrollIt();
    rssIndex = 0;
    getTime();
    strftime(printMe, sizeof(printMe), "%H:%M:%S", &timeinfo);
    Serial.println(printMe);
    scrollMe();
  }

  // delay(20000);
}
