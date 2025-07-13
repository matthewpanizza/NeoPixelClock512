////////////////////////////
//// INCLUDED LIBRARIES ////
////////////////////////////

#include "neopixel.h"
#include "math.h"

/////////////////////////////////////
// NEOPIXEL MATRIX CHARACTERISTICS //
/////////////////////////////////////

#define PIXEL_COUNT 512         //Number of pixels in matrix

#define PIXEL_PIN A3            //Define Hardware pin used for data

#define PIXEL_TYPE WS2812B       //Define LED Type

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, PIXEL_TYPE);    //Initialize neopixel function


////////////////////////////////////
/////// NIGHT MODE CONFIG //////////
////////////////////////////////////

#define dnbound 60              //Photoresistor value to initiate Night Mode

#define upbound 250             //Photoresistor value to exit Night Mode once entered, must be greater than dnbound

//  *Special note: values can be experimentally found through the particle console or app


////////////////////////////////////
//// HARDWARE CONFIGURATION ////////
////////////////////////////////////

SYSTEM_MODE(AUTOMATIC);         //Tells device to use WiFi by default

SYSTEM_THREAD(ENABLED);

#define upbtn D0                //Defines button configuration for navigation buttons

#define enbtn D1

#define dnbtn D2

#define spkpn D3                //Define Speaker pin (optional)

#define brtsns A4               //Define Photoresistor pin (use 10k pulldown resistor)

#define KEY_CODE 250            //Code stored in the first location of the eeprom to check if a first-time write is needed for a new MCU

////////////////////////////////////
//////// EEPROM ADDRESSES //////////
////////////////////////////////////
// EEPROM 0: Program Key Code    ///
// EEPROM 1: Time zone offset    ///
// EEPROM 2: Weather Data toggle ///
// EEPROM 3: CO2 Data toggle     ///
// EEPROM 4: Indoor temp toggle  ///
// EEPROM 5: Dark Mode Color     ///
// EEPROM 6: Mini Clock          ///
////////////////////////////////////


////////////////////////////////////
////// GLOBAL VARIABLE LIST ////////
////////////////////////////////////

int mprev;      //Previous Minute
int hprev;      //Previous Hour
int bclock;     //Blue Reference
int rclock;     //Red Reference
int gclock;     //Green Reference
int photo;      //variable for most recent photoresistor value
int bound;      //Boundary variable for night/day mode
int scan;       //Flag to do a conversion on the light sensor
int tmr;        //Timer variable for animations 
int TFahr;      //Global variable for outdoor temperature from weather webhook
int humidity;   //Global variable for outdoor humidity from weather webhook
int wmode;      //Main counter for which weather mode is currently being displayed
int dmode;      //Flag for choosing display mode, 1 is condition cycle, 2 is all in line
int cid;        //Global variable for outdoor condition id from weather webhook
char cltr[1];   //Global variable for outdoor condition mode (day or night from openweather webhook)
int i, j;       //Global loop variables, since these are used so much, optimize re-declaration of local vars
int itemp;      //Global for indoor temperature sensor, deprecated
int ico2;       //Global for indoor CO2 sensor, deprecated
int fdark;      //Flag to erase freezing temperature indicator upon startup
int screenArray[512];   //Array of pixels in array, with encoded 24-bit color
uint32_t lastUpdate;    //Timer for requesting data from openweather webhook
char dowVal[7];         //Stonks webhook string
bool trueTone;      //Flag to warm up temperature when in lower ambient light

//STARTUP(WiFi.selectAntenna(ANT_INTERNAL));

//Function run once on startup to initialize variables
void setup() {
    lastUpdate = 0;             //Set lastupdate to 0 so weather is updated soon
    pinMode(D0, INPUT_PULLDOWN);//Pin Mode setter, these inputs are buttons
    pinMode(D1, INPUT_PULLDOWN);
    pinMode(D2, INPUT_PULLDOWN); 
    pinMode(D7,OUTPUT);         //Enable output on onboard blue LED
    RGB.control(true);          //Take control of the onboard RGB LED
    RGB.color(0, 0, 0);         //Turn off onboard LED so it's not on when the room is dark
    scan = 1;                   //scans photoresistor for room brightness
    wmode = 1;                  //Start initially displaying temperature
    dmode = 1;                  //Initially displaying condition cycle mode
    fdark = 2;                  //Turn off freezing outdoor temp indicator
    trueTone = true;            //Turn on temperature warming by default
    strip.begin();              //Initialize neopixel matrix
    strip.show();
    for(j=0; j < 512; j++){     //Initialize matrix array for display
        screenArray[j] = 0;
    }
    photo = analogRead(A4)/18;  //Take initial reading of ambient light
    strDisp("Hello",56,photo,photo,photo,true); //Display "Hello" until we have connected to Wi-Fi
    for(i = 56; i < 200; i+=8){     //Startup animation
        printScreen(screenArray,i,i+7);
        delay(75);
    }
    waitFor(Particle.connected,100000);     //Wait for a long time for Wi-Fi connection, otherwise we have an invalid time
    for(j=0; j < 256; j++){     //Erase top half of screen where startup animations were
        screenArray[j] = 0;
    }
    uint8_t keyCode = EEPROM.read(0);   //Load the first EEPROM location which has the key code corresponding to this program (Allows swapping of MCUs)
    if(keyCode != KEY_CODE){
        EEPROM.write(0,KEY_CODE);
        EEPROM.write(1,17);     //Manual setting of EEPROMs
        EEPROM.write(2,1);
        EEPROM.write(3,0);
        EEPROM.write(4,0);
        EEPROM.write(5,1);
        EEPROM.write(6,0);
    }
    Particle.variable("Photo", photo);      //Open up ambient light sensor variable for cloud reads
    Particle.subscribe("hook-response/Weather", weatherHandler, MY_DEVICES);             //Subscribes to Weather API event
    Time.zone(12-EEPROM.read(1));
    mprev=(Time.minute()-1);
    hprev=(Time.hourFormat12()-1);
    bound = dnbound;
    delay(50);
    checkForUpdate(true);
}

//Functions to convert an encoded color value to a 0-255 individual color (R, G, or B)
int getRVal(int colorCode){                                                                                         //Filters out Red value from array element
    return ((colorCode/1000000)%1000);
}
int getGVal(int colorCode){                                                                                         //Filters out Green value from array element
    return ((colorCode/1000)%1000);
}
int getBVal(int colorCode){                                                                                         //Filters out Blue value from array element
    return (colorCode%1000);
}
//Dynamic function for updating neopixel array based on contents of software encoded array, start and stop take pixel ranges so partial sections can be updated
void printScreen(int inputArray[], int start, int end){
    uint16_t count;
    for(count = start; count <= end; count++){
        if(!(count>>8)){        //Update pixels for the array between 0 and 255
            if(((count/8)%2) == 0){                                                                                        //Prints standard row
                strip.setPixelColor(count,getRVal(inputArray[count]),getGVal(inputArray[count]), getBVal(inputArray[count]));
            }
            else{                                                                                                       //Reversed row for S configuration (mirror function)
                if((count%8) < 4){
                    strip.setPixelColor(count+7-((count%8)*2),getRVal(inputArray[count]),getGVal(inputArray[count]), getBVal(inputArray[count]));     //If on the right half of line, add to mirror to other side
                }
                else{
                    strip.setPixelColor(count+7-(2*(count%8)),getRVal(inputArray[count]),getGVal(inputArray[count]), getBVal(inputArray[count]));     //If on the left half of line, subtract to mirror to other side
                }
            }
        }
        else if ((count>>8) && count < 383){       //Bottom-right quadrant // Red - 0.65, Green - 0.8, Blue - 0.65
            int k = count-256;
            if(((count%8)%2) == 0){                                                                                        //Prints standard row
                strip.setPixelColor(256 + 16*(7-k%8) + 15-(k/8),round(getRVal(inputArray[count])*0.65),round(getGVal(inputArray[count])*0.8), round(getBVal(inputArray[count])*0.65));
            }
            else{                                                                                                       //Reversed row for S configuration (mirror function)
                strip.setPixelColor(256 + 16*(7-k%8) + (k/8),round(getRVal(inputArray[count])*0.65),round(getGVal(inputArray[count])*0.8), round(getBVal(inputArray[count])*0.65));
            }
        }
        else if (count > 383 && count < 512){       //Bottom-left quadrant
            int k = count-384;      
            if(((count%8)%2) == 0){                                                                                        //Prints standard row
                strip.setPixelColor(384 + 16*(7-k%8) + 15-(k/8),round(getRVal(inputArray[count])*0.65),round(getGVal(inputArray[count])*0.8), round(getBVal(inputArray[count])*0.65));
            }
            else{                                                                                                       //Reversed row for S configuration (mirror function)
                strip.setPixelColor(384 + 16*(7-k%8) + (k/8),round(getRVal(inputArray[count])*0.65),round(getGVal(inputArray[count])*0.8), round(getBVal(inputArray[count])*0.65));
            }
        }
    }
    strip.show();
}

