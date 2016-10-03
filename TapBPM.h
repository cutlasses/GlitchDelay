#pragma once

#include "Util.h"
#include "Interface.h"

class TAP_BPM
{
public:

  TAP_BPM( int button_pin );

  bool                      valid_bpm() const;
  float                     bpm() const;
  float                     beat_duration_ms() const;

  void                      setup();
  bool                      update( float time_ms );  // returns true on every beat (includes tempo taps)

private:
  
  BUTTON                    m_tap_button;
  RUNNING_AVERAGE<float, 4> m_average_times;

  float                     m_prev_tap_time_ms;
  float                     m_next_beat_time_ms;
};

