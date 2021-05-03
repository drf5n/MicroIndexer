
// https://github.com/drf5n/MicroIndexer
// Based on the MicroIndexer shown at
// https://www.youtube.com/watch?v=0ACs5FRYcTE 
// Using an encoder, lcd, stepper driver, and Teensy 3.2.
// Hardware:
//  Teensy 3.2 : https://www.pjrc.com/store/teensy32.html
//  2x16 I2C Liquid Crystal Display
//  20 detent rotary encoder button (KY-040 ?)
//  A4988 stepper driver board like https://www.pololu.com/product/1182
//  NEMA 17 bipolar stepper motor 200 steps/rev
//  misc bits: breadboard, plexiglass, plywood knob, etc. 

// operation states:
enum MODES {DEGMODE, DIVMODE, RPMMODE, STEPMODE, CONFIGMODE, ZEROMODE, END};
enum MENU_DEPTH {TOP, RUN, CONFIG};
// at TOP, scroll between MODES click -> RUN, long ->CONFIG
// at RUN, jog within modes click -> TOP, long -> CONFIG
// at CONFIG, adjust mode if possible click-> RUN, long -> TOP

int debugFlag = 0;

const int numModes = END - DEGMODE; // all but END 
//int configs[numModes] = {0,0,0,0,0,0,0} ;
const char * title[numModes] = {
  "Degrees ",
  "Divisions ",
  "RPM ",
  "StepMode ",
  "CFG Steps ",
  "Zero? ",
};

MODES mode = DEGMODE;
MENU_DEPTH menu_level = RUN;
// DIVMODE state variables 
long div_index = 0; 
long divisions = 8; // DIVMODE divisions/rev
// DEGMODE 
long degrees = 0;
long deltaDegrees = 1;
float ddDegrees = 1;
// CONFIGMODE
int8_t microstep_state = 3 ; 
// RPMMODE
float rpm = 10;
float deltaRPM =1;
// STEPMODE
int dSteps = 1;

long spindle_step = 0; 
int steps_per_rev = 200;

const int max_steps_per_sec = (1000UL*F_CPU/16000000UL);  // Scale to 1000 on16MhZ

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}



#include <LiquidCrystal_I2C.h> // Output Display 
//  Teensy 3.2 SDA = 18, SCL=19
// UNO SCL = A5/D19 SDA=A4/D18
const int LC_ID = 0x27;
const byte LC_rows = 2;
const byte LC_cols = 16;

// rotary encoder to choose mode and jog
#include <Encoder.h> // Input control
const byte EC_A = 22; // Encoder Pins
const byte EC_B = 21; // Encoder Pins

Encoder knob=Encoder(EC_A,EC_B);
void setup_knob(){
  pinMode(EC_A,INPUT_PULLUP);
  pinMode(EC_A,INPUT_PULLUP);
};

// Handle encoder button
#include <AceButton.h> // Input button control
using ace_button::AceButton;
const byte enter_pin = 20; // Center button of Encoder
// template for handler
void handleEvent(AceButton* button, uint8_t eventType, uint8_t buttonState);
AceButton acebuttons(enter_pin);


#include <MultiStepper.h>     
#include <AccelStepper.h>      // Stepper Control
const byte ST_enable_pin = 7; 
const byte ST_MS1 = 8;
const byte ST_MS2 = 9;
const byte ST_MS3 = 10;
const byte ST_step_pin = 11;
const byte ST_dir_pin = 12;
const int steps_in_MSmode[] = {200,400,800,1600,3200};

//void setup_microsteps(int new_code);
AccelStepper stepper= AccelStepper(AccelStepper::DRIVER,ST_step_pin, ST_dir_pin);
void setup_stepper(){
  pinMode(ST_enable_pin,OUTPUT);
  digitalWrite(ST_enable_pin,LOW);
    pinMode(ST_MS1,OUTPUT);
    pinMode(ST_MS2,OUTPUT);
    pinMode(ST_MS3,OUTPUT);
    setup_microsteps(microstep_state);

  //stepper.setEnablePin(ST_enable_pin);
  stepper.setPinsInverted (false, false, false);
  stepper.setMaxSpeed(max_steps_per_sec); // steps/sec  Teensy 3.2 is 72/16x faster than Accelstepper's 1000
  stepper.setAcceleration(1000); // steps/sec/sec
}


