#include "Arduino.h"
#include "TFT_eSPI.h"/* Please use the TFT library provided in the library. */
#include "pin_config.h"
#include "OneButton.h"
#include "Wire.h"

#define CTS328_SLAVE_ADDRESS (0x1A)
#define CTS820_SLAVE_ADDRESS  (0X15)

// #define TOUCH_MODULES_CST_MUTUAL    //Early use of CST328
#define TOUCH_MODULES_CST_SELF        //Use CST816 by default

#include "TouchLib.h"

#if defined(TOUCH_MODULES_CST_MUTUAL)
TouchLib touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS328_SLAVE_ADDRESS, PIN_TOUCH_RES);
#elif defined(TOUCH_MODULES_CST_SELF)
TouchLib touch(Wire, PIN_IIC_SDA, PIN_IIC_SCL, CTS820_SLAVE_ADDRESS, PIN_TOUCH_RES);
#endif

#define TOUCH_GET_FORM_INT 0

#define WIDTH 320
#define HEIGHT 170

// Wave equation applies to pixels with pixelStatus values of NORMAL_PIXEL, ABSORBANT_PIXEL, GLASS_PIXEL
#define NORMAL_PIXEL 0
// ABSORBANT pixels have high damping on v
#define ABSORBANT_PIXEL 1
// GLASS pixels exhibit impedance by changing v less (with a lower proportionality constant)
#define GLASS_PIXEL 2
// WALL pixels stay fixed with zeroes in u and v, and a hardcoded 16-bit color value in image
#define WALL_PIXEL 3
// Remaining SOURCE types set u to various amplitudes
#define LOW_FREQ_POS_SOURCE_PIXEL 4
#define LOW_FREQ_NEG_SOURCE_PIXEL 5
#define MID_FREQ_POS_SOURCE_PIXEL 6
#define MID_FREQ_NEG_SOURCE_PIXEL 7
#define HIGH_FREQ_POS_SOURCE_PIXEL 8
#define HIGH_FREQ_NEG_SOURCE_PIXEL 9
#define PHASED_ARRAY_SOURCE_PIXEL 10

// 0.1 creates waves with ~16 px half wavelength:
#define RADIANS_PER_ITERATION 0.1

// For phased array pixels only:
#define RADIANS_PER_PIXEL 0.15

// Shifting right 2 bits (i.e. division by 4) implies a refractive index of sqrt(4) = 2.0
#define GLASS_REFRACTION_BIT_SHIFT 2

// Using only half the available INT32 range to guard against overflow after addition operations
#define MIN_RANGE -0x40000000
#define MAX_RANGE 0x3FFFFFFF

#define LOW_DAMPING_BIT_SHIFT 12
#define HIGH_DAMPING_BIT_SHIFT 5

#define TOUCH_ONLY_MODE 0
#define RANDOM_POINTS_MODE 1
#define RANDOM_POINTS_ABSORBER_MODE 2
#define RANDOM_POINTS_MULTIFREQUENCY_MODE 3
#define RANDOM_POINTS_MULTIFREQUENCY_ABSORBER_MODE 4
#define MONOPOLE_MODE 5
#define MONOPOLE_ABSORBER_MODE 6
#define DIPOLE_MODE 7
#define DIPOLE_ABSORBER_MODE 8
#define QUADRUPOLE_MODE 9
#define QUADRUPOLE_ABSORBER_MODE 10
#define SUPERPOSITION_MODE 11
#define FLAT_MIRROR_MODE 12
#define PARABOLIC_MIRROR_MODE 13
#define ELLIPTIC_MIRROR_MODE 14
#define REFRACTION_MODE 15
#define PRISM_MODE 16
#define LENS_MODE 17
#define PARTIAL_INTERNAL_REFLECTION_MODE 18
#define TOTAL_INTERNAL_REFLECTION_MODE 19
#define FIBER_OPTIC_MODE 20
#define WAVEGUIDE_MODE 21
#define PHASED_ARRAY_MODE 22
#define DOUBLE_SLIT_DIFFRACTION_MODE 23
#define DIFFRACTION_GRATING_MODE 24
#define MAZE_MODE 25

#define TOTAL_MODES_COUNT 26

#define RED_BLUE_SCALE 0
#define YELLOW_PURPLE_SCALE 1
#define RED_GREEN_SCALE 2
#define YELLOW_CYAN_SCALE 3
#define BLUE_GREEN_SCALE 4
#define CYAN_PURPLE_SCALE 5

#define TOTAL_SCALE_COUNT 6

