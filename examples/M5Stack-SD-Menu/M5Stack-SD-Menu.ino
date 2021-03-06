/*
 * 
 * M5Stack SD Menu
 * Project Page: https://github.com/tobozo/M5Stack-SD-Updater
 * 
 * Copyright 2018 tobozo http://github.com/tobozo
 *
 * Permission is hereby granted, free of charge, to any person 
 * obtaining a copy of this software and associated documentation 
 * files ("M5Stack SD Updater"), to deal in the Software without 
 * restriction, including without limitation the rights to use, 
 * copy, modify, merge, publish, distribute, sublicense, and/or 
 * sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following 
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR 
 * OTHER DEALINGS IN THE SOFTWARE.
 * 
 * 
 * This sketch is the menu application. It must be compiled once 
 * (sketch / export compiled binary) and saved on the SD Card as 
 * "menu.bin" for persistence, and initially flashed on the M5Stack.
 * 
 * As SD Card mounting can be a hassle, using the ESP32 Sketch data 
 * uploader is also possible. Any file sent using this method will
 * automatically be copied onto the SD Card on next restart.
 * This includes .bin, .json, .jpg and .mod files.
 * 
 * The menu will list all available apps on the sdcard and load them 
 * on demand. 
 * 
 * Once you're finished with the loaded app, push reset+BTN_A and it 
 * loads the menu again. Rinse and repeat.
 *  
 * Obviously none of those apps will embed this menu, instead they
 * must include and implement the M5Stack SD Loader Snippet, a lighter
 * version of the loader dedicated to loading the menu.
 * 
 * Usage: Push BTN_A on boot calls the menu (in app) or powers off the 
 * M5Stack (in menu)
 * 
 * Accepted file types on the SD:
 *   - [sketch name].bin the Arduino binary
 *   - [sketch name].jpg an image (max 200x100 but smaller is better)
 *   - [sketch name].json file with dimensions descriptions: {"width":xxx,"height":xxx,"authorName":"tobozo", "projectURL":"http://blah"} 
 *   
 * The file names must be the same (case matters!) and left int heir relevant folders.
 * For this you will need to create two folders on the root of the SD:
 *   /jpg
 *   /json
 * jpg and json are optional but must both be set if provided.
 * 
 * 
 * 
 */

#include "SPIFFS.h"
#include <M5Stack.h>         // https://github.com/m5stack/M5Stack/
#ifdef M5_LIB_VERSION
  #include "utility/qrcode.h" // if M5Stack version >= 0.1.8 : qrCode from M5Stack
#else 
  #include "qrcode.h" // if M5Stack version <= 0.1.6 : qrCode from https://github.com/ricmoo/qrcode
#endif
#include <M5StackUpdater.h>  // https://github.com/tobozo/M5Stack-SD-Updater
#define M5SAM_LIST_MAX_COUNT 255
// if "M5SAM_LIST_MAX_COUNT" gives a warning at compilation, apply this PR https://github.com/tomsuch/M5StackSAM/pull/4
// or modify M5StackSAM.h manually
#include <M5StackSAM.h>      // https://github.com/tomsuch/M5StackSAM
#include <ArduinoJson.h>     // https://github.com/bblanchon/ArduinoJson/
#include "i18n.h"            // language file
#include "assets.h"          // some artwork for the UI
#include "controls.h"        // keypad / joypad / keyboard controls

#define tft M5.Lcd // syntax sugar, forward compat with other displays (i.e GO.Lcd)

/* 
 * /!\ Files with those extensions will be transferred to the SD Card
 * if found on SPIFFS.
 * Directory is automatically created.
 * TODO: make this optional
 */
bool migrateSPIFFS = true;
const uint8_t extensionsCount = 4; // change this if you add / remove an extension
String allowedExtensions[extensionsCount] = {
    // do NOT remove jpg and json or the menu will crash !!!
    "jpg", "json", "mod", "mp3"
};
String appDataFolder = "/data"; // if an app needs spiffs data, it's stored here

/* Storing json meta file information r */
struct JSONMeta {
  int width; // app image width
  int height; // app image height
  String authorName = "";
  String projectURL = "";
  String credits = ""; // scroll this ?
  // TODO: add more interesting properties
};

