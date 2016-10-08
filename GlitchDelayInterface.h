#pragma once

#include "Interface.h"
#include "TapBPM.h"

class GLITCH_DELAY_INTERFACE
{
  static const int      RANDOM_DIAL_PIN                 = 20;
  static const int      DELAY_DIAL_PIN                  = 17;
  static const int      FEEDBACK_DIAL_PIN               = 21;
  static const int      MIX_DIAL_PIN                    = 16;
  static const int      FREEZE_BUTTON_PIN               = 2;
  static const int      MODE_BUTTON_PIN                 = 1;
  static const int      LED_1_PIN                       = 4;
  static const int      LED_2_PIN                       = 3;
  static const int      LED_3_PIN                       = 5;

  static const bool     FREEZE_BUTTON_IS_TOGGLE         = true;
  static const int      NUM_LEDS                        = 3;
 
  static const int32_t  BIT_DEPTH_BUTTON_HOLD_TIME_MS   = 2000;
  
  DIAL              m_random_dial;
  DIAL              m_delay_dial;
  DIAL              m_feedback_dial;
  DIAL              m_mix_dial;

  BUTTON            m_freeze_button;
  BUTTON            m_mode_button;
  TAP_BPM           m_tap_bpm;        // same button as mode
  
  LED               m_beat_led;
  LED               m_glitch_led;
  LED               m_bit_depth_led;

  int               m_current_mode;
  bool              m_change_bit_depth_valid;
  bool              m_reduced_bit_depth;

public:

  GLITCH_DELAY_INTERFACE();

  void            setup();
  void            update( uint32_t time_in_ms );

  const DIAL&     random_dial() const;
  const DIAL&     delay_dial() const;
  const DIAL&     feedback_dial() const;
  const DIAL&     mix_dial() const;
  const BUTTON&   freeze_button() const;
  const TAP_BPM&  tap_bpm() const;
  
  LED&            glitch_led();

  int             mode() const;
  bool            reduced_bit_depth() const;
};