void setup_button(){
  using namespace ace_button;
  pinMode(enter_pin,INPUT_PULLUP);
  ButtonConfig* config = acebuttons.getButtonConfig();
  config->setFeature(ButtonConfig::kFeatureLongPress);
  config->setFeature(ButtonConfig::kFeatureClick);
  config->setEventHandler(handleEvent);
  ;
}


int enterLong = 0;    //  
int enterClicked = 0; //
int handleTriggered =0; 

void handleEvent(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  handleTriggered++;
  switch (eventType) {
    case AceButton::kEventLongPressed:
      enterLong = 1;
      if(menu_level == CONFIG) { 
        menu_level = TOP;
      } else {
        menu_level = CONFIG;    
      }
      break;
    case AceButton::kEventClicked:
      if (menu_level != RUN ) {
        menu_level = RUN;
      }else{
        menu_level = TOP;
      }
      enterClicked = 1;
      break;
    case AceButton::kEventPressed:
    //  handleEventPressed(button, eventType, buttonState);
      break;
    case ace_button::AceButton::kEventReleased:
     // handleEventReleased(button, eventType, buttonState);
      break;
    default:
      Serial.print("Unknown acebutton handleEvent ");
      Serial.println(eventType);
      Serial.print(":");
      Serial.println(buttonState); 
  }
  Serial.println("event_Type");
}



LiquidCrystal_I2C lcd=LiquidCrystal_I2C(LC_ID, LC_rows,LC_cols);
void setup_lcd(){
  lcd.init();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("github.com/drf5na/MicroIndexer");
  lcd.setCursor(0,1);
  lcd.print("/MicroIndexer/           ");
  delay(2000);
}

void setup_menus(){
  for(int i = 0;i<numModes; i++){ 
  ;
  }
}


#include <TimerOne.h>
void setup_background(unsigned int dt){
  Timer1.initialize(dt);
  Timer1.attachInterrupt(doBackground);
}


void doBackground(){  // Do these tasks in the background, every millisecond
  long int nextUI = 2000; // us
  long nowmicros = micros();
  stepper.run();
  if( nowmicros - nextUI > 0 ) { knob.read(); acebuttons.check(); }
}


void setup_microsteps(int new_code){
  // try to update microsteps and spindle
  // 
  static int old_code = -1;
  if (old_code < 0){
    spindle_step = 0;
  }
  if (new_code != old_code){
    // Recalculate position in steps (microsteps)
    spindle_step = (spindle_step * steps_in_MSmode[new_code])/steps_in_MSmode[old_code];
    stepper.setCurrentPosition(spindle_step); // update stepper state variable
    old_code = new_code;
    
    steps_per_rev = steps_in_MSmode[new_code];
    switch (new_code){
      case 0: // no microstepping, assume 200 steps/rev 000
        digitalWrite(ST_MS1,LOW);
        digitalWrite(ST_MS2,LOW);
        digitalWrite(ST_MS3,LOW);
        break;
      case 1: // half microstepping, assume 400 steps/rev 100
        digitalWrite(ST_MS1,HIGH);
        digitalWrite(ST_MS2,LOW);
        digitalWrite(ST_MS3,LOW);
        break;
      case 2: // quarter microstepping, assume 800 steps/rev 010
        digitalWrite(ST_MS1,LOW);
        digitalWrite(ST_MS2,HIGH);
        digitalWrite(ST_MS3,LOW);
        break;
      case 3: // Eighth microstepping, assume 1600 steps/rev 110
        digitalWrite(ST_MS1,HIGH);
        digitalWrite(ST_MS2,HIGH);
        digitalWrite(ST_MS3,LOW);
       break;
    case 4: // sixteenth microstepping, assume 3200 steps/rev 111
       digitalWrite(ST_MS1,HIGH);
       digitalWrite(ST_MS2,HIGH);
       digitalWrite(ST_MS3,HIGH);
       break;
    default:
       digitalWrite(ST_MS1,LOW);
       digitalWrite(ST_MS2,LOW);
       digitalWrite(ST_MS3,LOW);
       break;
    }
  }
}