/* filenames cache structure */
struct FileInfo {
  String fileName;  // the binary name
  String metaName;  // a json file with all meta info on the binary
  String iconName;  // a jpeg image representing the binary
  String faceName;  // a jpeg image representing the author
  uint32_t fileSize;
  bool hasIcon = false;
  bool hasMeta = false;
  bool hasFace = false; // github avatar
  bool hasData = false; // app requires a spiffs /data folder
  JSONMeta jsonMeta;
};

SDUpdater sdUpdater;
FileInfo fileInfo[M5SAM_LIST_MAX_COUNT];
M5SAM M5Menu;

uint16_t appsCount = 0; // how many binary files
bool inInfoMenu = false; // menu state machine
unsigned long lastcheck = millis(); // timer check
unsigned long lastpush = millis(); // keypad/keyboard activity
uint16_t checkdelay = 300; // timer frequency
uint16_t MenuID; // pointer to the current menu item selected
int16_t scrollPointer = 0; // pointer to the scrollText position
unsigned long lastScrollRender = micros(); // timer for scrolling
String lastScrollMessage; // last scrolling string state
int16_t lastScrollOffset; // last scrolling string position

/* vMicro compliance, see https://github.com/tobozo/M5Stack-SD-Updater/issues/5#issuecomment-386749435 */
void getMeta( String metaFileName, JSONMeta &jsonMeta );
void renderIcon( FileInfo &fileInfo );
void renderMeta( JSONMeta &jsonMeta );



void getMeta( String metaFileName, JSONMeta &jsonMeta ) {
  File file = SD.open( metaFileName );
#if ARDUINOJSON_VERSION_MAJOR==6
  StaticJsonDocument<512> jsonBuffer;
  DeserializationError error = deserializeJson( jsonBuffer, file );
  if (error) return;
  JsonObject root = jsonBuffer.as<JsonObject>();
  if ( !root.isNull() )
#else
  StaticJsonBuffer<512> jsonBuffer;
  JsonObject &root = jsonBuffer.parseObject(file);
  if ( root.success() )
#endif
  {
    jsonMeta.width  = root["width"];
    jsonMeta.height = root["height"];
    jsonMeta.authorName = root["authorName"].as<String>();
    jsonMeta.projectURL = root["projectURL"].as<String>();
    jsonMeta.credits    = root["credits"].as<String>();
  }
}


void renderScroll( String scrollText, uint8_t x, uint8_t y, uint16_t width ) {
  if( scrollText=="" ) return;
  tft.setTextSize( 2 ); // setup text size before it's measured  
  if( !scrollText.endsWith( " " )) {
    scrollText += " "; // append a space since scrolling text *will* repeat
  }
  while( tft.textWidth( scrollText ) < width ) {
    scrollText += scrollText; // grow text to desired width
  }

  String  scrollMe = "";
  int16_t textWidth = tft.textWidth( scrollText );
  int16_t vsize = 0,
          vpos = 0,
          voffset = 0,
          scrollOffset = 0;
  uint8_t csize = 0, 
          lastcsize = 0;
  
  scrollPointer-=1;
  if( scrollPointer<-textWidth ) {
    scrollPointer = 0;
    vsize = scrollPointer;
  }
  
  while( tft.textWidth(scrollMe) < width ) {
    for( uint8_t i=0; i<scrollText.length(); i++ ) {
      char thisChar[2];
      thisChar[0] = scrollText[i];
      thisChar[1] = '\0';
      csize = tft.textWidth( thisChar );
      vsize+=csize;
      vpos = vsize+scrollPointer;
      if( vpos>x && vpos<=x+width ) {
        scrollMe += scrollText[i];
        lastcsize = csize;
        voffset = scrollPointer%lastcsize;
        scrollOffset = x+voffset;
        if( tft.textWidth(scrollMe) > width-voffset ) {
          break; // break out of the loop and out of the while
        }
      }
    }
  }

  // display trim
  while( tft.textWidth( scrollMe ) > width-voffset ) {
    scrollMe.remove( scrollMe.length()-1 );
  }

  // only draw if things changed
  if( scrollOffset!=lastScrollOffset || scrollMe!=lastScrollMessage ) {
    tft.setTextColor( WHITE, BLACK ); // setting background color removes the flickering effect
    tft.setCursor( scrollOffset, y );
    tft.print( scrollMe );
    tft.setTextColor( WHITE );
  }

  tft.setTextSize( 1 );
  lastScrollMessage = scrollMe;
  lastScrollOffset  = scrollOffset;
  lastScrollRender  = micros();
  lastpush          = millis();
}


