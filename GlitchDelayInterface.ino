#include "GlitchDelayInterface.h"
#include "CompileSwitches.h"
  
GLITCH_DELAY_INTERFACE::GLITCH_DELAY_INTERFACE() :
  m_length_dial( LENGTH_DIAL_PIN ),
  m_delay_dial( DELAY_DIAL_PIN ),
  m_feedback_dial( FEEDBACK_DIAL_PIN ),
  m_mix_dial( MIX_DIAL_PIN ),
  m_freeze_button( FREEZE_BUTTON_PIN, FREEZE_BUTTON_IS_TOGGLE ),
  m_mode_button( MODE_BUTTON_PIN, false ),
  m_tap_bpm( MODE_BUTTON_PIN ),
  m_leds(),
  m_current_mode( 0 ),
  m_change_bit_depth_valid( true ),
  m_reduced_bit_depth( false )
{
  m_leds[0] = LED( LED_1_PIN );
  m_leds[1] = LED( LED_2_PIN );
  m_leds[2] = LED( LED_3_PIN ); 
}

void GLITCH_DELAY_INTERFACE::setup()
{
  m_freeze_button.setup();
  m_mode_button.setup();

  for( int x = 0; x < NUM_LEDS; ++x )
  {
    m_leds[x].setup();
    m_leds[x].set_brightness( 0.25f );
  }
}

void GLITCH_DELAY_INTERFACE::update()
{
  uint32_t time_in_ms = millis();
  
  m_length_dial.update() ;
  m_delay_dial.update();
  m_feedback_dial.update();
  m_mix_dial.update();
  
  m_freeze_button.update( time_in_ms );
  m_mode_button.update( time_in_ms );

  m_tap_bpm.update( time_in_ms );

  LED& beat_led = m_leds[0];
  
  if( m_tap_bpm.beat_type() != TAP_BPM::NO_BEAT )
  {
#ifdef DEBUG_OUTPUT
    Serial.print("Beat!\n");
#endif // DEBUG_OUTPUT

      beat_led.flash_on( time_in_ms, 100 );
  }

  if( m_mode_button.down_time_ms() > BIT_DEPTH_BUTTON_HOLD_TIME_MS && m_change_bit_depth_valid )
  {
    m_reduced_bit_depth = !m_reduced_bit_depth;

    // don't allow the mode to change until button is released
    m_change_bit_depth_valid = false;
  }

  if( !m_change_bit_depth_valid && !m_mode_button.active() )
  {
    // once the mode button has been released, we can change the mode again
    m_change_bit_depth_valid = true;
  }

  beat_led.update( time_in_ms );

  // update bit depth led
  LED& bit_depth_led = m_leds[ NUM_LEDS - 1 ];
  if( m_reduced_bit_depth )
  {
    bit_depth_led.set_active( true );
  }
  else
  {
    bit_depth_led.set_active( false );
  }
  bit_depth_led.update( time_in_ms );

#ifdef DEBUG_OUTPUT
  /*
  if( m_speed_dial.update() )
  {
    Serial.print("Speed ");
    Serial.print(m_speed_dial.value());
    Serial.print("\n");
  }
  if( m_mix_dial.update() )
  {
    Serial.print("Mix ");
    Serial.print(m_mix_dial.value());
    Serial.print("\n");   
  }
  if( m_length_dial.update() )
  {
    Serial.print("Length ");
    Serial.print(m_length_dial.value());
    Serial.print("\n");
  }
  if( m_position_dial.update() )
  {
    Serial.print("Position ");
    Serial.print(m_position_dial.value());
    Serial.print("\n");   
  }
  m_freeze_button.update();

  if( m_freeze_button.active() )
  {
    Serial.print("on\n");
  }
  */
#endif // DEBUG_OUTPUT
}

const DIAL& GLITCH_DELAY_INTERFACE::length_dial() const
{
  return m_length_dial;
}

const DIAL& GLITCH_DELAY_INTERFACE::delay_dial() const
{
  return m_delay_dial;
}

const DIAL& GLITCH_DELAY_INTERFACE::feedback_dial() const
{
  return m_feedback_dial;
}

const DIAL& GLITCH_DELAY_INTERFACE::mix_dial() const
{
  return m_mix_dial;
}

const BUTTON& GLITCH_DELAY_INTERFACE::freeze_button() const
{
  return m_freeze_button;
}

int GLITCH_DELAY_INTERFACE::mode() const
{
  return m_current_mode;
}

bool GLITCH_DELAY_INTERFACE::reduced_bit_depth() const
{
  return m_reduced_bit_depth;
}