uint8_t button_1_state = 0, button_2_state = 0, prev_button_1_state = 0, prev_button_2_state = 0;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// Array of current wave amplitudes and their first partial derivatives with respect to time
int32_t *u, *v;
// Array indicating pixel types (NORMAL_PIXEL, WALL_PIXEL, ABSORBANT_PIXEL, GLASS_PIXEL, etc.)
uint8_t *pixelType;
// Array for a full-screen image, 16-bit color encoding
uint16_t *image;

uint8_t normalDampingBitShift = LOW_DAMPING_BIT_SHIFT;
uint8_t absorbantDampingBitShift = HIGH_DAMPING_BIT_SHIFT;

uint32_t loopCounter = 0;
uint64_t startTime = esp_timer_get_time();

uint8_t mode;
uint8_t colorScale;
String label = "";

uint64_t timestamp = 0;

bool touchEnabled = false;
int lastTouchI = -1, lastTouchJ = -1;
int touchPolarity = 1;
bool touched = 0;

int32_t applyCap(int32_t x) {
  if (x < MIN_RANGE) {
    return MIN_RANGE;
  }
  if (x > MAX_RANGE) {
    return MAX_RANGE;
  }
  return x;
}

/*
 * Given row i, column j returns index into u, v, pixelType, and image arrays.
 * Used extensively from within clearField(), initalizeField(), and when processing touch events;
 * avoided elsewhere because it does multiplication.
 */
int toIndex(int i, int j) {
  return (i * WIDTH) + j;
}

/*
 * Used extensively from within initializeField().
 * Sets values in pixelStatus array to WALL_PIXEL along edges, ABSORBANT_PIXEL within specified padding regions, and NORMAL_PIXEL everywhere else.
 */
void clearField(int northPadding, int eastPadding, int southPadding, int westPadding) {
  for (int i = 0; i < HEIGHT; i++) {
    for (int j = 0; j < WIDTH; j++) {
      int index = toIndex(i, j);
      u[index] = v[index] = 0;
      image[index] = 0;
      pixelType[index] = NORMAL_PIXEL;
      if (i < northPadding + 1 || j < westPadding + 1 || i >= HEIGHT - southPadding - 1 || j >= WIDTH - eastPadding - 1) {
        pixelType[index] = ABSORBANT_PIXEL;
      }
      if (i == 0 || j == 0 || i == HEIGHT - 1 || j == WIDTH - 1) {
        pixelType[index] = WALL_PIXEL;
      }
    }
  }
  loopCounter = 0; // Reset phase of sine wave used for SOURCE type pixels
}

/*
 * Huge function that initializes values in pixelStatus array for any given mode; runs only on startup and when the mode is updated.
 */
