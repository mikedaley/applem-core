#pragma once

#include <cstdint>
#include <functional>

namespace a2e {

/**
 * Keyboard - Handles keyboard input translation for Apple IIe
 *
 * Translates raw browser keycodes to Apple II ASCII codes,
 * handling shift, control, and caps lock modifiers.
 */
class Keyboard {
public:
  // Callback type for sending translated keys to emulator
  using KeyCallback = std::function<void(int)>;

  Keyboard();

  /**
   * Set callback for when a key is translated
   * @param callback Function to call with translated Apple II keycode
   */
  void setKeyCallback(KeyCallback callback) { keyCallback_ = std::move(callback); }

  /**
   * Handle a raw key down event from the browser
   * @param browserKeycode The browser's keycode (e.g., 65 for 'A')
   * @param shift Shift key is pressed
   * @param ctrl Control key is pressed
   * @param alt Alt/Option key is pressed (Open Apple)
   * @param meta Meta/Command key is pressed (Closed Apple)
   * @param capsLock Caps Lock is active
   * @return The translated Apple II keycode, or -1 if not mapped
   */
  int handleKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                    bool meta, bool capsLock);

  /**
   * Handle a raw key up event from the browser
   * @param browserKeycode The browser's keycode
   * @param shift Shift key is pressed
   * @param ctrl Control key is pressed
   * @param alt Alt/Option key is pressed
   * @param meta Meta/Command key is pressed
   */
  void handleKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                   bool meta);

  /**
   * Get the current Open Apple (Alt) button state
   */
  bool isOpenApplePressed() const { return openApplePressed_; }

  /**
   * Get the current Closed Apple (Meta) button state
   */
  bool isClosedApplePressed() const { return closedApplePressed_; }

private:
  /**
   * Translate a browser keycode to base Apple II ASCII
   * @param browserKeycode The browser's keycode
   * @return Base ASCII code, or -1 if not mapped
   */
  int translateKeycode(int browserKeycode) const;

  /**
   * Apply shift modifier to a keycode
   * @param browserKeycode Original browser keycode
   * @param baseKey Base ASCII code
   * @return Shifted ASCII code
   */
  int applyShift(int browserKeycode, int baseKey) const;

  /**
   * Apply control modifier to a keycode
   * @param key ASCII code (should be a-z or A-Z)
   * @return Control character (0x01-0x1A)
   */
  int applyControl(int key) const;

  KeyCallback keyCallback_;
  bool openApplePressed_ = false;
  bool closedApplePressed_ = false;
};

} // namespace a2e