/* by file info */
void renderIcon( FileInfo &fileInfo ) {
  if( !fileInfo.hasMeta || !fileInfo.hasIcon ) {
    return;
  }
  JSONMeta jsonMeta = fileInfo.jsonMeta;
  tft.drawJpgFile( SD, fileInfo.iconName.c_str(), tft.width()-jsonMeta.width-10, (tft.height()/2)-(jsonMeta.height/2)+10, jsonMeta.width, jsonMeta.height, 0, 0, JPEG_DIV_NONE );
}

/* by menu ID */
void renderIcon( uint16_t MenuID ) {
  renderIcon( fileInfo[MenuID] );
}

void renderFace( String face ) {
  tft.drawJpgFile( SD, face.c_str(), 5, 85, 120, 120, 0, 0, JPEG_DIV_NONE );
}


void renderMeta( JSONMeta &jsonMeta ) {
  tft.setTextSize( 1 );
  tft.setTextColor( WHITE );
  tft.setCursor( 10, 35 );
  tft.print( fileInfo[MenuID].fileName );
  tft.setCursor( 10, 70 );
  tft.print( String( fileInfo[MenuID].fileSize ) + String( FILESIZE_UNITS ) );
  tft.setCursor( 10, 50 );
  
  if( jsonMeta.authorName!="" && jsonMeta.projectURL!="" ) { // both values provided
    tft.print( AUTHOR_PREFIX );
    tft.print( jsonMeta.authorName );
    tft.print( AUTHOR_SUFFIX );
    qrRender( jsonMeta.projectURL, 160 );
  } else if( jsonMeta.projectURL!="" ) { // only projectURL
    tft.print( jsonMeta.projectURL );
    qrRender( jsonMeta.projectURL, 160 );
  } else { // only authorName
    tft.drawCentreString( jsonMeta.authorName,tft.width()/2,(tft.height()/2)-25,2 );
  }
}



/* give up on redundancy and ECC to produce less and bigger squares */
uint8_t getLowestQRVersionFromString( String text, uint8_t ecc ) {
  if(ecc>3) return 4; // fail fast
  uint16_t len = text.length();
  uint8_t QRMaxLenByECCLevel[4][3] = {
    // https://www.qrcode.com/en/about/version.html  
    { 41, 77, 127 }, // L
    { 34, 63, 101 }, // M
    { 27, 48, 77 },  // Q
    { 17, 34, 58 }   // H
  };
  for( uint8_t i=0; i<3; i++ ) {
    if( len <= QRMaxLenByECCLevel[ecc][i] ) {
      return i+1;
    }
  }
  // there's no point in doing higher with M5Stack's display
  return 4;
}


void qrRender( String text, float sizeinpixels ) {
  // see https://github.com/Kongduino/M5_QR_Code/blob/master/M5_QRCode_Test.ino
  // Create the QR code
  QRCode qrcode;

  uint8_t ecc = 0; // QR on TFT can do without ECC
  uint8_t version = getLowestQRVersionFromString( text, ecc );
  uint8_t qrcodeData[qrcode_getBufferSize( version )];
  qrcode_initText( &qrcode, qrcodeData, version, ecc, text.c_str() );

  uint8_t thickness = sizeinpixels / qrcode.size;
  uint16_t lineLength = qrcode.size * thickness;
  uint8_t xOffset = ( ( tft.width() - ( lineLength ) ) / 2 ) + 70;
  uint8_t yOffset =  ( tft.height() - ( lineLength ) ) / 2;

  tft.fillRect( xOffset-5, yOffset-5, lineLength+10, lineLength+10, WHITE );
  
  for ( uint8_t y = 0; y < qrcode.size; y++ ) {
    // Each horizontal module
    for ( uint8_t x = 0; x < qrcode.size; x++ ) {
      bool q = qrcode_getModule( &qrcode, x, y );
      if (q) {
        tft.fillRect( x * thickness + xOffset, y * thickness + yOffset, thickness, thickness, TFT_BLACK );
      }
    }
  }
}