void initializeField() {

  int centerI = HEIGHT >> 1;
  int centerJ = WIDTH >> 1;
  clearField(0, 0, 0, 0);
  label = "";

  switch(mode) {
    case TOUCH_ONLY_MODE:
      label = "TOUCH ONLY";
      break;
    case RANDOM_POINTS_ABSORBER_MODE:
      label = " (ABSORBING BOUNDARY)";
      clearField(15, 15, 15, 15); // fall through
    case RANDOM_POINTS_MODE:
      for (int point = 0; point < 6; point++) {
        pixelType[toIndex(random(30, HEIGHT - 30), random(30, WIDTH - 30))] = point % 2 ? MID_FREQ_POS_SOURCE_PIXEL : MID_FREQ_NEG_SOURCE_PIXEL;
      }
      label = "RANDOM POINTS" + label;
      break;
    case RANDOM_POINTS_MULTIFREQUENCY_ABSORBER_MODE:
      label = " (ABSORBING BOUNDARY)";
      clearField(15, 15, 15, 15); // fall through
    case RANDOM_POINTS_MULTIFREQUENCY_MODE:
      pixelType[toIndex(random(30, HEIGHT - 30), random(30, WIDTH - 30))] = MID_FREQ_POS_SOURCE_PIXEL;
      pixelType[toIndex(random(30, HEIGHT - 30), random(30, WIDTH - 30))] = MID_FREQ_NEG_SOURCE_PIXEL;
      pixelType[toIndex(random(30, HEIGHT - 30), random(30, WIDTH - 30))] = HIGH_FREQ_POS_SOURCE_PIXEL;
      pixelType[toIndex(random(30, HEIGHT - 30), random(30, WIDTH - 30))] = HIGH_FREQ_NEG_SOURCE_PIXEL;
      pixelType[toIndex(random(30, HEIGHT - 30), random(30, WIDTH - 30))] = LOW_FREQ_POS_SOURCE_PIXEL;
      pixelType[toIndex(random(30, HEIGHT - 30), random(30, WIDTH - 30))] = LOW_FREQ_NEG_SOURCE_PIXEL;
      label = "MULTIFREQUENCY POINTS" + label;
      break;
    case MONOPOLE_ABSORBER_MODE:
      label = " (ABSORBING BOUNDARY)";
      clearField(15, 15, 15, 15); // fall through
    case MONOPOLE_MODE:
      pixelType[toIndex(centerI, centerJ)] = MID_FREQ_POS_SOURCE_PIXEL;
      label = "MONOPOLE" + label;
      break;
    case DIPOLE_ABSORBER_MODE:
      label = " (ABSORBING BOUNDARY)";
      clearField(15, 15, 15, 15); // fall through
    case DIPOLE_MODE:
      pixelType[toIndex(centerI, centerJ) - 10] = MID_FREQ_NEG_SOURCE_PIXEL;
      pixelType[toIndex(centerI, centerJ) + 10] = MID_FREQ_POS_SOURCE_PIXEL;
      label = "DIPOLE" + label;
      break;
    case QUADRUPOLE_ABSORBER_MODE:
      label = " (ABSORBING BOUNDARY)";
      clearField(15, 15, 15, 15); // fall through
    case QUADRUPOLE_MODE:
      pixelType[toIndex(centerI - 10, centerJ) - 10] = MID_FREQ_POS_SOURCE_PIXEL;
      pixelType[toIndex(centerI + 10, centerJ) - 10] = MID_FREQ_NEG_SOURCE_PIXEL;
      pixelType[toIndex(centerI + 10, centerJ) + 10] = MID_FREQ_POS_SOURCE_PIXEL;
      pixelType[toIndex(centerI - 10, centerJ) + 10] = MID_FREQ_NEG_SOURCE_PIXEL;      
      label = "QUADRUPOLE" + label;
      break;
    case SUPERPOSITION_MODE: {
      int padding = 30;
      clearField(padding, padding, padding, padding);
      int halfWidth = 10;
      int left = 100;
      int top = 60;
      int guideLength = 30;

      for (int j = 1; j < guideLength; j++) {
        pixelType[toIndex(top, j)] = WALL_PIXEL;
        pixelType[toIndex(top + 4 * halfWidth, j)] = WALL_PIXEL;
        for (int i = top + 1; i < top + 4 * halfWidth; i++) {
          pixelType[toIndex(i, 1)] = LOW_FREQ_POS_SOURCE_PIXEL;
          for (int j = 2; j <= padding; j++) {
            pixelType[toIndex(i, j)] = NORMAL_PIXEL;
          }
        }
      }
      for (int i = 1; i < guideLength; i++) {
        pixelType[toIndex(i, left)] = WALL_PIXEL;
        pixelType[toIndex(i, left + halfWidth)] = WALL_PIXEL;
        for (int j = left + 1; j < left + halfWidth; j++) {
          pixelType[toIndex(1, j)] = HIGH_FREQ_POS_SOURCE_PIXEL;
          for (int i = 2; i <= padding; i++) {
            pixelType[toIndex(i, j)] = NORMAL_PIXEL;
          }
        }
      }
      label = "SUPERPOSITION";
      break;
    }
    case FLAT_MIRROR_MODE:
      clearField(25, 25, 25, 0);
      for (int i = 25; i < HEIGHT - 25; i++) {
        pixelType[toIndex(i, 1)] = MID_FREQ_POS_SOURCE_PIXEL;
      }
      for (int i = 50; i < HEIGHT - 50; i++) {
        pixelType[toIndex(i, 1.5 * i)] = WALL_PIXEL;
      }
      label = "FLAT MIRROR";
      break;
    case PARABOLIC_MIRROR_MODE:
      /* sideways version:
      clearField(30, 10, 0, 10);
      for (int i = 1; i < HEIGHT - 1; i++) {
        int j = (i - centerI) * (i - centerI) >> 5;
        j += j >> 1;
        for (int k = 0; k < 8; k++) {
          pixelType[toIndex(i, j + k)] = WALL_PIXEL;
        }
      }
      pixelType[toIndex(centerI, 15)] = HIGH_FREQ_POS_SOURCE_PIXEL;
      */
      clearField(40, 0, 0, 0);
      for (int j = 0; j <= centerJ; j++) {
        int y = (j * j) / 150;
        pixelType[toIndex(HEIGHT - y, centerJ - j)] = WALL_PIXEL;
        pixelType[toIndex(HEIGHT - y, centerJ + j)] = WALL_PIXEL;
      }
      for (int j = 10; j < WIDTH - 10; j++) {
        pixelType[toIndex(0, j)] = HIGH_FREQ_POS_SOURCE_PIXEL;
      }
      label = "PARABOLIC MIRROR";
      break;
    case ELLIPTIC_MIRROR_MODE: {
      int a = centerJ;
      int b = centerI;
      for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
          int x = j - a;
          int y = i - b;
          int rangeFactor = (x * x * b * b) + (a * a * y * y) - ( a * a * b * b);
          if (rangeFactor >= 0 && rangeFactor < 10000000) {
            pixelType[toIndex(i, j)] = WALL_PIXEL;
          }
        }
      }
      pixelType[toIndex(b, 25)] = HIGH_FREQ_POS_SOURCE_PIXEL;
      label = "ELLIPTIC MIRROR";
      break;
    }
    case REFRACTION_MODE:
      clearField(20, 20, 20, 20);
      for (int i = 25; i < HEIGHT - 25; i++) {
        for (int j = centerJ - 30; j <= centerJ + 30; j++) {
          pixelType[toIndex(i, j)] = GLASS_PIXEL;
        }
        if (i > 100) {
          pixelType[toIndex(i, i - 17)] = MID_FREQ_POS_SOURCE_PIXEL;
        }
      }
      label = "REFRACTION";
      break;
    case PRISM_MODE: {
      clearField(20, 20, 20, 20);
      for (int i = 20; i < 150; i++) {
        int prismHalfWidth = (i - 20) * 75 / 130;
        for (int j = centerJ - prismHalfWidth; j <= centerJ + prismHalfWidth; j++) {
          pixelType[toIndex(i, j)] = GLASS_PIXEL;
        }
      }

      for (int i = centerI; i < HEIGHT - 20; i++) {
        if (i > 130) {
          pixelType[toIndex(i, (i - 70))] = MID_FREQ_POS_SOURCE_PIXEL;
        }
      }
      label = "PRISM";
      break;
    }
    case LENS_MODE: {
      clearField(20, 30, 20, 30);
      int maxRadiusSquared = (centerJ + 100) * (centerJ + 100);
      int leftRadialFocusHorizontalPosition = -140;
      int rightRadialFocusHorizontalPosition = WIDTH + 40;
      for (int i = 21; i < HEIGHT - 21; i++) {
        pixelType[toIndex(i, 0)] = HIGH_FREQ_POS_SOURCE_PIXEL;
        for (int j = 22; j < WIDTH - 21; j++) {
          if (((centerI - i) * (centerI - i)) + ((leftRadialFocusHorizontalPosition - j) * (leftRadialFocusHorizontalPosition - j)) < maxRadiusSquared) {
            if (((centerI - i) * (centerI - i)) + ((rightRadialFocusHorizontalPosition - j) * (rightRadialFocusHorizontalPosition - j))  < maxRadiusSquared) {
              pixelType[toIndex(i, j)] = GLASS_PIXEL;
            }
          }
        }
      }
      label = "LENS";
      break;
    }
    case PARTIAL_INTERNAL_REFLECTION_MODE:
      clearField(20, 20, 20, 10);
      for (int i = centerI; i < HEIGHT - 20; i++) {
        for (int j = 1; j < WIDTH - 18; j++) {
          pixelType[toIndex(i, j)] = GLASS_PIXEL;
        }
        if (i > 100) {
          pixelType[toIndex(i, (i - 100) << 1)] = MID_FREQ_POS_SOURCE_PIXEL;
          for (int j = 1; j < (i - 100) << 1; j++) {
            pixelType[toIndex(i, j)] = ABSORBANT_PIXEL;
          }
        }
      }
      label = "PARTIAL INTERNAL REFLECTION";
      break;
    case TOTAL_INTERNAL_REFLECTION_MODE:
      clearField(20, 20, 20, 20);
      for (int i = centerI; i < HEIGHT - 20; i++) {
        for (int j = 1; j < WIDTH - 18; j++) {
          pixelType[toIndex(i, j)] = GLASS_PIXEL;
        }
        if (i > 100) {
          pixelType[toIndex(i, (i - 100) >> 1)] = MID_FREQ_POS_SOURCE_PIXEL;
          for (int j = 1; j < ((i - 100) >> 1); j++) {
            pixelType[toIndex(i, j)] = ABSORBANT_PIXEL;
          }
        }
      }
      label = "TOTAL INTERNAL REFLECTION";
      break;
    case FIBER_OPTIC_MODE: {
      clearField(0, 40, 0, 10);

      int topCenter = 40;
      for (int i = topCenter - 15; i <= topCenter + 15; i++) {
        for (int j = 2; j < WIDTH - 41; j++) {
          pixelType[toIndex(i, j)] = GLASS_PIXEL;
        }
      }
      for (int i = topCenter - 13; i <= topCenter + 13; i++) {
        pixelType[toIndex(i, 1)] = LOW_FREQ_POS_SOURCE_PIXEL;
      }

      int middleCenter = centerI + 25;
      for (int i = middleCenter - 8; i <= middleCenter + 8; i++) {
        for (int j = 2; j < WIDTH - 41; j++) {
          pixelType[toIndex(i, j)] = GLASS_PIXEL;
        }
      }
      for (int i = middleCenter - 6; i <= middleCenter + 6; i++) {
        pixelType[toIndex(i, 1)] = MID_FREQ_POS_SOURCE_PIXEL;
      }

      int bottomCenter = HEIGHT - 20;
      for (int i = bottomCenter - 4; i <= bottomCenter + 4; i++) {
        for (int j = 2; j < WIDTH - 41; j++) {
          pixelType[toIndex(i, j)] = GLASS_PIXEL;
        }
      }
      for (int i = bottomCenter - 3; i <= bottomCenter + 3; i++) {
        pixelType[toIndex(i, 1)] = HIGH_FREQ_POS_SOURCE_PIXEL;
      }      
      label = "FIBER OPTIC CABLES";
      break;
    }
    case WAVEGUIDE_MODE: {
      clearField(15, 15, 15, 15);
      int halfWidth = 25;
      for (int j = 1; j < centerJ; j++) {
        pixelType[toIndex(centerI - halfWidth, j)] = WALL_PIXEL;
        pixelType[toIndex(centerI + halfWidth, j)] = WALL_PIXEL;
      }
      for (int i = centerI - halfWidth + 1; i < centerI + halfWidth - 1; i++) {
        pixelType[toIndex(i, 1)] = i > centerI ? MID_FREQ_POS_SOURCE_PIXEL : MID_FREQ_NEG_SOURCE_PIXEL;
        for (int j = 2; j <= 15; j++) {
          pixelType[toIndex(i, j)] = NORMAL_PIXEL;
        }
      }
      label = "WAVEGUIDE";
      break;
    }
    case PHASED_ARRAY_MODE:
      clearField(15, 15, 15, 15);
      for (int j = centerJ - 100; j < centerJ + 100; j++) {
        pixelType[toIndex(centerI, j)] = PHASED_ARRAY_SOURCE_PIXEL;
      }
      label = "PHASED ARRAY";
      break;
    case DOUBLE_SLIT_DIFFRACTION_MODE: {
      clearField(20, 20, 0, 20);
      int halfSlitWidth = 10;
      for (int j = 0; j < WIDTH; j++) {
        pixelType[toIndex(HEIGHT - 1, j)] = ((j > centerJ - halfSlitWidth && j < centerJ + halfSlitWidth)
          || (j < centerJ - (3 * halfSlitWidth))
          || (j > centerJ + (3 * halfSlitWidth)))
          ? WALL_PIXEL : MID_FREQ_POS_SOURCE_PIXEL;
      }
      label = "DOUBLE SLIT DIFFRACTION";
      break;
    }
    case DIFFRACTION_GRATING_MODE:
      clearField(25, 20, 0, 20);
      for (int j = 1; j < WIDTH - 1; j++) {
        if (j > 110 && j < WIDTH - 110 && (j % 20 < 5 || j % 20 > 15)) {
          pixelType[toIndex(HEIGHT - 1, j)] = HIGH_FREQ_POS_SOURCE_PIXEL;
        }
      }
      label = "DIFFRACTION GRATING";
      break;
    case MAZE_MODE: {
      bool leftward = true;
      for (int i = HEIGHT / 5; i < HEIGHT; i += HEIGHT / 5) {
        if (leftward) {
          for (int j = 0; j < WIDTH - (HEIGHT / 5); j++) {
            pixelType[toIndex(i, j)] = WALL_PIXEL;
          }
        } else {
          for (int j = HEIGHT / 5; j < WIDTH; j++) {
            pixelType[toIndex(i, j)] = WALL_PIXEL;
          }
        }
        leftward = !leftward;
      }
      for (int j = 1; j < (HEIGHT / 5) - 1; j++) {
        pixelType[toIndex(j, 1)] = MID_FREQ_POS_SOURCE_PIXEL;
      }
      label = "MAZE";
      break;
    }
    default:
      label = "";
      break;
  }
}

