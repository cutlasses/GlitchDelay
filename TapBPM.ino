#include "TapBPM.h"


TAP_BPM::TAP_BPM( int button_pin ) :
  m_tap_button( button_pin, false ),
  m_average_times(),
  m_prev_tap_time_ms(-1.0f)
{
  
}

bool TAP_BPM::valid_bpm() const
{
  return m_average_times.size() >= 3;
}

float TAP_BPM::bpm() const
{
  return 0.0f;
}

void TAP_BPM::setup()
{
  m_tap_button.setup();
}

void TAP_BPM::update( float time_ms )
{
  m_tap_button.update( time_ms );

  if( m_tap_button.active() )
  {
    if( m_prev_tap_time_ms > 0.0f )
    {
      float duration = time_ms - m_prev_tap_time_ms;

      m_average_times.add( duration );
    }
    else
    {
      m_prev_tap_time_ms = time_ms;
    }
  }
}