//Function to return a pixel bitmap for a small-sized number based on an inputted decimal number like '9' or '5'
uint32_t snum(int val) {//Code block for displaying smaller 3x5 numbers, pix arg is the top left pixel, num is the number

// Each number has binary encoding based on offset from the initial pixel. 
// Chart below shows offsets. Pixels are picked to form numbers, then add up all 2^n pixels to get decimal number
// 0 8  16  
// 1 9  17  
// 2 10 18  
// 3 11 19  
// 4 12 20  
// 
// 
if(val > 9 || val < 0) return 0;    //Don't overflow the array
uint32_t numArray[10] = { 2035999, 2031616, 1512733, 2037013, 2032647, 1905943, 1905951, 2031873, 2037023, 2037015};
return numArray[val];

}
//Function to return a pixel bitmap for a normal-sized number based on an inputted decimal number like '9' or '5'
uint32_t num(int val) {       //Code block for displaying larger 4x7 numbers, pix arg is the top left pixel, num is the number

// Each number has binary encoding based on offset from the initial pixel. 
// Chart below shows offsets. Pixels are picked to form numbers, then add up all 2^n pixels to get decimal number
// 0 8  16  24
// 1 9  17  25
// 2 10 18  26
// 3 11 19  27
// 4 12 20  28
// 5 13 21  29
// 6 14 22  30
if(val > 9 || val < 0) return 0;    //Don't overflow the array
uint32_t numArray[10] = {2134983039, 2130706432, 1330203001, 2135509321, 2131232783,2034846031, 2034846079, 2130772225, 2135509375, 2135509327};
return numArray[val];

}
//Function for displaying an individual character. Inpix is the top-left pixel of the bitmap, RGB sets the color, small chooses between small/normal letter sizes
uint8_t letter(char ltr, int inpix, uint8_t R, uint8_t G, uint8_t B, bool small){
    uint8_t charNumber = (int) ltr;
    if(charNumber >= 48 && charNumber <= 57){
        if(small){
            displayNumber((charNumber-48),inpix+2,R,G,B,true);
            return 3;
        }
        else{
            displayNumber((charNumber-48),inpix,R,G,B,false);
        } 
        return 4;
    }
    switch (charNumber) //Use the ASCII number to choose which bitmap is used
    {
    case 32:
        encode8Cond(127,inpix,8,0,0,0,true);
        return 1;
    case 33:
        encode8Cond(47,inpix,8,R,G,B,true);
        return 1;
    case 34:
        encode32Cond(196611,inpix,24,R,G,B,true);
        return 3;
    case 39:
    case 96:
        encode8Cond(3,inpix,8,R,G,B,true);
        return 1;
    case 40:
        encode32Cond(16702,inpix,16,R,G,B,true);
        return 2;
    case 41:
        encode32Cond(15937,inpix,16,R,G,B,true);
        return 2;
    case 43:
        encode32Cond(531464,inpix,24,R,G,B,true);
        return 3;
    case 44:
        encode8Cond(96,inpix,8,R,G,B,true);
        return 1;
    case 45:
        encode32Cond(2056,inpix,16,R,G,B,true);
        return 2;
    case 46:
        encode8Cond(64,inpix,8,R,G,B,true);
        return 1;
    case 47:
        encode32Cond(399456,inpix,24,R,G,B,true);
        return 3;   
    case 58:
        encode8Cond(34,inpix,8,R,G,B,true);
        return 1;
    case 59:
        encode8Cond(98,inpix,8,R,G,B,true);
        return 1;
    case 60:
        encode32Cond(2233352,inpix,24,R,G,B,true);
        return 3;
    case 61:
        encode32Cond(1315860,inpix,24,R,G,B,true);
        return 3;
    case 62:
        encode32Cond(529442,inpix,24,R,G,B,true);
        return 3;
    case 63:
        encode32Cond(1005827,inpix,24,R,G,B,true);
        return 3;
    case 65:
        encode32Cond(2114521470,inpix,32,R,G,B,true);
        return 4;
    case 66:
        encode32Cond(910772607,inpix,32,R,G,B,true);
        return 4;
    case 67:
        encode32Cond(574701886,inpix,32,R,G,B,true);
        return 4;
    case 68:
        encode32Cond(1044463999,inpix,32,R,G,B,true);
        return 4;
    case 69:
        encode32Cond(1095321983,inpix,32,R,G,B,true);
        return 4;
    case 70:
        encode32Cond(17369471,inpix,32,R,G,B,true);
        return 4;
    case 71:
        encode32Cond(2018066815,inpix,32,R,G,B,true);
        return 4;
    case 72:
        encode32Cond(2131232895,inpix,32,R,G,B,true);
        return 4;
    case 73:
        encode32Cond(4292417,inpix,24,R,G,B,true);
        return 3;
    case 74:
        encode32Cond(20922657,inpix,32,R,G,B,true);
        return 4;
    case 75:
        encode32Cond(1997015167,inpix,32,R,G,B,true);
        return 4;
    case 76:
        encode32Cond(1077952639,inpix,32,R,G,B,true);
        return 4;
    case 77:
        encode8Cond(127,inpix+32,8,R,G,B,true);
        encode32Cond(25035135,inpix,32,R,G,B,true);
        return 5;
    case 78:
        encode8Cond(127,inpix+32,8,R,G,B,true);
        encode32Cond(1077805439,inpix,32,R,G,B,true);
        return 5;
    case 79:
        encode32Cond(1044463934,inpix,32,R,G,B,true);
        return 4;
    case 80:
        encode32Cond(101255551,inpix,32,R,G,B,true);
        return 4;
    case 81:
        encode32Cond(522273086,inpix,32,R,G,B,true);
        return 4;
    case 82:
        encode32Cond(826886527,inpix,32,R,G,B,true);
        return 4;
    case 83:
        encode32Cond(826886470,inpix,32,R,G,B,true);
        return 4;
    case 84:
        encode8Cond(1,inpix+32,8,R,G,B,true);
        encode32Cond(25100545,inpix,32,R,G,B,true);
        return 5;
    case 85:
        encode32Cond(1061175359,inpix,32,R,G,B,true);
        return 4;
    case 86:
        encode32Cond(526409759,inpix,32,R,G,B,true);
        return 4;
    case 87:
        encode8Cond(63,inpix+32,8,R,G,B,true);
        encode32Cond(1077887039,inpix,32,R,G,B,true);
        return 5;
    case 88:
        encode32Cond(7569152,inpix,24,R,G,B,true);
        return 3;
    case 89:
        encode8Cond(1,inpix+32,8,R,G,B,true);
        encode32Cond(41681409,inpix,32,R,G,B,true);
        return 5;
    case 90:
        encode32Cond(51196353,inpix,32,R,G,B,true);
        return 4;
    case 91:
        encode32Cond(16767,inpix,16,R,G,B,true);
        return 2;
    case 93:
        encode32Cond(32577,inpix,16,R,G,B,true);
        return 2;
    case 97:
        encode32Cond(7358512,inpix,24,R,G,B,true);
        return 3;
    case 98:
        encode32Cond(3164287,inpix,24,R,G,B,true);
        return 3;
    case 99:
        encode32Cond(4737072,inpix,24,R,G,B,true);
        return 3;
    case 100:
        encode32Cond(8341552,inpix,24,R,G,B,true);
        return 3;
    case 101:
        encode32Cond(5788728,inpix,24,R,G,B,true);
        return 3;
    case 102:
        encode32Cond(657790,inpix,24,R,G,B,true);
        return 3;
    case 103:
        encode32Cond(8148040,inpix,24,R,G,B,true);
        return 3;
    case 104:
        encode32Cond(7342207,inpix,24,R,G,B,true);
        return 3;
    case 105:
        encode8Cond(116,inpix,8,R,G,B,true);
        return 1;
    case 106:
        encode32Cond(3817504,inpix,24,R,G,B,true);
        return 3;
    case 107:
        encode32Cond(6819967,inpix,24,R,G,B,true);
        return 3;
    case 109:
        encode8Cond(112,inpix+32,8,R,G,B,true);
        encode32Cond(141559920,inpix,32,R,G,B,true);
        return 5;    
    case 110:
        encode32Cond(7342200,inpix,32,R,G,B,true);
        return 3;
    case 111:
        encode32Cond(3164208,inpix,24,R,G,B,true);
        return 3;
    case 112:
        encode32Cond(529532,inpix,24,R,G,B,true);
        return 3;
    case 113:
        encode32Cond(8131592,inpix,24,R,G,B,true);
        return 3;
    case 114:
        encode32Cond(1050744,inpix,24,R,G,B,true);
        return 3;
    case 115:
        encode32Cond(7623772,inpix,24,R,G,B,true);
        return 3;
    case 116:
        encode32Cond(556552,inpix,24,R,G,B,true);
        return 3;
    case 117:
        encode32Cond(7880760,inpix,24,R,G,B,true);
        return 3;
    case 118:
        encode32Cond(3686456,inpix,24,R,G,B,true);
        return 3;
    case 119:
        encode8Cond(56,inpix+32,8,R,G,B,true);
        encode32Cond(1081098296,inpix,32,R,G,B,true);
        return 5;    
    case 120:
        encode32Cond(4730952,inpix,24,R,G,B,true);
        return 3;
    case 121:
        encode32Cond(3952716,inpix,24,R,G,B,true);
        return 3;
    case 122:
        encode32Cond(5002340,inpix,24,R,G,B,true);
        return 3;
    case 108:
    case 124:
        encode8Cond(127,inpix,8,R,G,B,true);
        return 1;
    default:
        return 0;
    }
}
//Function to display an entire string on the neopixel matrix, with the first character at inpix. Uses individual letters to craft string
void strDisp(const char *wrd, int inpix, uint8_t R, uint8_t G, uint8_t B, bool small){
    int loop;
    int pix = inpix;
    for(loop=0;loop<strlen(wrd);loop++)
    {
        pix = pix+8+(8*letter(wrd[loop],pix,R,G,B,small));
    }
}
//Take an RGB value and convert it to a 9-digit number for storage in the screen array. R is the upper 3 digits, G is the middle 3, and B is the lower 3
int encodeColor(uint8_t R, uint8_t G, uint8_t B){                       //Encodes RGB 24 bit color into one integer for array storage
    return B+(1000*G)+(1000000*R);
}
//Function to draw out a 64-bit bitmap in the software screen array. RGB sets the color, inpix is the top-left pixel of the bitmap, erase erases the pixels in the region between inpix and length
void encode64Cond(uint64_t enCond, int inpix, int length, uint8_t R, uint8_t G, uint8_t B, bool erase){
    uint64_t encNum = enCond;
    if(erase){
        for(i = 0; i < length; i++){
            if(i+inpix >= 0){
                screenArray[i+inpix] = 0;
            }
        }
    }
    for(i = 0; i < length; i++){
        if((encNum & 1) == 1 && (i + inpix) >= 0){
            screenArray[i+inpix] = encodeColor(R,G,B);
        }
        encNum = encNum >> 1;
    }
}
//Function to draw out a 32-bit bitmap in the software screen array. RGB sets the color, inpix is the top-left pixel of the bitmap, erase erases the pixels in the region between inpix and length
void encode32Cond(uint32_t enCond, int inpix, int length, uint8_t R, uint8_t G, uint8_t B, bool erase){
    uint32_t encNum = enCond;
    if(erase){
        for(i = 0; i < length; i++){
            if(i+inpix >= 0){
                screenArray[i+inpix] = 0;
            }
        }
    }
    for(i = 0; i < length; i++){
        if((encNum & 1) == 1 && (i + inpix) >= 0){
            screenArray[i+inpix] = encodeColor(R,G,B);
        }
        encNum = encNum >> 1;
    }
}
//Function to draw out a 8-bit bitmap in the software screen array. RGB sets the color, inpix is the top-left pixel of the bitmap, erase erases the pixels in the region between inpix and length
void encode8Cond(uint8_t enCond, int inpix, int length, uint8_t R, uint8_t G, uint8_t B, bool erase){
    uint8_t encNum = enCond;
    if(erase){
        for(i = 0; i < length; i++){
            if(i+inpix >= 0){
                screenArray[i+inpix] = 0;
            }
        }
    }
    for(i = 0; i < length; i++){
        if((encNum & 1) == 1 && (i + inpix) >= 0){
            screenArray[i+inpix] = encodeColor(R,G,B);
        }
        encNum = encNum >> 1;
    }
}
//Function for displaying a static weather glyph based on a condition-id from openweathermap API. Condition-IDs are converted to bitmaps for display
void displayCondition(int cnum, int inpix, uint8_t R, uint8_t G, uint8_t B, bool erase){
    if(cltr[0] == 'd')
    {
        if(cnum == 800 || cnum == 801 || cnum == 721)                       //Clear or Haze
        {
            encode64Cond(2251921634885640,inpix, 64,round(R/1.8),round(G*1.3),0, erase);
        }
        if(cnum == 802 || cnum == 803)                                      //Partly cloudy
        {
            encode64Cond(17264541704,inpix, 64, R,G*1.9,0, erase);
            encode64Cond(1168745917412540416,inpix, 64,R,G,B, erase);
        }
    }
    else
    {
        if(cnum == 800 || cnum == 801 || cnum == 721)
        {
            encode64Cond(68716846972928,inpix, 64, R,G*1.2,B/3, erase);
        }
        if(cnum == 802 || cnum == 803)                                      //Partly cloudy
        {
            encode64Cond(263714,inpix, 64, R,G*1.2,B/3, erase);
            encode64Cond(17833647421456,inpix, 64,R,G,B, erase);
        }
    }
    if((cnum > 199 && cnum < 721) || (cnum > 730 && cnum < 772)){
        encode64Cond(570676717487874,inpix, 64, R,G,B, erase);
    }
}
//Function for converting a number from decimal to locations in the software array using the number bitmaps
void displayNumber(int val, int inpix, uint8_t R, uint8_t G, uint8_t B, bool small){
    uint32_t encNum;
    if(small){
        encNum = snum(val);
    }
    else{
        encNum = num(val);
    } 
    for(i = 0; i < 32; i++){
        if(i+inpix >= 0){
            screenArray[i+inpix] = 0;
        }
    }
    for(i = 0; i < 32; i++){
        if((encNum & 1) == 1 && (i + inpix) >= 0){
            screenArray[i+inpix] = B+(1000*G)+(1000000*R);
        }
        encNum = encNum >> 1;
    }
}
//Function for displaying a clock anywhere on the array based on inpix. Takes and maps time to decimal digits using bitmaps
void displayClock(int inpix, uint8_t R, uint8_t G, uint8_t B, int manctrl){
    int hr = Time.hourFormat12();
    int min = Time.minute();
    if(mprev != min || manctrl) {                                      //Check if time has changed from last pixel push
        if(EEPROM.read(6) == 0){                            //If the brightness is above the threshold, post large numbers by default
            mprev = Time.minute();                          //Save current time for next check
            screenArray[inpix+57] = (B/2)+(1000*(G/2))+(1000000*(R/2));                        //Display clock colons
            screenArray[inpix+61] = (B/2)+(1000*(G/2))+(1000000*(R/2));
            if(min/10 == 0) {                               //Check if minute number is less than 10
                displayNumber(0,72+inpix,R,G,B,false);            //Display 0 digit if less than 10 in 10's place
                displayNumber(min,112+inpix,R,G,B,false);
            }
            else {
                displayNumber((min/10),72+inpix,R,G,B,false);
                displayNumber((min%10),112+inpix,R,G,B,false);
            }
            displayNumber(1,inpix-24,R*(hr/10),G*(hr/10),B*(hr/10),false);   //Display 1 if present in the hour
            displayNumber(hr-(10*(hr/10)),inpix+16,R,G,B,false);        //Display other digit of hour
        }
        else{                                               //Display small clock if set in EEPROM
            mprev = Time.minute();                          //Save current time for next check
            screenArray[inpix+49] = (B/2)+(1000*(G/2))+(1000000*(R/2));                        //Display clock colons
            screenArray[inpix+51] = (B/2)+(1000*(G/2))+(1000000*(R/2));
            if(min/10 == 0) {                               //Check if minute number is less than 10
                displayNumber(0,64+inpix,R,G,B,true);            //Display 0 digit if less than 10 in 10's place
                displayNumber(min,96+inpix,R,G,B,true);
            }
            else {
                displayNumber((min/10),64+inpix,R,G,B,true);
                displayNumber((min%10),96+inpix,R,G,B,true);
            }
            displayNumber(1,inpix-16,R*(hr/10),G*(hr/10),B*(hr/10),true);   //Display 1 if present in the hour
            displayNumber(hr-(10*(hr/10)),inpix+16,R,G,B,true);        //Display other digit of hour
        }
    }
} 