void getFileInfo( File &file ) {
  String fileName   = file.name();
  uint32_t fileSize = file.size();
  Serial.println( String( DEBUG_FILELABEL ) + fileName );
  
  fileInfo[appsCount].fileName = fileName;
  fileInfo[appsCount].fileSize = fileSize;

  String currentIconFile = "/jpg" + fileName;
  currentIconFile.replace( ".bin", ".jpg" );
  if( SD.exists( currentIconFile.c_str() ) ) {
    fileInfo[appsCount].hasIcon = true;
    fileInfo[appsCount].iconName = currentIconFile;
  }
  currentIconFile.replace( ".jpg", "_gh.jpg" );
  if( SD.exists( currentIconFile.c_str() ) ) {
    fileInfo[appsCount].hasFace = true;
    fileInfo[appsCount].faceName = currentIconFile;
  }
  String currentDataFolder = appDataFolder + fileName;
  currentDataFolder.replace( ".bin", "" );
  if( SD.exists( currentDataFolder.c_str() ) ) {
    fileInfo[appsCount].hasData = true; // TODO: actually use this feature
  }
  
  String currentMetaFile = "/json" + fileName;
  currentMetaFile.replace( ".bin", ".json" );
  if( SD.exists(currentMetaFile.c_str() ) ) {
    fileInfo[appsCount].hasMeta = true;
    fileInfo[appsCount].metaName = currentMetaFile;
  }

  
  if( fileInfo[appsCount].hasMeta == true ) {
    getMeta( fileInfo[appsCount].metaName, fileInfo[appsCount].jsonMeta );
  }
}


void listDir( fs::FS &fs, const char * dirName, uint8_t levels ){
  Serial.printf( String( DEBUG_DIRNAME ).c_str(), dirName );

  File root = fs.open( dirName );
  if( !root ){
    Serial.println( DEBUG_DIROPEN_FAILED );
    return;
  }
  if( !root.isDirectory() ){
    Serial.println( DEBUG_NOTADIR );
    return;
  }

  File file = root.openNextFile();
  while( file ){
    if( file.isDirectory() ){
      Serial.print( DEBUG_DIRLABEL );
      Serial.println( file.name() );
      if( levels ){
        listDir( fs, file.name(), levels -1 );
      }
    } else {
      if(   String( file.name() )!=MENU_BIN // ignore menu
         && String( file.name() ).endsWith( ".bin" ) // ignore files not ending in ".bin"
         && !String( file.name() ).startsWith( "/." ) ) { // ignore dotfiles (thanks to https://twitter.com/micutil)
        getFileInfo( file );
        appsCount++;
      } else {
        // ignored files
        Serial.println( String( DEBUG_IGNORED ) + file.name() );
      }
    }
    file = root.openNextFile();
  }
  file = fs.open( MENU_BIN );
  getFileInfo( file );
  appsCount++;
}


/* bubble sort filenames */
void aSortFiles() {
  bool swapped;
  FileInfo temp;
  String name1, name2;
  do {
    swapped = false;
    for( uint16_t i=0; i<appsCount-1; i++ ) {
      name1 = fileInfo[i].fileName[0];
      name2 = fileInfo[i+1].fileName[0];
      if( name1==name2 ) {
        name1 = fileInfo[i].fileName[1];
        name2 = fileInfo[i+1].fileName[1];
        if( name1==name2 ) {
          name1 = fileInfo[i].fileName[2];
          name2 = fileInfo[i+1].fileName[2];        
        } else {
          // give it up :-)
        }
      }

      if ( name1 > name2 || name1==MENU_BIN ) {
        temp = fileInfo[i];
        fileInfo[i] = fileInfo[i + 1];
        fileInfo[i + 1] = temp;
        swapped = true;
      }
    }
  } while ( swapped );
}


