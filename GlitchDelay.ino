#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <Bounce.h>     // Arduino compiler can get confused if you don't include include all required headers in this file?!?

#include "GlitchDelayEffect.h"
#include "GlitchDelayInterface.h"
#include "CompileSwitches.h"
#include "Util.h"


AudioInputI2S            audio_input;
GLITCH_DELAY_EFFECT      glitch_delay_effect;
AudioMixer4              delay_mixer;
AudioMixer4              wet_dry_mixer;
//AudioEffectDelay         audio_delay;
AudioOutputI2S           audio_output;

const int DRY_CHANNEL( 0 );
const int WET_CHANNEL( 1 );
const int FEEDBACK_CHANNEL( 1 );

const float MAX_FEEDBACK( 0.75f );

AudioConnection          patch_cord_L1( audio_input, 0, delay_mixer, 0 );
//AudioConnection          patch_cord_L2( delay_mixer, 0, audio_delay, 0 );
AudioConnection          patch_cord_L2( delay_mixer, 0, glitch_delay_effect, 0 );
AudioConnection          patch_cord_L3( glitch_delay_effect, 0, delay_mixer, FEEDBACK_CHANNEL );
AudioConnection          patch_cord_L4( glitch_delay_effect, 0, wet_dry_mixer, WET_CHANNEL );
AudioConnection          patch_cord_L5( audio_input, 0, wet_dry_mixer, DRY_CHANNEL );
AudioConnection          patch_cord_L6( wet_dry_mixer, 0, audio_output, 0 );
//AudioConnection          patch_cord_L1( audio_input, 0, audio_output, 0 );    // left channel passes straight through (for testing)
AudioConnection          patch_cord_R1( audio_input, 1, audio_output, 1 );      // right channel passes straight through
AudioControlSGTL5000     sgtl5000_1;

GLITCH_DELAY_INTERFACE   glitch_delay_interface;

//////////////////////////////////////

void setup()
{
  Serial.begin(9600);

  AudioMemory(16);
  
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.8f);

  sgtl5000_1.lineInLevel( 10 );  // 0.56volts p-p
  sgtl5000_1.lineOutLevel( 13 );  // 3.16volts p-p
  
  SPI.setMOSI(7);
  SPI.setSCK(14);

  glitch_delay_interface.setup();

  //audio_delay.delay(0, 300);

  wet_dry_mixer.gain( DRY_CHANNEL, 0.5f );
  wet_dry_mixer.gain( WET_CHANNEL, 0.5f );

  delay_mixer.gain( 0, 0.5f );
  delay_mixer.gain( 1, 0.25f );

  delay(1000);
  
#ifdef DEBUG_OUTPUT
  Serial.print("Setup finished!\n");
#endif // DEBUG_OUTPUT
}

void loop()
{
  glitch_delay_interface.update();

  if( glitch_delay_interface.freeze_button().active() != glitch_delay_effect.freeze_active() )
  {
    glitch_delay_effect.set_freeze( glitch_delay_interface.freeze_button().active() );
  }

  const float delay = clamp( glitch_delay_interface.delay_dial().value(), 0.0f, 1.0f );
  glitch_delay_effect.set_delay_time( delay );
  //glitch_delay_effect.set_delay_time( 0.0f );

  const float wet_dry = clamp( glitch_delay_interface.mix_dial().value(), 0.0f, 1.0f );
  wet_dry_mixer.gain( DRY_CHANNEL, 1.0f - wet_dry );
  wet_dry_mixer.gain( WET_CHANNEL, wet_dry );
  
  const float feedback = glitch_delay_interface.feedback_dial().value();
  delay_mixer.gain( FEEDBACK_CHANNEL, feedback * MAX_FEEDBACK );

#ifdef DEBUG_OUTPUT
/*
  static int count = 0;
  if( ++count % 1000 == 0 )
  {
    Serial.print("delay ");
    Serial.print(delay);
    Serial.print("\n");
  
    Serial.print("mix ");
    Serial.print(wet_dry);
    Serial.print("\n");
  
    Serial.print("feedback ");
    Serial.print(feedback * MAX_FEEDBACK);
    Serial.print("\n");
  
    Serial.print("****\n");
  }
*/
     
#endif // DEBUG_OUTPUT
    
#ifdef DEBUG_OUTPUT
  const int processor_usage = AudioProcessorUsage();
  if( processor_usage > 30 )
  {
    Serial.print( "Performance spike: " );
    Serial.print( processor_usage );
    Serial.print( "\n" );
  }
#endif
}




