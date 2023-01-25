/*
// - Lien vidéo: https://youtu.be/FNpEdO0jpaU
//   REQUIRES the following Arduino libraries:
// - SmartMatrix Library: https://github.com/pixelmatix/SmartMatrix
// - AnimatedGIF Library:  https://github.com/bitbank2/AnimatedGIF
*/

#include <MatrixHardware_ESP32_V0.h>                // This file contains multiple ESP32 hardware configurations, edit the file to define GPIOPINOUT (or add #define GPIOPINOUT with a hardcoded number before this #include)
#include <SmartMatrix.h>
#include <SD.h>
#include <AnimatedGIF.h>

#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 64;      // Set to the height of your display
const uint8_t kRefreshDepth = 36;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_64ROW_MOD32SCAN;   // Choose the configuration that matches your panels.  See more details in MatrixCommonHub75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);


#define UpHeader 0x9C
#define endHeader 0x36

Stream* mySeriel;
AnimatedGIF gif;
File f;
long lastData;
const uint16_t NUM_LEDS = kMatrixWidth * kMatrixHeight + 1;
rgb24 buff[NUM_LEDS];

void updateScreenCallback(void);

void setDriver(Stream* s) {
  mySeriel = s;
}

uint16_t XY(uint8_t x, uint8_t y) {
  return (y * kMatrixWidth) + x;
}

void updateScreenCallback(void) {
  mySeriel->write(UpHeader);
  mySeriel->write((uint8_t *)buff, NUM_LEDS*3);
  mySeriel->write(endHeader);
  yield();
  backgroundLayer.swapBuffers();
}

void DrawPixelRow(int startX, int y, int numPixels, rgb24 * color) {
  for(int i=startX; i<(startX + numPixels); i++){
    if(i < kMatrixWidth){
      buff[XY(i, y)] = color[i-startX];
    } else {
      backgroundLayer.drawPixel(i - kMatrixWidth, y, color[i-startX]);
    }
  }
}

// Draw a line of image directly on the LED Matrix
void GIFDraw(GIFDRAW *pDraw){
  uint8_t *s;
  //uint16_t *d, *usPalette, usTemp[320];
  int x, y, iWidth;
  rgb24 *d, *usPalette, usTemp[128];

  iWidth = pDraw->iWidth;
  if (iWidth > (kMatrixWidth * 2))
    iWidth = (kMatrixWidth * 2);
  usPalette = (rgb24*)pDraw->pPalette;

  y = pDraw->iY + pDraw->y; // current line
  
  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) // restore to background color
  {
    for (x=0; x<iWidth; x++)
    {
      if (s[x] == pDraw->ucTransparent)
        s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) // if transparency used
  {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + iWidth;
    x = 0;
    iCount = 0; // count non-transparent pixels
    while(x < iWidth)
    {
      c = ucTransparent-1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd)
      {
        c = *s++;
        if (c == ucTransparent) // done, stop
        {
          s--; // back up to treat it like transparent
        }
        else // opaque
        {
          *d++ = usPalette[c];
          iCount++;
        }
      } // while looking for opaque pixels
      if (iCount) // any opaque pixels?
      {
        DrawPixelRow(pDraw->iX+x, y, iCount, (rgb24 *)usTemp);
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd)
      {
        c = *s++;
        if (c == ucTransparent)
          iCount++;
        else
          s--; 
      }
      if (iCount)
      {
        x += iCount; // skip these
        iCount = 0;
      }
    }
  }
  else
  {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
    for (x=0; x<iWidth; x++)
      usTemp[x] = usPalette[*s++];
      DrawPixelRow(pDraw->iX, y, iWidth, (rgb24 *)usTemp);
  }
} /* GIFDraw() */


void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  //Serial.print("Playing gif: ");
  //Serial.println(fname);
  f = SD.open(fname);
  if (f)
  {
    *pSize = f.size();
    return (void *)&f;
  }
  return NULL;
} /* GIFOpenFile() */

void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
} /* GIFCloseFile() */

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
} /* GIFReadFile() */

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{ 
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
//  Serial.printf("Seek time = %d us\n", i);
  return pFile->iPos;
} /* GIFSeekFile() */

unsigned long start_tick = 0;

void ShowGIF(char *name)
{
  start_tick = millis();
   
  if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
  {
    while (gif.playFrame(true, NULL))
    {
      updateScreenCallback();
      if ( (millis() - start_tick) > 60000) 
        break;
    }
    gif.close();
  }

} /* ShowGIF() */



/************************* Arduino Sketch Setup and Loop() *******************************/
void setup() {
  Serial.begin(1350000);
  setDriver(&Serial);
  delay(5000);
  matrix.addLayer(&backgroundLayer); 
  matrix.begin();
  backgroundLayer.setBrightness(255);
  SD.begin(3);
  gif.begin(BIG_ENDIAN_PIXELS, GIF_PALETTE_RGB888);
}

String gifDir = "/gifs"; // play all GIFs in this directory on the SD card
char filePath[256] = { 0 };
File root, gifFile;

void loop() 
{  
      root = SD.open(gifDir);
      if (root) {
        gifFile = root.openNextFile();
        while (gifFile) {
          memset(filePath, 0x0, sizeof(filePath));                
          strcpy(filePath, gifFile.name());
          ShowGIF(filePath);
          gifFile.close();
          gifFile = root.openNextFile();
          }
         lastData = millis();
         root.close();
      } // root
  else if (millis() - lastData > 3000) {
      backgroundLayer.fillScreen({ 0, 0, 0 });
      backgroundLayer.setFont(font3x5);
      backgroundLayer.drawString(3, 24, { 255, 0, 255 }, "Waiting");
      backgroundLayer.swapBuffers();
      lastData = millis();
   }
      
      delay(4000); // pause before restarting
      
}