void setup() {
  // put your setup code here, to run once:
  setup_lcd();
  setup_knob();
  setup_stepper();
  setup_button();
  setup_menus();
  setup_microsteps(microstep_state);
  Serial.begin(9600);
  setup_background(50); // micros?
}

void update_display(){
  static unsigned long lastupdate = 0;
//  lcd.print("U ");
//lcd.print(nextupdate - millis());
  if (millis() -lastupdate < 250 ) // not time yet 
    return;
  lastupdate += 250; // 
  lcd.setCursor(0,0); // col,row
 // lcd.print("u");
 // lcd.print(mode);
 // lcd.print(knob.read());
 // lcd.print(enterClicked);
 // lcd.print(enterLong);
  lcd.print(title[mode]);
  lcd.print("            ");
  //lcd.print(configs[mode]);
  lcd.setCursor(13,0);
  switch(menu_level){
    case TOP:
      lcd.print("<->");
      break;
    case RUN: 
      lcd.print("RUN");
      break;
    case CONFIG:
    lcd.print("CFG");
      break;
  }
  lcd.print("          ");
  lcd.setCursor(0,1);
  switch(mode){
    case DIVMODE:
      lcd.print(div_index);
      lcd.print("/");
      lcd.print(divisions);
      lcd.print(" (");
      lcd.print(divisions*stepper.currentPosition()/(float)steps_per_rev,4);
      lcd.print(" act)");
      
      break;
    case DEGMODE:
      lcd.print(360.0 * stepper.currentPosition()/steps_per_rev,3);
      lcd.print("~");
      lcd.print(degrees);
      lcd.print("/");
      lcd.print(deltaDegrees);
      break;
    case STEPMODE:
      lcd.print((int)microstep_state);
      lcd.print(": ");
      lcd.print(stepper.currentPosition());
      lcd.print("/");
      lcd.print(steps_per_rev);
      
      break;
    case RPMMODE:
      lcd.print(rpm);
      lcd.print(" S:" ); 
      lcd.print(60.0 * stepper.speed()/steps_per_rev,1);
      lcd.print("/");
      lcd.print(60.0 * stepper.maxSpeed()/steps_per_rev,1);
      break;
    case CONFIGMODE:
      lcd.print(microstep_state);
      lcd.print(":");
      lcd.print(steps_per_rev);
      lcd.print("/rev");
      break;
      
    default:
      //lcd.setCursor(0,0);
      lcd.print("Unhandled Mode ");
      lcd.print(mode)
     ; 
  }
  lcd.print("                ");
  if(Serial){
    Serial.print("knob: ");
    Serial.print(knob.read());
    Serial.print(" /4: ");
    Serial.print(knob.read()/4);
    Serial.print(" Enter: ");
    Serial.print(digitalRead(enter_pin));
    Serial.print("(");
    Serial.print(enterClicked);
    Serial.print(enterLong);
    Serial.print(",");
    Serial.print(handleTriggered);
    Serial.print(")");
    Serial.print(": ");
    Serial.print(mode);
    Serial.print("/");
    Serial.print(menu_level);
    Serial.print(": ");
    Serial.print(title[mode]);
    Serial.print("/");
    Serial.print(menu_level);
    Serial.print(" Step: ");
    Serial.print(stepper.currentPosition());
    Serial.print("/");    
    Serial.print(stepper.targetPosition());
    Serial.print("@");
    Serial.print(stepper.speed());
    Serial.print(" ");
    Serial.print(rpm);
    Serial.print("RPM");
    Serial.print(" D:");
    Serial.print(debugFlag);
    Serial.println();
    
  }
  
}

