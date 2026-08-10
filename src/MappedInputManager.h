#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const { gpio.update(); }
  // Called on every activity transition. A screen that closes on a button press
  // leaves the matching release to land on whatever is revealed behind it, which
  // then acts on a press its user never aimed at it. Both Confirm and Back are
  // suppressed until the button is next seen up.
  void armReleaseGuards() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

 private:
  HalGPIO& gpio;
  mutable bool suppressConfirmReleaseUntilButtonUp = false;
  mutable bool suppressBackReleaseUntilButtonUp = false;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};