void setup() {

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  Serial.begin(115200);
  Serial.println("Initializing LILYGO T-Display-S3");

  pinMode(PIN_BUTTON_1, INPUT);
  pinMode(PIN_BUTTON_2, INPUT);

  pinMode(PIN_TOUCH_RES, OUTPUT);
  digitalWrite(PIN_TOUCH_RES, LOW);
  delay(500);
  digitalWrite(PIN_TOUCH_RES, HIGH);

  Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);

  // Check to see if this is the touch version
  if (!touch.init()) {
    Serial.println("Touch IC not found.");
    touchEnabled = false;
  } else {
    Serial.println("Found touch IC.");
    touchEnabled = true;
  }

  // Initialize the screen and set up a full-screen sprite
  tft.init();
  tft.setRotation(1);
  sprite.createSprite(WIDTH, HEIGHT);
  sprite.setTextColor(TFT_GREEN);

  // Allocate arrays: pixelType, u, v, and image
  pixelType = (uint8_t*)malloc(WIDTH * HEIGHT);

  u = (int32_t*)malloc(WIDTH * HEIGHT * 4);
  v = (int32_t*)malloc(WIDTH * HEIGHT * 4);

  image = (uint16_t*)malloc(WIDTH * HEIGHT * 2);

  // Initialize mode value, startTime, and pixelType array:
  mode = touchEnabled ? TOUCH_ONLY_MODE : RANDOM_POINTS_MODE;
  initializeField();

  colorScale = RED_BLUE_SCALE;
}