//Function for displaying the outdoor temperature, takes the temperature variale, initial pixel, and color
void displayTemp(int temperature, int inpix, uint8_t R, uint8_t G, uint8_t B, bool small){
    int TC1 = temperature/10;
    int TC2 = temperature%10;
    if(small){
        displayNumber(TC1,inpix, R, G, B,true);
        displayNumber(TC2,inpix+32,R,G,B,true);
        screenArray[inpix+64] = encodeColor(R,G,B);
    }
    else{
        displayNumber(TC1,inpix, R, G, B,false);
        displayNumber(TC2,inpix+40,R,G,B,false);
        screenArray[inpix+80] = encodeColor(R,G,B);
    }
}
void displayHumid(int humidPercent, int inpix, uint8_t R, uint8_t G, uint8_t B){
    int HC1 = humidPercent/10;
    int HC2 = humidPercent%10;
    if(humidPercent == 100){
        for(HC1 = 0; HC1 < 7; HC1++){
            screenArray[inpix+HC1] = encodeColor(R,G,B);
        }
        displayNumber(0,inpix+16, R, G, B,false);
        displayNumber(0,inpix+56,R,G,B,false);
    }
    else{
        displayNumber(HC1,inpix, R, G, B,false);
        displayNumber(HC2,inpix+40,R,G,B,false);
    }
}
void dimg(int cnum, int inpix, uint8_t R, uint8_t G, uint8_t B){  //Code block for displaying a still weather animation when in dark mode
    if(cnum > 299 && cnum < 322)
    {
        screenArray[inpix+14] = encodeColor(R,G,B);
        screenArray[inpix+28] = encodeColor(R,G,B);
        screenArray[inpix+46] = encodeColor(R,G,B);
    }
    else if(cnum > 199 && cnum < 235)
    {
        screenArray[inpix+27] = encodeColor(R,G,B);
        screenArray[inpix+20] = encodeColor(R,G,B);
        screenArray[inpix+29] = encodeColor(R,G,B);
        screenArray[inpix+22] = encodeColor(R,G,B);
    }
    else if(cnum > 499 && cnum < 533)
    {
        screenArray[inpix+11] = encodeColor(R,G,B);
        screenArray[inpix+22] = encodeColor(R,G,B);
        screenArray[inpix+28] = encodeColor(R,G,B);
        screenArray[inpix+37] = encodeColor(R,G,B);
        screenArray[inpix+43] = encodeColor(R,G,B);
    }
    else if(cnum > 599 && cnum < 630)                                    //Snow
    {
        screenArray[inpix+11] = encodeColor(R,G,B);
        screenArray[inpix+22] = encodeColor(R,G,B);
        screenArray[inpix+28] = encodeColor(R,G,B);
        screenArray[inpix+37] = encodeColor(R,G,B);
        screenArray[inpix+43] = encodeColor(R,G,B);   
    }
}
//Function to display an animated effect for the weather image, such as rain under a cloud, snow, sleet
void animateCondition(int cnum, int inpix, int tmr, uint8_t R, uint8_t G, uint8_t B){                      //Code block for displaying one animation of a weather condition such as rain
    if(cnum > 199 && cnum < 235){
        screenArray[inpix+27] = 0;
        screenArray[inpix+20] = 0;
        screenArray[inpix+29] = 0;
        screenArray[inpix+22] = 0;
        printScreen(screenArray,inpix+20,inpix+29);
        if(connDelay(tmr/4)) return;        //Break from the function if a button is pressed
        //printScreen(screenArray,inpix+20,inpix+29);
        //if(connDelay(tmr)) return;
        screenArray[inpix+27] = encodeColor(R/1.2,G*2.4,0);
        printScreen(screenArray,inpix+27, inpix+27);
        if(connDelay(tmr/2)) return;        //Break from the function if a button is pressed
        screenArray[inpix+20] = encodeColor(R/1.2,G*2,0);
        printScreen(screenArray,inpix+20, inpix+20);
        if(connDelay(tmr/2)) return;        //Break from the function if a button is pressed
        screenArray[inpix+29] = encodeColor(R/1.2,G*2,0);
        printScreen(screenArray,inpix+29, inpix+29);
        if(connDelay(tmr/2)) return;        //Break from the function if a button is pressed
        screenArray[inpix+22] = encodeColor(R/1.2,G*2,0);
        printScreen(screenArray,inpix+22, inpix+22);
        if(connDelay(tmr)) return;          //Break from the function if a button is pressed
        screenArray[inpix+27] = 0;
        screenArray[inpix+20] = 0;
        screenArray[inpix+29] = 0;
        screenArray[inpix+22] = 0;
    }
    else if(cnum > 299 && cnum < 322){
        screenArray[inpix+14] = 0;
        screenArray[inpix+28] = 0;
        screenArray[inpix+46] = 0;
        //printScreen(screenArray,inpix+11,inpix+46);
        //if(connDelay(tmr)) return;
        screenArray[inpix+11] = encodeColor(0,G,B);
        screenArray[inpix+29] = encodeColor(0,G,B);
        screenArray[inpix+43] = encodeColor(0,G,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+11] = 0;
        screenArray[inpix+29] = 0;
        screenArray[inpix+43] = 0;
        screenArray[inpix+12] = encodeColor(0,G,B);
        screenArray[inpix+30] = encodeColor(0,G,B);
        screenArray[inpix+44] = encodeColor(0,G,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+12] = 0;
        screenArray[inpix+30] = 0;
        screenArray[inpix+44] = 0;
        screenArray[inpix+13] = encodeColor(0,G,B);
        screenArray[inpix+27] = encodeColor(0,G,B);
        screenArray[inpix+45] = encodeColor(0,G,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+13] = 0;
        screenArray[inpix+27] = 0;
        screenArray[inpix+45] = 0;
        screenArray[inpix+14] = encodeColor(0,G,B);
        screenArray[inpix+28] = encodeColor(0,G,B);
        screenArray[inpix+46] = encodeColor(0,G,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+14] = 0;
        screenArray[inpix+28] = 0;
        screenArray[inpix+46] = 0;
    }
    else if(cnum > 499 && cnum < 533){
        screenArray[inpix+11] = 0;
        screenArray[inpix+22] = 0;
        screenArray[inpix+28] = 0;
        screenArray[inpix+37] = 0;
        screenArray[inpix+43] = 0;
        //printScreen(screenArray,inpix+11,inpix+46);
        //if(connDelay(tmr)) return;
        screenArray[inpix+12] = encodeColor(0,G/3,B);
        screenArray[inpix+19] = encodeColor(0,G/3,B);
        screenArray[inpix+29] = encodeColor(0,G/3,B);
        screenArray[inpix+38] = encodeColor(0,G/3,B);
        screenArray[inpix+44] = encodeColor(0,G/3,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+12] = 0;
        screenArray[inpix+19] = 0;
        screenArray[inpix+29] = 0;
        screenArray[inpix+38] = 0;
        screenArray[inpix+44] = 0;
        screenArray[inpix+13] = encodeColor(0,G/3,B);
        screenArray[inpix+20] = encodeColor(0,G/3,B);
        screenArray[inpix+30] = encodeColor(0,G/3,B);
        screenArray[inpix+35] = encodeColor(0,G/3,B);
        screenArray[inpix+45] = encodeColor(0,G/3,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+13] = 0;
        screenArray[inpix+20] = 0;
        screenArray[inpix+30] = 0;
        screenArray[inpix+35] = 0;
        screenArray[inpix+45] = 0;
        screenArray[inpix+14] = encodeColor(0,G/3,B);
        screenArray[inpix+21] = encodeColor(0,G/3,B);
        screenArray[inpix+27] = encodeColor(0,G/3,B);
        screenArray[inpix+36] = encodeColor(0,G/3,B);
        screenArray[inpix+46] = encodeColor(0,G/3,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+14] = 0;
        screenArray[inpix+21] = 0;
        screenArray[inpix+27] = 0;
        screenArray[inpix+36] = 0;
        screenArray[inpix+46] = 0;
        screenArray[inpix+11] = encodeColor(0,G/3,B);
        screenArray[inpix+22] = encodeColor(0,G/3,B);
        screenArray[inpix+28] = encodeColor(0,G/3,B);
        screenArray[inpix+37] = encodeColor(0,G/3,B);
        screenArray[inpix+43] = encodeColor(0,G/3,B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+11] = 0;
        screenArray[inpix+22] = 0;
        screenArray[inpix+28] = 0;
        screenArray[inpix+37] = 0;
        screenArray[inpix+43] = 0;
    }
    else if(cnum > 599 && cnum < 630){
        screenArray[inpix+11] = 0;
        screenArray[inpix+22] = 0;
        screenArray[inpix+28] = 0;
        screenArray[inpix+37] = 0;
        screenArray[inpix+43] = 0;
        //printScreen(screenArray,inpix+11,inpix+46);
        //if(connDelay(tmr)) return;
        screenArray[inpix+12] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+19] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+29] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+38] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+44] = encodeColor(1.5*R,1.5*G,1.8*B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+12] = 0;
        screenArray[inpix+19] = 0;
        screenArray[inpix+29] = 0;
        screenArray[inpix+38] = 0;
        screenArray[inpix+44] = 0;
        screenArray[inpix+13] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+20] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+30] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+35] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+45] = encodeColor(1.5*R,1.5*G,1.8*B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+13] = 0;
        screenArray[inpix+20] = 0;
        screenArray[inpix+30] = 0;
        screenArray[inpix+35] = 0;
        screenArray[inpix+45] = 0;
        screenArray[inpix+14] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+21] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+27] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+36] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+46] = encodeColor(1.5*R,1.5*G,1.8*B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+14] = 0;
        screenArray[inpix+21] = 0;
        screenArray[inpix+27] = 0;
        screenArray[inpix+36] = 0;
        screenArray[inpix+46] = 0;
        screenArray[inpix+11] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+22] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+28] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+37] = encodeColor(1.5*R,1.5*G,1.8*B);
        screenArray[inpix+43] = encodeColor(1.5*R,1.5*G,1.8*B);
        printScreen(screenArray,inpix+11, inpix+46);
        if(connDelay(tmr)) return;
        screenArray[inpix+11] = 0;
        screenArray[inpix+22] = 0;
        screenArray[inpix+28] = 0;
        screenArray[inpix+37] = 0;
        screenArray[inpix+43] = 0;
    }
    else{
        if(connDelay(tmr*3)) return;
    }
}
void checkForUpdate(bool manualCtrl){
    if(lastUpdate+300000 < millis() || lastUpdate > millis() || manualCtrl){
        Particle.publish("Weather", "1", PRIVATE);                                      // Publishes to get weather data
        //Particle.publish("WeatherHL", "1", PRIVATE);
        /*if(EEPROM.read(3) == 1)
        {
            Particle.publish("co2dat", "1", PRIVATE);       //Deprecated - publishing a wekbhook to a CO2 sensing microcontroller
        }
        if(EEPROM.read(4) == 1)
        {
            Particle.publish("itempdat", "1", PRIVATE);
        }*/
        //Particle.publish("GetStock", "DIA", PRIVATE);
        lastUpdate = millis();
    }
    /*else if(lastUpdate+15000 < millis() && lastUpdate+30000 > millis()){
        Particle.publish("WeatherHL", "1", PRIVATE);
    }*/
}   
//Connected delay funciton, delays as long as a button is not pressed                       
bool connDelay(int length){
    int count;
    for(count=0; count<(length/100); count++){
        if(digitalRead(D0)==LOW && digitalRead(D1)==LOW && digitalRead(D2)==LOW){
            delay(100);
        }
        else{
            return true;
        }
    }
    return false;
}
bool isSensorDark(){
    if(analogRead(A4) < bound) {
        bound = upbound;
        scan = 1;
        rclock = 1;
        gclock = 0;
        bclock = 0;
        if(EEPROM.read(5) == 2){
            gclock = 1;
            rclock = 0;
        }
        else if(EEPROM.read(5) == 3){
            bclock = 1;
            rclock = 0;
        }
        dmode = 1;
        return true;
    }
    return false;
}
void weatherLoop(int inpix, int clockpix, uint8_t R, uint8_t G, uint8_t B){
    if(wmode == 1)                                                  //Outdoor Temperature
    {
        for(int l=0; l <= 25; l++){                                     //Fade up animation for numbers
            displayTemp(TFahr,inpix,0,(gclock*l)/25,0,false);                     //Call function used to display numbers
            printScreen(screenArray,inpix,inpix+88);
            delay(10);                                               //Adjust this delay to change animation duration
        }
        connDelay(2500);
        if(isSensorDark()){
            for(int l=25; l >= 0; l--){                                     //Fade down animation
                displayClock(clockpix, ((R*l)/25)+rclock, ((G*l)/25)+gclock, ((B*l)/25)+bclock, true);
                displayTemp(TFahr,inpix,0,(G*l)/25,0,false);                     //Call function used to display numbers
                printScreen(screenArray,clockpix,inpix+88);
                delay(10);                                              //Adjust this delay to change animation duration
            }
            return;
        }
        else{
            for(int l=25; l >= 0; l--){                                     //Fade down animation
                displayTemp(TFahr,inpix,0,(G*l)/25,0,false);                     //Call function used to display numbers
                printScreen(screenArray,inpix,inpix+88);
                delay(10);                                              //Adjust this delay to change animation duration
            }
        }
        if(EEPROM.read(4) == 1){                                    //Check settings in EEPROM
            wmode = 2;                                              //Go to indoor temperature code block if configured to do so
        }
        else{
            wmode = 3;                                              //Otherwise skip, and go to the humidity control
        }
    }
    if(wmode == 2)                                                  //Indoor Temp from Adafruit Sensor
    {
        for(int l=0; l <= 25; l++){                                     //Fade up animation for numbers
            displayTemp(itemp,inpix,(R*l)/50,0,(B*l)/25,false);                     //Call function used to display numbers
            printScreen(screenArray,inpix,inpix+88);
            delay(10);                                               //Adjust this delay to change animation duration
        }
        connDelay(2500);
        if(isSensorDark()){
            for(int l=25; l >= 0; l--){                                     //Fade down animation
                displayClock(clockpix, ((R*l)/25)+rclock, ((G*l)/25)+gclock, ((B*l)/25)+bclock, true);
                displayTemp(itemp,inpix,(R*l)/50,0,(B*l)/25,false);                     //Call function used to display numbers
                printScreen(screenArray,clockpix,inpix+88);
                delay(10);                                              //Adjust this delay to change animation duration
            }
            return;
        }
        else{
            for(int l=25; l >= 0; l--){                                     //Fade down animation
                displayTemp(itemp,inpix,(R*l)/50,0,(B*l)/25,false);                     //Call function used to display numbers
                printScreen(screenArray,inpix,inpix+88);
                delay(10);                                              //Adjust this delay to change animation duration
            }
        }
        wmode = 3;
    }
    if (wmode == 3)
    {
        for(int l=0; l <= 25; l++){
            displayHumid(humidity,inpix,0,(G*l)/25,(B*l)/25);
            printScreen(screenArray,inpix,inpix+88);
            delay(10);
        }
        connDelay(2500);
        if(isSensorDark()){
            for(int l=25; l >= 0; l--){
                displayClock(clockpix, ((R*l)/25)+rclock, ((G*l)/25)+gclock, ((B*l)/25)+bclock, true);
                displayHumid(humidity,inpix,0,(G*l)/25,(B*l)/25);
                printScreen(screenArray,clockpix,inpix+88);
                delay(10);
            }
            return;
        }
        else{
            for(int l=25; l >= 0; l--){
                displayHumid(humidity,inpix,0,(G*l)/25,(B*l)/25);
                printScreen(screenArray,inpix,inpix+88);
                delay(10);
            }
        }
        wmode = 4;
    }
    if(wmode == 4)
    {
        for(int l = 0; l <= 20; l++){
            displayCondition(cid,inpix, (R*l)/20, (G*l)/20, (B*l)/20, false);
            printScreen(screenArray,inpix,inpix+88);
            delay(10);
        }
        animateCondition(cid,inpix,250,R,G,B);
        displayClock(clockpix, R,G,B, false);
        printScreen(screenArray,clockpix,clockpix+152);
        animateCondition(cid,inpix,250,R,G,B);
        displayClock(clockpix, R,G,B, false);
        printScreen(screenArray,clockpix,clockpix+152);
        animateCondition(cid,inpix,250,R,G,B);
        displayClock(clockpix, R,G,B, false);
        printScreen(screenArray,clockpix,clockpix+152);
        if(isSensorDark()){
            for(int l = 20; l >= 0; l--){
                displayClock(clockpix, ((R*l)/25)+rclock, ((G*l)/25)+gclock, ((B*l)/25)+bclock, true);
                displayCondition(cid,inpix, ((R*l)/25)+rclock, ((G*l)/25)+gclock, ((B*l)/25)+bclock, false);
                dimg(cid,160, ((R*l)/25)+rclock, ((G*l)/25)+gclock, ((B*l)/25)+bclock);
                printScreen(screenArray,clockpix,inpix+88);
                delay(10);
            }
            return;
        }
        else{
            for(int l = 20; l >= 0; l--){
                displayCondition(cid,inpix, (R*l)/20, (G*l)/20, (B*l)/20, false);
                dimg(cid,160, (R*l)/20, (G*l)/20, (B*l)/20);
                printScreen(screenArray,inpix,inpix+88);
                delay(10);
            }
        }
        wmode = 1;
    }
}
void settings(int inpix, uint8_t R, uint8_t G, uint8_t B){                                        //Code block for a settings menu, once initiated, it waits for the user to press the up (D0) and down (D2) to exit                                                              
    #define numMenuItems 6
    bool sett = true;
    int smode = 1;
    fillStrip(inpix,inpix+255,0,0,0,true);
    strDisp("Settings",inpix,R,G,B,false);
    printScreen(screenArray,inpix,inpix+255);
    while(digitalRead(enbtn) == HIGH) delay(5);
    fillStrip(inpix,inpix+255,0,0,0,true);
    while(sett == true) 
    {
        switch (smode){
            case 1:
                strDisp("WiFi", inpix, R, G, B, false);
                break;
            case 2:
                strDisp("Display", inpix, R, G, B, false);
                break;
            case 3:
                strDisp("Dark md", inpix, R, G, B, false);
                break;
            case 4:
                strDisp("About", inpix, R, G, B, false);
                break;
            case 5:
                strDisp("Tzone", inpix, R, G, B, false);
                break;
            case 6:
                strDisp("Exit", inpix, R, G, B, false);
                if(digitalRead(enbtn) == HIGH){
                    sett = false;
                    while(digitalRead(enbtn) == HIGH) delay(5);
                    fillStrip(inpix,inpix+255,0,0,0,true);
                }
                break;
            /*case 6:
                strDisp("Get CO2", inpix, R, G, B, false);
                break;
            case 7:
                strDisp("Get tmp", inpix, R, G, B, false);
                break;
            case 8:
                strDisp("Drk clr", inpix, R, G, B, false);
                break;
            case 9:
                strDisp("Mini clk", inpix, R, G, B, false);
                break;*/                
        }
        if(digitalRead(enbtn) == HIGH){
            bool submenu = true;
            int submode = 1;
            int tzoff = EEPROM.read(1);
            while(digitalRead(enbtn) == HIGH) delay(5);
            fillStrip(inpix,inpix+255,0,0,0,true);
            while(submenu){
                switch (smode){
                    case 1:     //Wifi Sub-menu
                        switch(submode){
                            case 1:
                                strDisp("sig str", inpix, R, G, B, false);
                                if(digitalRead(enbtn) == HIGH){
                                    #if PLATFORM_ID == PLATFORM_ARGON
                                        WiFiSignal sig = WiFi.RSSI();
                                        uint8_t strength = uint8_t(sig.getStrength());
                                    #elif PLATFORM_ID == PLATFORM_BORON
                                        CellularSignal sig = Cellular.RSSI();
                                        uint8_t strength = sig.getStrength();     
                                    #endif
                                    fillStrip(inpix,inpix+255,0,0,0,true);
                                    displayNumber(strength/100,inpix,R,G,B,true);
                                    displayNumber((strength/10)%10,inpix+48,R,G,B,true);
                                    displayNumber(strength%10,inpix+96,R,G,B,true);
                                    printScreen(screenArray,inpix,inpix+255);
                                    while(digitalRead(enbtn) == HIGH) delay(5);
                                    while(digitalRead(enbtn) == LOW) delay(5);
                                    fillStrip(inpix,inpix+255,0,0,0,true);
                                    while(digitalRead(enbtn) == HIGH) delay(5);
                                }
                                break;
                            case 2:
                                strDisp("IP",inpix, R, G, B, false);
                                break;
                            case 3:
                                strDisp("Net name", inpix, R, G, B, false);
                                break;
                            case 4:
                                strDisp("Exit", inpix, R, G, B, false);
                                if(digitalRead(enbtn) == HIGH) submenu = false;
                                break;
                        }
                        if(menuButtonUpdate(&submode,4)) fillStrip(inpix,inpix+255,0,0,0,true);
                        break;
                    case 2:     //Display Sub-menu
                        switch(submode){
                            case 1:
                                strDisp("Tru-tone", inpix, R, G, B, false);
                                break;
                            case 2:
                                strDisp("Get wthr",inpix, R, G, B, false);
                                break;
                            case 3:
                                strDisp("Get temp", inpix, R, G, B, false);
                                break;
                            case 4:
                                strDisp("Exit", inpix, R, G, B, false);
                                if(digitalRead(enbtn) == HIGH) submenu = false;
                                break;
                        }
                        if(menuButtonUpdate(&submode,4)) fillStrip(inpix,inpix+255,0,0,0,true);
                        break;
                    case 3:     //Dark Sub-menu
                        switch(submode){
                            case 1:
                                strDisp("Mini clk", inpix, R, G, B, false);
                                break;
                            case 2:
                                strDisp("Dark clr",inpix, R, G, B, false);
                                break;
                            case 3:
                                strDisp("Disp temp", inpix, R, G, B, false);
                                break;
                            case 4:
                                strDisp("Exit", inpix, R, G, B, false);
                                if(digitalRead(enbtn) == HIGH) submenu = false;
                                break;
                        }
                        if(menuButtonUpdate(&submode,4)) fillStrip(inpix,inpix+255,0,0,0,true);
                        break;
                    case 4:     //Dark Sub-menu
                        switch(submode){
                            case 1:
                                strDisp("Sys-vsn", inpix, R, G, B, false);
                                break;
                            case 2:
                                strDisp("Firm-vsn",inpix, R, G, B, false);
                                break;
                            case 3:
                                strDisp("Reset", inpix, R, G, B, false);
                                break;
                            case 4:
                                strDisp("Exit", inpix, R, G, B, false);
                                if(digitalRead(enbtn) == HIGH) submenu = false;
                                break;
                        }
                        if(menuButtonUpdate(&submode,4)) fillStrip(inpix,inpix+255,0,0,0,true);
                        break;
                    case 5:
                        while(digitalRead(enbtn)) delay(5);
                        while(!digitalRead(enbtn)){
                            displayClock(0, 0, gclock, 0, true);
                            printScreen(screenArray,0,255);
                            if(menuButtonUpdate(&tzoff,23)){
                                char tnum[3] = "";
                                sprintf(tnum,"%d", tzoff);
                                strDisp(tnum,inpix,rclock,gclock,bclock,true);
                                Time.zone(12-tzoff);
                                Particle.syncTime();
                            }
                            delay(5);
                        }
                        EEPROM.write(1, tzoff);
                        while(digitalRead(enbtn)) delay(5);
                        submenu = false;
                        break;
                    case 6:
                        strDisp("Exit", inpix, R, G, B, false);
                        if(digitalRead(enbtn) == HIGH){
                            submenu = false;
                            while(digitalRead(enbtn) == HIGH) delay(5);
                        }
                        break;
                }
                printScreen(screenArray,inpix,inpix+255);
            }
            /*if(dispm == 1){
                fillStrip(inpix,inpix+255,0,0,0,true);
                strDisp(WiFi.SSID(), 0, rclock/2, gclock, bclock/2);
                printScreen(screenArray,inpix,inpix+255);
                while(digitalRead(enbtn) == HIGH) delay(50);
                while(digitalRead(enbtn) == LOW)  delay(50);
                fillStrip(inpix,inpix+255,0,0,0,true);
                while(digitalRead(enbtn) == HIGH) delay(5);
            }
            else if(dispm == 2){
                WiFiSignal sig = WiFi.RSSI();
                uint8_t strength = uint8_t(sig.getStrength());
                fillStrip(inpix,inpix+255,0,0,0,true);
                displayNumber(strength/100,inpix,R,G,B);
                displayNumber((strength/10)%10,inpix+48,R,G,B);
                displayNumber(strength%10,inpix+96,R,G,B);
                printScreen(screenArray,inpix,inpix+255);
                while(digitalRead(enbtn) == HIGH) delay(5);
                while(digitalRead(enbtn) == LOW) delay(5);
                fillStrip(inpix,inpix+255,0,0,0,true);
                while(digitalRead(enbtn) == HIGH) delay(5);
            }
            else if(dispm == 3){
                fillStrip(inpix,inpix+255,0,0,0,true);
                int tzoff = 12-EEPROM.read(1);
                if(tzoff < 0){
                    letter('-', inpix, R, G, B);
                }
                else{
                    letter('+', inpix, R, G, B);
                }
                displayNumber(abs(tzoff)/10,inpix+40,R, G, B);
                displayNumber(abs(tzoff%10),inpix+88, R, G, B);
                printScreen(screenArray,inpix,inpix+255);
                while(digitalRead(enbtn) == HIGH) delay(5);
                while(digitalRead(enbtn) == LOW)
                {
                    if(digitalRead(dnbtn) == HIGH && tzoff < 12){
                        tzoff++;
                        while(digitalRead(dnbtn) == HIGH) delay(5);
                    }
                    if(digitalRead(upbtn) == HIGH && tzoff > -12){
                        tzoff--;
                        while(digitalRead(upbtn) == HIGH) delay(5);
                    }
                    if(tzoff < 0){
                        letter('+', inpix, 0, 0, 0);
                        letter('-', inpix, R, G, B);
                    }
                    else{
                        letter('+', inpix, R, G, B);
                    }
                    displayNumber(abs(tzoff)/10,inpix+40,rclock,gclock,bclock);
                    displayNumber(abs(tzoff%10),inpix+88,rclock,gclock,bclock);
                    printScreen(screenArray,inpix,inpix+255);
                    delay(5);
                }
                fillStrip(inpix,inpix+255,0,0,0,true);
                EEPROM.write(1, 12-tzoff);
                Time.zone(12-EEPROM.read(1));
                Particle.syncTime();
                while(digitalRead(enbtn) == HIGH) delay(5);
            }
            else if(dispm == 4){
                fillStrip(inpix,inpix+255,0,0,0,true);
                strDisp(System.version(),inpix,R,G,B);
                printScreen(screenArray,inpix,inpix+255);
                while(digitalRead(enbtn) == HIGH) delay(5);
                while(digitalRead(enbtn) == LOW) delay(5);
                fillStrip(inpix,inpix+255,0,0,0,true);
                while(digitalRead(enbtn) == HIGH) delay(5);
            }
            else if(dispm == 5){
                fillStrip(inpix,inpix+255,0,0,0,true);
                strDisp("YES",inpix,0,G,0);
                printScreen(screenArray,inpix,inpix+255);
                EEPROM.write(2,1);
                while(digitalRead(enbtn) == HIGH) delay(5);
                while(digitalRead(enbtn) == LOW)
                {
                    if(digitalRead(upbtn) == HIGH){
                        fillStrip(inpix,inpix+255,0,0,0,true);
                        strDisp("YES",inpix,0,G,0);
                        printScreen(screenArray,inpix,inpix+255);
                        EEPROM.write(2,1);
                    }
                    else if(digitalRead(dnbtn) == HIGH){
                        fillStrip(inpix,inpix+255,0,0,0,true);
                        strDisp("NO",inpix,R,0,0);
                        printScreen(screenArray,inpix,inpix+255);
                        EEPROM.write(2,0);
                    }
                    delay(5);
                }
                fillStrip(inpix,inpix+255,0,0,0,true);
                while(digitalRead(enbtn) == HIGH) delay(5);
            }
            else if(dispm == 6){
                fillStrip(inpix,inpix+255,0,0,0,true);
                strDisp("YES",inpix,0,G,0);
                printScreen(screenArray,inpix,inpix+255);
                EEPROM.write(3,1);
                while(digitalRead(enbtn) == HIGH) delay(5);
                while(digitalRead(enbtn) == LOW)
                {
                    if(digitalRead(upbtn) == HIGH){
                        fillStrip(inpix,inpix+255,0,0,0,true);
                        strDisp("YES",inpix,0,G,0);
                        printScreen(screenArray,inpix,inpix+255);
                        EEPROM.write(3,1);
                    }
                    else if(digitalRead(dnbtn) == HIGH){
                        fillStrip(inpix,inpix+255,0,0,0,true);
                        strDisp("NO",inpix,R,0,0);
                        printScreen(screenArray,inpix,inpix+255);
                        EEPROM.write(3,0);
                    }
                    delay(5);
                }
                fillStrip(inpix,inpix+255,0,0,0,true);
                while(digitalRead(enbtn) == HIGH) delay(5);
            }
            else if(dispm == 7){
                strip.clear();
                strdisp("YES",0,0,gclock,0);
                EEPROM.write(4,1);
                while(digitalRead(enbtn) == HIGH)
                {
                    delay(5);
                }
                while(digitalRead(enbtn) == LOW)
                {
                    if(digitalRead(upbtn) == HIGH){
                        strip.clear();
                        strdisp("YES",0,0,gclock,0);
                        EEPROM.write(4,1);
                    }
                    else if(digitalRead(dnbtn) == HIGH){
                        strip.clear();
                        strdisp("NO",0,rclock,0,0);
                        EEPROM.write(4,0);
                    }
                    delay(5);
                }
                strip.clear();
                while(digitalRead(enbtn) == HIGH)
                {
                    delay(5);
                }
            }
            else if(dispm == 8){
                strip.clear();
                int color;
                color = 1;
                strdisp("RED",0,rclock,0,0);
                EEPROM.write(5,1);
                while(digitalRead(enbtn) == HIGH)
                {
                    delay(5);
                }
                while(digitalRead(enbtn) == LOW)
                {
                    if(digitalRead(upbtn) == HIGH && digitalRead(D2) == LOW){
                        strip.clear();
                        strdisp("RED",0,rclock,0,0);
                        EEPROM.write(5,1);
                    }
                    else if(digitalRead(dnbtn) == HIGH && digitalRead(D0) == LOW){
                        strip.clear();
                        strdisp("GREEN",0,0,gclock,0);
                        EEPROM.write(5,2);
                    }
                    else if(digitalRead(upbtn) == HIGH && digitalRead(D2) == HIGH)
                    {
                        strip.clear();
                        strdisp("BLUE",0,0,0,bclock);
                        EEPROM.write(5,3);
                    }
                    delay(5);
                }
                strip.clear();
                while(digitalRead(enbtn) == HIGH)
                {
                    delay(5);
                }
            }
            else if(dispm == 9){
                strip.clear();
                strdisp("YES",0,0,gclock,0);
                EEPROM.write(6,1);
                while(digitalRead(enbtn) == HIGH)
                {
                    delay(5);
                }
                while(digitalRead(enbtn) == LOW)
                {
                    if(digitalRead(upbtn) == HIGH){
                        strip.clear();
                        strdisp("YES",0,0,gclock,0);
                        EEPROM.write(6,1);
                    }
                    else if(digitalRead(D2) == HIGH){
                        strip.clear();
                        strdisp("NO",0,rclock,0,0);
                        EEPROM.write(6,0);
                    }
                    delay(5);
                }
                strip.clear();
                while(digitalRead(enbtn) == HIGH)
                {
                    delay(5);
                }
            }*/
        }
        if(menuButtonUpdate(&smode,numMenuItems)) fillStrip(inpix,inpix+255,0,0,0,true);
        printScreen(screenArray,inpix,inpix+255);
        delay(50);
    }
}
bool menuButtonUpdate(int *modeSelect, int maxItems){
    if(digitalRead(upbtn) == LOW && digitalRead(enbtn) == LOW && digitalRead(dnbtn) == HIGH)
    {
        if(*modeSelect < maxItems){
            *modeSelect = *modeSelect + 1;
        }
        else{
            *modeSelect = 1;
        }
        while(digitalRead(dnbtn) == HIGH && digitalRead(upbtn) == LOW) delay(5);
        return true;
    }
    if(digitalRead(upbtn) == HIGH && digitalRead(enbtn) == LOW && digitalRead(dnbtn) == LOW)
    {
        if(*modeSelect > 1){
            *modeSelect = *modeSelect - 1;
        }
        else{
            *modeSelect = maxItems;
        }
        while(digitalRead(upbtn) == HIGH && digitalRead(dnbtn) == LOW) delay(5);
        return true;
    }
    return false;
}
void displayDate(int inpix, int curDay, int curDate, uint8_t R, uint8_t G, uint8_t B){
    switch(curDay){
    case 1:
        strDisp("Sun", inpix,R,G,B,true);
        break;
    case 2:
        strDisp("Mon", inpix,R,G,B,true);
        break;
    case 3:
        strDisp("Tue", inpix,R,G,B,true);
        break;
    case 4:
        strDisp("Wed", inpix,R,G,B,true);
        break;
    case 5:
        strDisp("Thu", inpix,R,G,B,true);
        break;
    case 6:
        strDisp("Fri", inpix,R,G,B,true);
        break;
    case 7:
        strDisp("Sat", inpix,R,G,B,true);
        break;
    }
    displayNumber(curDate/10,inpix+146,R,G,B,true);
    displayNumber(curDate%10,inpix+178,R,G,B,true);
}
void dimZone(int spix, int epix, int delay){

}
void colorModeProcess(){
    if(scan == 1){
        scan=2;
        if(analogRead(A4) < bound) {
            bound = upbound;
            if(fdark == 2){
                fillStrip(0,511,0,0,0,false);
                fdark = 1;
            }
            rclock = 1;
            gclock = 0;
            bclock = 0;
            if(EEPROM.read(5) == 2){
                gclock = 1;
                rclock = 0;
            }
            else if(EEPROM.read(5) == 3){
                bclock = 1;
                rclock = 0;
            }
            dmode = 1;
        }
        else {
            if(fdark == 1){
                fillStrip(0,511,0,0,0,false);
                fdark = 2;
            }
            photo = analogRead(A4)/18;
            if(trueTone){
                double tempB;
                double tempG;
                tempB = (float)analogRead(A4)/((float)5428)+0.3;
                tempG = (float)analogRead(A4)/((float)10000)+0.65;
                rclock = analogRead(A4)/18;//*(1-(analogRead(A4)/9500));
                gclock = (analogRead(A4)/18)*tempG;
                bclock = (analogRead(A4)/18)*tempB;
            }
            else{
                rclock = analogRead(A4)/18;
                gclock = (analogRead(A4)/18)*0.9;
                bclock = (analogRead(A4)/18)*0.8;
            }
            bound = dnbound;
            //RGB.control(false);
        }
    }
}
void loop(){

//////////////////////////////////////
///////////PHOTORESISTOR//////////////
//////////////////////////////////////    
    
    colorModeProcess();

//////////////////////////////////////
//////////////MODE 1//////////////////              Clock with cycling weather conditions
//////////////////////////////////////
    
    if(dmode == 1){                                                     

        displayClock(0, rclock, gclock, bclock, true);                          //Manually Update Temperature every full cycle
        printScreen(screenArray,0,PIXEL_COUNT-1);
        checkForUpdate(false);
        scan = 1;
        

        //////////IF SENSOR IS BRIGHT//////////
        if(analogRead(A4) >= bound)             
        {
            ///////WEATHER DISPLAY MODES//////////
            if(EEPROM.read(2) == 1)                                             //Check if setting is enabled for displaying weather elements
            {
                weatherLoop(160,0,rclock,gclock,bclock);
            }
            if(digitalRead(upbtn) == LOW && digitalRead(enbtn) == HIGH && digitalRead(dnbtn) == LOW){
                settings(256,rclock,gclock,bclock);
            }
        }

        ///////IF SENSOR IS DARK//////////
        else
        {
            //if(EEPROM.read(2) == 1){
                displayCondition(cid,160, rclock, gclock, bclock, true);
                dimg(cid,160, rclock, gclock, bclock);
                displayTemp(TFahr,257/*169+256*/,rclock, gclock, bclock,true);
                //digitalWrite(D7,HIGH);
                printScreen(screenArray,160,255);
            //}
            delay(1000);
            //digitalWrite(D7,LOW);
        }
    }
    delay(100);
}
void fillStrip(int start, int end, uint8_t R, uint8_t G, uint8_t B, bool apply){
    for(i = start; i <= end; i++){
        screenArray[i] = encodeColor(R,G,B);
    }
    if(apply){
        printScreen(screenArray,start,end);
    }
}
void weatherHandler(const char *event, const char *data) {
  // Handle the integration response
    int j;
    char tempdata[6];
    char humdata[3];
    char cdata[3];
    float tempK;
    float tempF;
    float tempC;
    //screenArray[508] = 100;
    for(j=0; j<strlen(data); j++)
    {
        if(data[j] == 't' && data[j+1] == 'e' && data[j+2] == 'm' && data[j+3] == 'p' && data[j+4] == '"'){
            tempdata[0] = data[j+6];
            tempdata[1] = data[j+7];
            tempdata[2] = data[j+8];
            tempdata[3] = data[j+9];
            tempdata[4] = data[j+10];
            tempdata[5] = data[j+11];
        }
        if(data[j] == 'h' && data[j+1] == 'u' && data[j+2] == 'm' && data[j+3] == 'i'){
            humdata[0] = data[j+10];
            humdata[1] = data[j+11];
            if(data[j+12] == '0')
            {
                humdata[2] = data[j+12];
            }
        }
        if(data[j] == 'i' && data[j+1] == 'c' && data[j+2] == 'o' && data[j+3] == 'n'){
            cltr[0] = data[j+9];
        }
        if(data[j-1] == '[' && data[j] == '{' && data[j+1] == '"' && data[j+2] == 'i' && data[j+3] == 'd'){
            cdata[0] = data[j+6];
            cdata[1] = data[j+7];
            cdata[2] = data[j+8];
        }
    }
    if(humdata[0] >= '0' && humdata[0] <= '9'){
        humidity = atof(humdata);
    }
    if(cdata[0] >= '0' && cdata[0] <= '9'){
        cid = atof(cdata);
    }
    if(tempdata[0] >= '0' && tempdata[0] <= '9'){
        tempK = atof(tempdata);
        tempC = tempK-273.15;
        tempF = (tempC*1.8)+32;
        TFahr = (int)tempF;
    }
}
void weatherHandlerHL(const char *event, const char *HLdata) {
  // Handle the integration response
    int xyz;
    char highdata[5];
    char lowdata[5];
    //screenArray[509] = 100;
    for(xyz=0; xyz<strlen(HLdata); xyz++)
    {
        if(HLdata[xyz] == 'm' && HLdata[xyz+1] == 'i' && HLdata[xyz+2] == 'n'){
            lowdata[0] = HLdata[xyz+5];
            lowdata[1] = HLdata[xyz+6];
            lowdata[2] = HLdata[xyz+7];
            lowdata[3] = HLdata[xyz+8];
            lowdata[4] = HLdata[xyz+9];
            //screenArray[510] = 100;
        }
        if(HLdata[xyz] == 'm' && HLdata[xyz+1] == 'a' && HLdata[xyz+2] == 'x'){
            highdata[0] = HLdata[xyz+5];
            highdata[1] = HLdata[xyz+6];
            highdata[2] = HLdata[xyz+7];
            highdata[3] = HLdata[xyz+8];
            highdata[4] = HLdata[xyz+9];
            //screenArray[511] = 100;
            xyz = strlen(HLdata);
        }
    }
    //printScreen(screenArray,509,511);*/
}
void myHandler2(const char *event, const char *data) {
    if(strcmp(data,"mode0")==0)
    {
        if(analogRead(A4) > bound)
        {
            strip.clear();
            scan = 2;
            dmode = 1;
        }
    }
    if(strcmp(data,"mode1")==0)
    {
        if(analogRead(A4) > bound)
        {
            dmode = 2;
        }
    }
}
void co2Handler(const char *event, const char *data) {
    for(j=0;j<strlen(data);j++){
        float codata;
        char codat[4];
        if(data[j] == 'l' && data[j+1] == 'u' && data[j+2] == 'e' && data[j+3] == '"' && data[j+4] == ':' && data[j+5] == '"'){
            codat[0] = data[j+6];
            codat[1] = data[j+7];
            codat[2] = data[j+8];
            codat[3] = data[j+9];
        }
        codata = atof(codat);
        ico2 = (int)codata;
        if(analogRead(A4) < bound && EEPROM.read(3) == 1)// && dco2 == 1)
        {
            for(i=248;i<256;i++){
                strip.setPixelColor(i,0,0,0);
            }
            for(i=248;i<(ico2/750)+248;i++){
                if(i>248 && i<256){
                    strip.setPixelColor(i,rclock,gclock,bclock);
                }
                else if(i==248)
                {
                    if(TFahr < 33){
                        strip.setPixelColor(i,bclock,gclock,rclock);
                    }
                    else{
                        strip.setPixelColor(i,rclock,gclock,bclock);
                    }
                }
            }
            strip.show();
        }
    }
}
void itHandler(const char *event, const char *data) {
    for(j=0;j<strlen(data);j++){
        float tempF;
        float tempC;
        char tempdat[2];
        if(data[j] == 'l' && data[j+1] == 'u' && data[j+2] == 'e' && data[j+3] == '"' && data[j+4] == ':' && data[j+5] == '"'){
            tempdat[0] = data[j+6];
            tempdat[1] = data[j+7];
            //tempdat[2] = data[j+8];
            //tempdat[3] = data[j+9];
        }
        tempC = atof(tempdat)-2;                //Temperature offset for accuracy of thermometer
        tempF = (tempC*1.8)+32;
        itemp = (int)tempF;
    }
}
void stockHandler(const char *event, const char *data){
    for(j=0;j<strlen(data);j++){
        float stockVal;
        char tempdat[7];
        if(data[j] == '"' && data[j+1] == 'c' && data[j+2] == '"' && data[j+3] == ':'){
            for(int loop=0; loop < 7; loop++){
                if(data[j+4+loop] == ','){
                    loop = 7;
                }
                else{
                    tempdat[loop] = data[j+4+loop];
                }
            }
        }
        strcpy(dowVal,tempdat);
    }
}

 