void buildM5Menu() {
  M5Menu.clearList();
  M5Menu.setListCaption( MENU_SUBTITLE );
  for( uint16_t i=0; i < appsCount; i++ ) {
    String shortName = fileInfo[i].fileName.substring(1);
    shortName.replace( ".bin", "" );
    
    if( shortName=="menu" ) {
      shortName = ABOUT_THIS_MENU;
    }
    
    M5Menu.addList( shortName );
  }
}


void menuUp() {
  MenuID = M5Menu.getListID();
  if( MenuID > 0 ) {
    MenuID--;
  } else {
    MenuID = appsCount-1;
  }
  M5Menu.setListID( MenuID );
  M5Menu.drawAppMenu( MENU_TITLE, MENU_BTN_INFO, MENU_BTN_LOAD, MENU_BTN_NEXT );
  M5Menu.showList();
  renderIcon( MenuID );
  inInfoMenu = false;
  lastpush = millis();
}


void menuDown() {
  M5Menu.drawAppMenu( MENU_TITLE, MENU_BTN_INFO, MENU_BTN_LOAD, MENU_BTN_NEXT );
  M5Menu.nextList();
  MenuID = M5Menu.getListID();
  renderIcon( MenuID );
  inInfoMenu = false;
  lastpush = millis();
}


void menuInfo() {
  inInfoMenu = true;
  M5Menu.windowClr();
  renderMeta( fileInfo[MenuID].jsonMeta );
  if( fileInfo[MenuID].hasFace ) {
    renderFace( fileInfo[MenuID].faceName );
  }
  lastpush = millis();
}


void menuMeta() {
  inInfoMenu = false;
  M5Menu.drawAppMenu( MENU_TITLE, MENU_BTN_INFO, MENU_BTN_LOAD, MENU_BTN_NEXT );
  M5Menu.showList();
  MenuID = M5Menu.getListID();
  renderIcon( MenuID );
  lastpush = millis();
}


/* 
 *  Scan SPIFFS for binaries and move them onto the SD Card
 *  TODO: create an app manager for the SD Card
 */
void scanDataFolder() {
  Serial.println( DEBUG_SPIFFS_SCAN );
  /* check if mandatory folders exists and create if necessary */

  // data folder
  if( !SD.exists( appDataFolder ) ) {
    SD.mkdir( appDataFolder );
  }


  for( uint8_t i=0; i<extensionsCount; i++ ) {
    String dir = "/" + allowedExtensions[i];
    if( !SD.exists( dir ) ) {
      SD.mkdir( dir );
    }
  }
  
  if( !SPIFFS.begin() ){
    Serial.println( DEBUG_SPIFFS_MOUNTFAILED );
  } else {
    File root = SPIFFS.open( "/" );
    if( !root ){
      Serial.println( DEBUG_DIROPEN_FAILED );
    } else {
      if( !root.isDirectory() ){
        Serial.println( DEBUG_NOTADIR );
      } else {
        File file = root.openNextFile();
        Serial.println( file.name() );
        String fileName = file.name();
        String destName = "";
        if( fileName.endsWith( ".bin" ) ) {
          destName = fileName;
        }
        // move allowed file types to their own folders
        for( uint8_t i=0; i<extensionsCount; i++)  {
          String ext = "." + allowedExtensions[i];
          if( fileName.endsWith( ext ) ) {  
            destName = "/" + allowedExtensions[i] + fileName;
          }
        }

        if( destName!="" ) {
          sdUpdater.displayUpdateUI( String( MOVINGFILE_MESSAGE ) + fileName );
          size_t fileSize = file.size();
          File destFile = SD.open( destName, FILE_WRITE );
          
          if( !destFile ){
            Serial.println( DEBUG_SPIFFS_WRITEFAILED) ;
          } else {
            static uint8_t buf[512];
            size_t packets = 0;
            Serial.println( String( DEBUG_FILECOPY ) + fileName );
            
            while( file.read( buf, 512) ) {
              destFile.write( buf, 512 );
              packets++;
              sdUpdater.SDMenuProgress( (packets*512)-511, fileSize );
            }
            destFile.close();
            Serial.println();
            Serial.println( DEBUG_FILECOPY_DONE );
            SPIFFS.remove( fileName );
            Serial.println( DEBUG_WILL_RESTART );
            delay( 500 );
            ESP.restart();
          }
        } else {
          Serial.println( DEBUG_NOTHING_TODO );
        }
      }
    }
  }
}