void handle_mode(int deltaKnob){
  // Act on the [mode,menu_level,deltaKnob]  
  switch (mode) {
    case ZEROMODE:  // Zero the spindle
      if (menu_level == RUN) {
        spindle_step = 0;
        degrees = 0;
        div_index = 0;
        stepper.setCurrentPosition(spindle_step);
        menu_level = TOP;     
      }
    break;               
    case STEPMODE:
      switch(menu_level){
        case RUN: // knob indexes
          if(deltaKnob != 0){
            if(deltaKnob > 0 ){
              stepper.move(dSteps);
            } else {
              stepper.move(-dSteps);
            }
          }
          break;
         case CONFIG: // knob changes divisions
          if(deltaKnob != 0 ){
            dSteps += (deltaKnob>0? 1 : -1);
            if (dSteps < 1) 
              dSteps = 1;
          }
      }
      break;    
    case DIVMODE:
      switch(menu_level){
        case RUN: // knob indexes by division
          if(deltaKnob != 0) {
            div_index += (deltaKnob>0? 1:-1);
            stepper.moveTo(round((steps_per_rev*div_index+0.5)/divisions));
          }
          break;
        case CONFIG: // knob changes divisions
          if(deltaKnob != 0 ){
            divisions += (deltaKnob>0? 1 : -1);
            if (divisions < 1) 
              divisions = 1;
          }
        } // 
      break;
    case DEGMODE:
      switch(menu_level){
        case RUN: // knob indexes by #degrees
          if(deltaKnob != 0) {
            degrees += (deltaKnob>0? deltaDegrees:-deltaDegrees);
            stepper.moveTo(round((steps_per_rev*degrees+.5)/360));
          }
          break;
        case CONFIG: // knob changes deltaDegrees
          if(deltaKnob != 0 ){
            deltaDegrees += (deltaKnob>0? 1 : -1);
            if (deltaDegrees < 1) 
              deltaDegrees = 1;
          }
          break;
        default:
          break;
        } //
      break;
    case RPMMODE:
      switch(menu_level){
        case TOP:
          //rpm = 0.0;
         // stepper.moveTo(stepper.currentPosition());
          break;
        case RUN: // knob indexes by division
            // trick to change by 1/20*
          if(deltaKnob != 0) {
          deltaRPM = exp(trunc(log(rpm*(rpm>0?1:-1))/log(5)-2.43)*log(5));
            if(deltaKnob > 0) { // turn CW/more CW
              rpm += 1 * deltaRPM;
            } else if (deltaKnob < 0) { // turn CCW, more CCW
             rpm -= 1 * deltaRPM;
            }
          }
          
          rpm = constrain(rpm, -60*max_steps_per_sec/steps_per_rev,60*max_steps_per_sec/steps_per_rev);  
          //stepper.setSpeed(abs(rpm)*60*steps_per_rev);
          //stepper.runSpeedToPosition();
          //stepper.setMaxSpeed( min(abs(rpm)*60*steps_per_rev,max_steps_per_sec));
          stepper.setMaxSpeed(max(1e-12,((rpm>0)?1:-1)*rpm/60.0*steps_per_rev));
          stepper.moveTo((rpm > 0? 1:-1) *1000000); // ;* steps_per_rev);
          break;
        case CONFIG: // knob changes divisions
            debugFlag = 1;
          if(deltaKnob != 0 ){
            rpm = (deltaKnob>0? 10 : -10);
          }
          break;
        default:
          break;
        } // RUNMODE+menu_level 
      break;


    case CONFIGMODE:
      switch(menu_level){
        case RUN: // Change microstep level
          break;
        case CONFIG: // knob changes deltaDegrees
          if(deltaKnob != 0 ){
            microstep_state += (deltaKnob>0? 1 : -1);
            if( microstep_state < 0 ) microstep_state = 0;
            if( microstep_state > 4 ) microstep_state = 4;
            setup_microsteps(microstep_state);
          }
          break;
        default:
          break;
        } // 
      break;

    default:
      Serial.print("Unhandled Mode: ");
      Serial.print(mode);
      break;     
  }
}

void loop() {
  static int lastKnob = 0;
  int nowKnob, deltaKnob;

//  stepper.run(); // do stepper motion defer to interrupt
  acebuttons.check(); // polls button & handleEvents() sets enterClicked, enterLong and menu_depth.
  //lcd.print("L");
  nowKnob = knob.read()/4;
  deltaKnob = nowKnob - lastKnob;
  lastKnob = nowKnob;
    
  if (menu_level == TOP && deltaKnob != 0 ) { // change menu item:
    if (deltaKnob > 0 ) mode = mode + 1;
    if (deltaKnob < 0 ) mode = mode - 1;
    mode = mode % END;  // restrict to valid modes 
  }
  update_display();
  handle_mode(deltaKnob);
  delay(5); // 100ms
}
