#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <Bounce.h>     // Arduino compiler can get confused if you don't include include all required headers in this file?!?

//#include "AudioFreezeEffect.h"
#include "GlitchDelayInterface.h"
#include "CompileSwitches.h"
#include "Util.h"


AudioInputI2S            audio_input;
//AUDIO_FREEZE_EFFECT      audio_freeze_effect;
AudioMixer4              audio_mixer;
AudioEffectDelay         audio_delay;
AudioOutputI2S           audio_output;

AudioConnection          patch_cord_L1( audio_input, 0, audio_mixer, 0 );
AudioConnection          patch_cord_L2( audio_mixer, 0, audio_delay, 0 );
AudioConnection          patch_cord_L3( audio_delay, 0, audio_mixer, 1 );
AudioConnection          patch_cord_L4( audio_delay, 0, audio_output, 0 );
//AudioConnection          patch_cord_L1( audio_input, 0, audio_output, 0 );    // left channel passes straight through (for testing)
AudioConnection          patch_cord_R1( audio_input, 1, audio_output, 1 );      // right channel passes straight through
AudioControlSGTL5000     sgtl5000_1;

GLITCH_DELAY_INTERFACE   glitch_delay_interface;

//////////////////////////////////////

void setup()
{
  Serial.begin(9600);

  AudioMemory(120);
  
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.8f);

  sgtl5000_1.lineInLevel( 10 );  // 0.56volts p-p
  sgtl5000_1.lineOutLevel( 13 );  // 3.16volts p-p
  
  SPI.setMOSI(7);
  SPI.setSCK(14);

  glitch_delay_interface.setup();

  audio_delay.delay(0, 110);

  audio_mixer.gain( 0, 0.5f );
  audio_mixer.gain( 1, 0.25f );

  delay(1000);
  
#ifdef DEBUG_OUTPUT
  Serial.print("Setup finished\n");
#endif // DEBUG_OUTPUT
}

void loop()
{
  /*  
  audio_freeze_interface.update();

  if( audio_freeze_interface.freeze_button().active() != audio_freeze_effect.is_freeze_active() )
  {
    audio_freeze_effect.set_freeze( audio_freeze_interface.freeze_button().active() );
  }

  audio_freeze_effect.set_length( audio_freeze_interface.length_dial().value() );
  audio_freeze_effect.set_centre( audio_freeze_interface.position_dial().value() );
  audio_freeze_effect.set_speed( audio_freeze_interface.speed_dial().value() );

  if( audio_freeze_interface.mode() == 1 )
  {
    audio_freeze_effect.set_reverse( true );
  }
  else
  {
    audio_freeze_effect.set_reverse( false );
  }

  if( audio_freeze_interface.freeze_button().active() )
  {
    const float freeze_mix_amount = clamp( audio_freeze_interface.mix_dial().value(), 0.0f, 1.0f );
    
    audio_mixer.gain( MIX_FREEZE_CHANNEL, freeze_mix_amount );
    audio_mixer.gain( MIX_ORIGINAL_CHANNEL, 1.0f - freeze_mix_amount );
  }
  else
  {
    audio_mixer.gain( MIX_FREEZE_CHANNEL, 0.0f );
    audio_mixer.gain( MIX_ORIGINAL_CHANNEL, 1.0f );

    // only adjust bit-depth when freeze is not active, need to write the data in the new bit-depth before it can be played
    if( audio_freeze_interface.reduced_bit_depth() )
    {
      audio_freeze_effect.set_bit_depth( 8 );
    }
    else
    {
      audio_freeze_effect.set_bit_depth( 16 );
    }
    */
    
#ifdef DEBUG_OUTPUT
//  const int processor_usage = AudioProcessorUsage();
//  if( processor_usage > 30 )
//  {
//    Serial.print( "Performance spike: " );
//    Serial.print( processor_usage );
//    Serial.print( "\n" );
//  }
#endif
}