#define SPI_FLASH_SEC_STEP8 SPI_FLASH_SEC_SIZE / 4

static esp_image_metadata_t getSketchMeta( const esp_partition_t* running ) {
  esp_image_metadata_t data;
  if ( !running ) return data;
  const esp_partition_pos_t running_pos  = {
    .offset = running->address,
    .size = running->size,
  };
  data.start_addr = running_pos.offset;
  esp_image_verify( ESP_IMAGE_VERIFY, &running_pos, &data );
  return data;
}



void dumpSketchToSD( const char* fileName ) {
  const esp_partition_t* source_partition = esp_ota_get_running_partition();
  const char* label = "Current running partition";

  size_t fileSize;
  {
    File destFile = SD.open( fileName );
    if( !destFile ) {
      Serial.printf( "Can't open %s\n", fileName );
      return;
    }
    fileSize = destFile.size();
    destFile.close();
  }

  esp_image_metadata_t sketchMeta = getSketchMeta( source_partition );
  uint32_t sketchSize = sketchMeta.image_len;

  Preferences preferences;
  preferences.begin( "sd-menu" );
  uint32_t menuSize = preferences.getInt( "menusize", 0 );
  uint8_t image_digest[32];
  preferences.getBytes( "digest", image_digest, 32 );
  preferences.end();

  if( menuSize==sketchSize ) {
    bool match = true;
    for( uint8_t i=0; i<32; i++ ) {
      if( image_digest[i]!=sketchMeta.image_digest[i] ) {
        Serial.println( "NONVSMATCH" );
        match = false;
        break;
      }
    }
    if( match ) {
      Serial.printf( "%s size (%d bytes) and hashes match %s's expected data from NVS: %d, no replication necessary\n", label, sketchSize, fileName, menuSize );
      return;
    }
  }
 
  Serial.printf( "%s (%d bytes) differs from %s's expected NVS size: %d, overwriting\n", label, sketchSize, fileName, fileSize );
  static uint8_t spi_rbuf[SPI_FLASH_SEC_STEP8];

  Serial.printf( " [INFO] Writing %s ...\n", fileName );
  File destFile = SD.open( fileName, FILE_WRITE );
  uint32_t bytescounter = 0;
  for ( uint32_t base_addr = source_partition->address; base_addr < source_partition->address + sketchSize; base_addr += SPI_FLASH_SEC_STEP8 ) {
    memset( spi_rbuf, 0, SPI_FLASH_SEC_STEP8 );
    spi_flash_read( base_addr, spi_rbuf, SPI_FLASH_SEC_STEP8 );
    destFile.write( spi_rbuf, SPI_FLASH_SEC_STEP8 );
    bytescounter++;
    if( bytescounter%128==0 ) {
      Serial.println( "." );
    } else {
      Serial.print( "." );
    }
  }
  Serial.println();
  destFile.close();

  preferences.begin( "sd-menu", false );
  preferences.putInt( "menusize", sketchSize );
  preferences.putBytes( "digest", sketchMeta.image_digest, 32 );
  preferences.end();

}