void loop() {

  // First: Check the buttons
  button_1_state = digitalRead(PIN_BUTTON_1);
  button_2_state = digitalRead(PIN_BUTTON_2);

  // Button 1 advances the mode, advances the color scale, and resets the field.
  if (button_1_state != prev_button_1_state) {
    if (button_1_state == LOW) {
      touched = true;
      mode++;
      if (mode == TOTAL_MODES_COUNT) {
        mode = touchEnabled ? TOUCH_ONLY_MODE : RANDOM_POINTS_MODE;
      }
      colorScale++;
      if (colorScale == TOTAL_SCALE_COUNT) {
        colorScale = 0;
      }
      startTime = esp_timer_get_time();
      timestamp = startTime;
      initializeField();
    } // else Serial.println("BUTTON 1 RELEASED");
  }

  // Button 2 advances the color scale only, before resetting the field
  if (button_2_state != prev_button_2_state) {
    if (button_2_state == LOW) {
      touched = true;
      colorScale++;
      if (colorScale == TOTAL_SCALE_COUNT) {
        colorScale = 0;
      }
      startTime = esp_timer_get_time();
      timestamp = startTime;
      initializeField();
    } // else Serial.println("BUTTON 2 RELEASED");
  }

  prev_button_1_state = button_1_state;
  prev_button_2_state = button_2_state;
  
  if (button_1_state == LOW || button_2_state == LOW) {
    delay(100); // Pro forma debounce (main loop already gives pin voltages enough time to stop ringing)
    return;
  }
  
  // Handle touch logic: touching and dragging on the screen introduces large values into v that change sign with alternating strokes.
  if (touchEnabled) {
    bool read = false;
    try {
      read = touch.read();
    }
    catch(...) {
      Serial.println("Touch read error");
    }
    if (read) {
      touched = true;
      TP_Point t = touch.getPoint(0);
      int i = HEIGHT - t.x;
      int j = t.y;
      v[toIndex(i, j)] = touchPolarity * MAX_RANGE >> 1;
      if (lastTouchI != -1 && lastTouchJ != -1) {
        // Clumsy loop to draw a line from lastTouchI, lastTouchJ to i, j
        double i_d = (double)i;
        double j_d = (double)j;
        double i_target = (double)lastTouchI;
        double j_target = (double)lastTouchJ;
        double r = sqrt((i_d - i_target) * (i_d - i_target) + (j_d - j_target) * (j_d - j_target));
        double delta_i = (i_target - i_d) / r;
        double delta_j = (j_target - j_d) / r;
        for (int k = 0; k <= round(r); k++) {
          int i_index = round(i_d + delta_i * k);
          int j_index = round(j_d + delta_j * k);
          // Set values in v to large values along the drag path
          v[toIndex(i_index, j_index)] = touchPolarity * MAX_RANGE >> 1;
        }
      }
      lastTouchI = i;
      lastTouchJ = j;
    } else {
      if (lastTouchI != -1 || lastTouchJ != -1) {
        touchPolarity = -touchPolarity;
      }
      lastTouchI = -1;
      lastTouchJ = -1;
    }
  }

  // First CPU-intensive loop: update values in v based on values in u given the wave equation:
  // d2u/dt2 = c * c * (d2x/dt2 + d2y/dt2) - k * du/dt
  // where d2u/dt2 is second partial time derivative, d2u/dx2 and d2u/dy2 are second partial space derivatives with respect to x and y,
  // c is constant wave speed through the medium (same for regions with NORMAL and ABSORBANT pixels, slower for areas with GLASS pixels),
  // and k is a damping constant close to zero except in regions with pixels of type IMEPEDANCE_PIXEL.
  for (int index = 0; index < HEIGHT * WIDTH; index++) {
    uint8_t pixelStatus = pixelType[index];
    if (pixelStatus == NORMAL_PIXEL || pixelStatus == ABSORBANT_PIXEL || pixelStatus == GLASS_PIXEL) {
      // Wave equation applies to normal, absorbant, and glass pixels
      int32_t uCen = u[index];
      int32_t uNorth = u[index - WIDTH];
      int32_t uSouth = u[index + WIDTH];
      int32_t uEast = u[index + 1];
      int32_t uWest = u[index - 1];
      int32_t uxx = ((uWest + uEast) >> 1) - uCen;
      int32_t uyy = ((uNorth + uSouth) >> 1) - uCen;
      int32_t vel = v[index] + (uxx >> 1) + (uyy >> 1);
      // Velocity is damped lightly for normal and glass pixels, heavily for absorbant pixels
      vel -= (vel >> (pixelStatus == ABSORBANT_PIXEL ? absorbantDampingBitShift : normalDampingBitShift));
      v[index] = applyCap(vel);
    }
  }

  // Second CPU-intensive loop: update each value in u based on its corresponding value in v given that v=du/dt, using one loop interval as dt,
  // except for SOURCE pixels where we set u to their current amplitude. Then, calculate a 16-bit color value for image, based on the value in u.
  int lowFrequencyAmplitude = (MAX_RANGE >> 1) * sin(0.5 * RADIANS_PER_ITERATION * loopCounter);
  int midFrequencyAmplitude = (MAX_RANGE >> 1) * sin(RADIANS_PER_ITERATION * loopCounter);
  int highFrequencyAmplitude = (MAX_RANGE >> 1) * sin(2 * RADIANS_PER_ITERATION * loopCounter);
  for (int index = 0; index < HEIGHT * WIDTH; index++) {
      uint8_t pixelStatus = pixelType[index];
      // WALL_PIXEL: can be skipped (u = 0, v = 0).
      if (pixelStatus == NORMAL_PIXEL || pixelStatus == ABSORBANT_PIXEL) {
        // NORMAL_PIXEL and ABSORBANT_PIXEL: update u by adding an amount proportional to v. (Using loop interval as dt, proportionality constant is 1.0.)
        u[index] = applyCap(u[index] + v[index]);
      } else if (pixelStatus == GLASS_PIXEL) {
        // GLASS_PIXEL: Using 2.0 as index of refraction for glass implies a wave speed of 0.5, so the proportionality constant is 0.25, obtainable by shifting right 2 bits.
        u[index] = applyCap(u[index] + (v[index] >> GLASS_REFRACTION_BIT_SHIFT));
      } else if (pixelType[index] == LOW_FREQ_POS_SOURCE_PIXEL) {
        u[index] = lowFrequencyAmplitude;
      } else if (pixelType[index] == LOW_FREQ_NEG_SOURCE_PIXEL) {
        u[index] = -lowFrequencyAmplitude;
      } else if (pixelType[index] == MID_FREQ_POS_SOURCE_PIXEL) {
        u[index] = midFrequencyAmplitude;
      } else if (pixelType[index] == MID_FREQ_NEG_SOURCE_PIXEL) {
        u[index] = -midFrequencyAmplitude;
      } else if (pixelType[index] == HIGH_FREQ_POS_SOURCE_PIXEL) {
        u[index] = highFrequencyAmplitude;
      } else if (pixelType[index] == HIGH_FREQ_NEG_SOURCE_PIXEL) {
        u[index] = -highFrequencyAmplitude;
      } else if (pixelType[index] == PHASED_ARRAY_SOURCE_PIXEL) {
        // PHASED_ARRAY_MODE has a horizontal line of phased array pixels; they introduce a sinusoidal dependence on index (spatial variable)
        u[index] = (MAX_RANGE >> 1) * sin( 0.5 * (RADIANS_PER_ITERATION * loopCounter - RADIANS_PER_PIXEL * index));
      }

      // Second part of loop body- select a 16-bit color to put in image array
      if (pixelStatus == WALL_PIXEL) {
        // Just use a light gray for WALL pixels (can use 0xffff for garish white)
        image[index] = 2048 | 64 | 2;
      } else {
        // Have to actually calculate a color for anything that isn't a WALL_PIXEL, based on its value in u
        bool isPositive = u[index] >= 0;
        uint16_t val = (uint16_t)((isPositive ? u[index] : -u[index]) >> 23);
        if (val > 63) {
          val = 63;
        }

        // Standard 16-bit RGB565-encoded color values (e.g. TFT_GREEN, etc.) produce bizarre colors when planted directly into image array
        // but this encoding seems to work
        int red = ((val & 0xf8) << 2);
        int green = val >> 3;
        int blue = ((val & 0xfc) << 7);

        switch(colorScale) {
            case RED_BLUE_SCALE:
              image[index] = isPositive ? red : blue;
              break;
            case YELLOW_PURPLE_SCALE:
              image[index] = isPositive ? red | green : red | blue;
              break;
            case RED_GREEN_SCALE:
              image[index] = isPositive ? red : green;
              break;
            case YELLOW_CYAN_SCALE:
              image[index] = isPositive ? red | green : green | blue;
              break;
            case BLUE_GREEN_SCALE:
              image[index] = isPositive ? green : blue;
              break;
            case CYAN_PURPLE_SCALE:
              image[index] = isPositive ? green | blue : red | blue;
              break;
        }

        // ABSORBANT_PIXEL and GLASS_PIXEL need some extra color bits set for visibility
        if (pixelStatus == ABSORBANT_PIXEL) {
          // For ABSORBANT_PIXEL increase red, green, blue saturation
          image[index] |= 1024 | 32 | 1;
        }
        if (pixelStatus == GLASS_PIXEL) {
          // For GLASS_PIXEL increase blue saturation only
          image[index] |= 2048;
        }
      }
  }

  loopCounter++;

  // Push the image array into the sprite
  sprite.pushImage(0, 0, WIDTH, HEIGHT, image);

  // Roughly calculate frames per second
  uint64_t new_timestamp = esp_timer_get_time();
  double duration = (double)((new_timestamp - timestamp) / 1000);
  uint8_t fps = round(1000 / duration);

  if (label.length() > 0) {
    // Draw strings on the sprite for label, current fps
    sprite.setTextSize(1);
    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.drawString(label, 0, 0, 2);

    if (timestamp > 0) {
      sprite.drawString(String(fps) + " fps", 280, 155, 2);

      uint64_t total_microseconds = new_timestamp - startTime;
      uint64_t microsec = total_microseconds % 1000000;
      uint64_t total_ms = floor((double)total_microseconds / 1000);
      uint64_t ms = total_ms % 1000;
      uint64_t total_sec = (total_ms - ms) / 1000;
      uint64_t sec = total_sec % 60;
      uint64_t total_min = (total_sec - sec) / 60;
      uint64_t min = total_min % 60;
      uint64_t total_hrs = (total_min - min) / 60;
      // Serial.println(String(total_microseconds) + "   " + String(total_ms) + "    " + String(total_sec) + "   " + sec + "   " + min);

      String seconds = String(sec);
      if(total_min < 1) {
        sprite.drawString(seconds, 0, 155, 2); 
      } else {
          while (seconds.length() < 2) {
            seconds = "0" + seconds;
          }
          String minutes = String(min);
          if (total_hrs < 1) {
            sprite.drawString(minutes + ":" + seconds, 0, 155, 2);
          } else {
              while (minutes.length() < 2) {
                minutes = "0" + minutes;
              }
            sprite.drawString(String(total_hrs) + ":" + minutes + ":" + seconds, 0, 155, 2);
          }
      }
      if (!touched && ((touchEnabled && mode == TOUCH_ONLY_MODE) || (!touchEnabled && mode == RANDOM_POINTS_MODE && total_sec < 10))) {
        sprite.setTextColor(TFT_RED, TFT_BLACK);
        sprite.drawString("WAVE EQUATION SIMULATOR", 70, 60, 2);
        sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
        sprite.drawString("https://github.com/jtiscione/TDisplayWave/", 25, 90, 2);
      }
    }
  }
  sprite.pushSprite(0, 0);
  timestamp = new_timestamp;

}