void setup() {
  Serial.begin( 115200 );
  Serial.println( WELCOME_MESSAGE );
  Serial.print( INIT_MESSAGE );
  M5.begin();
  //tft.begin();

  //Wire.begin(); // looks like this isn't needed anymore
  // Thanks to Macbug for the hint, my old ears couldn't hear the buzzing :-) 
  // See Macbug's excellent article on this tool:
  // https://macsbug.wordpress.com/2018/03/12/m5stack-sd-updater/
  dacWrite( 25, 0 ); // turn speaker signal off
  // Also thanks to @Kongduino for a complementary way to turn off the speaker:
  // https://twitter.com/Kongduino/status/980466157701423104
  ledcDetachPin( 25 ); // detach DAC
  
  if( digitalRead( BUTTON_A_PIN ) == 0 ) {
    Serial.println( GOTOSLEEP_MESSAGE );
    M5.setWakeupButton( BUTTON_B_PIN );
    M5.powerOFF();
  }
  
  tft.setBrightness(100);

  lastcheck = millis();
  bool toggle = true;
  tft.drawJpg(disk01_jpg, 1775, (tft.width()-30)/2, 100);
  tft.setTextSize(1);
  int16_t posx = ( tft.width() / 2 ) - ( tft.textWidth( SD_LOADING_MESSAGE ) / 2 );
  if( posx <0 ) posx = 0;
  tft.setCursor( posx, 136 );
  tft.print( SD_LOADING_MESSAGE );


  tft.setTextSize( 2 );
  while( !SD.begin( TFCARD_CS_PIN ) ) {
    // TODO: make a more fancy animation
    unsigned long now = millis();
    toggle = !toggle;
    uint16_t color = toggle ? BLACK : WHITE;
    tft.setCursor( 10,100 );
    tft.setTextColor( color );
    tft.print( INSERTSD_MESSAGE );
    if( toggle ) {
      tft.drawJpg( disk01_jpg, 1775, (tft.width()-30)/2, 100 );
      delay( 300 );
    } else {
      tft.drawJpg( disk00_jpg, 1775, (tft.width()-30)/2, 100 );
      delay( 500 );
    }
    // go to sleep after a minute, no need to hammer the SD Card reader
    if( lastcheck + 60000 < now ) {
      Serial.println( GOTOSLEEP_MESSAGE );
      M5.setWakeupButton( BUTTON_B_PIN );
      M5.powerOFF();
    }
  }

  tft.setTextColor( WHITE );
  tft.setTextSize( 1 );
  tft.clear();

  sdUpdater.SDMenuProgress( 10, 100 );

  if( migrateSPIFFS ) { // TODO: control this from the UI
    // scan for SPIFFS files waiting to be moved onto the SD Card
    scanDataFolder();
  }

  sdUpdater.SDMenuProgress( 20, 100 );
  listDir(SD, "/", 0);
  sdUpdater.SDMenuProgress( 30, 100 );
  aSortFiles();
  sdUpdater.SDMenuProgress( 40, 100 );
  buildM5Menu();

  #ifdef USE_PSP_JOY
    initJoyPad();
  #endif
  #ifdef USE_FACES_GAMEBOY
    initKeypad();
  #endif

  // TODO: animate loading screen
  tft.clear();
  /* fake loading progress, looks kool ;-) */
  for( uint8_t i=50; i<=80; i++ ) {
    sdUpdater.SDMenuProgress( i, 100 );
  }

  dumpSketchToSD( MENU_BIN );

  sdUpdater.SDMenuProgress( 100, 100 );
  
  M5Menu.drawAppMenu( MENU_TITLE, MENU_BTN_INFO, MENU_BTN_LOAD, MENU_BTN_NEXT );
  M5Menu.showList();
  renderIcon(0);
  inInfoMenu = false;
  lastcheck = millis();
  lastpush = millis();
  checkdelay = 300;

}


void loop() {

  HIDSignal hidState = getControls();

  switch( hidState ) {
    case UI_DOWN:
      menuDown();
    break;
    case UI_UP:
      menuUp();
    break;
    case UI_INFO:
      if( !inInfoMenu ) {
        menuInfo();
      } else {
        menuMeta();
      }
    break;
    case UI_LOAD:
      sdUpdater.updateFromFS( SD, fileInfo[ M5Menu.getListID() ].fileName );
      ESP.restart();
    break;
    default:
    case UI_INERT:
      if( inInfoMenu ) {
        // !! scrolling text also prevents sleep mode !!
        renderScroll( fileInfo[MenuID].jsonMeta.credits, 0, 5, 320 );
      }
    break;
  }

  M5.update();
  
  // go to sleep after 10 minutes if nothing happens
  if( lastpush + 600000 < millis() ) {
    Serial.println( GOTOSLEEP_MESSAGE );
    M5.setWakeupButton( BUTTON_B_PIN );
    M5.powerOFF();
  }

}
