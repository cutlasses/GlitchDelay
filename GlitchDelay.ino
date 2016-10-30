#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <Bounce.h>     // Arduino compiler can get confused if you don't include include all required headers in this file?!?

#include "CompileSwitches.h"
#include "GlitchDelayEffect.h"
#include "GlitchDelayInterface.h"
#include "TapBPM.h"
#include "Util.h"


// Use these with the audio adaptor board
#define SDCARD_CS_PIN    10
#define SDCARD_MOSI_PIN  7
#define SDCARD_SCK_PIN   14

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

#ifdef STANDALONE_AUDIO
AudioPlaySdRaw           raw_player;
AudioConnection          patch_cord_L1( raw_player, 0, delay_mixer, 0 );
//AudioConnection          patch_cord_L2( delay_mixer, 0, audio_delay, 0 );
AudioConnection          patch_cord_L2( delay_mixer, 0, glitch_delay_effect, 0 );
AudioConnection          patch_cord_L3( glitch_delay_effect, 0, delay_mixer, FEEDBACK_CHANNEL );
AudioConnection          patch_cord_L4( glitch_delay_effect, 0, wet_dry_mixer, WET_CHANNEL );
AudioConnection          patch_cord_L5( raw_player, 0, wet_dry_mixer, DRY_CHANNEL );
AudioConnection          patch_cord_L6( wet_dry_mixer, 0, audio_output, 0 );
//AudioConnection          patch_cord_L1( audio_input, 0, audio_output, 0 );    // left channel passes straight through (for testing)
AudioConnection          patch_cord_R1( audio_input, 1, audio_output, 1 );      // right channel passes straight through
#else // STANDALONE_AUDIO
AudioConnection          patch_cord_L1( audio_input, 0, delay_mixer, 0 );
//AudioConnection          patch_cord_L2( delay_mixer, 0, audio_delay, 0 );
AudioConnection          patch_cord_L2( delay_mixer, 0, glitch_delay_effect, 0 );
AudioConnection          patch_cord_L3( glitch_delay_effect, 0, delay_mixer, FEEDBACK_CHANNEL );
AudioConnection          patch_cord_L4( glitch_delay_effect, 0, wet_dry_mixer, WET_CHANNEL );
AudioConnection          patch_cord_L5( audio_input, 0, wet_dry_mixer, DRY_CHANNEL );
AudioConnection          patch_cord_L6( wet_dry_mixer, 0, audio_output, 0 );
//AudioConnection          patch_cord_L1( audio_input, 0, audio_output, 0 );    // left channel passes straight through (for testing)
AudioConnection          patch_cord_R1( audio_input, 1, audio_output, 1 );      // right channel passes straight through
#endif // !STANDALONE_AUDIO


AudioControlSGTL5000     sgtl5000_1;

GLITCH_DELAY_INTERFACE   glitch_delay_interface;


//////////////////////////////////////

void setup()
{

  Serial.begin(9600);

#ifdef DEBUG_OUTPUT
  Serial.print("Setup started!\n");
#endif // DEBUG_OUTPUT

  AudioMemory(16);
  
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.8f);

  sgtl5000_1.lineInLevel( 10 );  // 0.56volts p-p
  sgtl5000_1.lineOutLevel( 13 );  // 3.16volts p-p
  
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);

#ifdef STANDALONE_AUDIO
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here, but print a message repetitively
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
#endif // STANDALONE_AUDIO

  glitch_delay_interface.setup();

  //audio_delay.delay(0, 300);

  wet_dry_mixer.gain( DRY_CHANNEL, 0.5f );
  wet_dry_mixer.gain( WET_CHANNEL, 0.5f );

  delay_mixer.gain( 0, 0.5f );
  delay_mixer.gain( 1, 0.25f );
  
#ifdef DEBUG_OUTPUT
  Serial.print("Setup finished!\n");
#endif // DEBUG_OUTPUT
}

void loop()
{
  uint32_t time_in_ms = millis();

#ifdef STANDALONE_AUDIO
  if( !raw_player.isPlaying() )
  {
    raw_player.play( "GUITAR.RAW" ); 
    //raw_player.play( "DRUMLOOP.RAW" );
  }
#endif


  const bool valid_bpm = glitch_delay_interface.tap_bpm().valid_bpm();
  glitch_delay_interface.update( time_in_ms );

  const int beat_duration   = valid_bpm ? glitch_delay_interface.tap_bpm().beat_duration_ms() : 1.0f;

  if( glitch_delay_effect.can_start_glitch() && glitch_delay_interface.tap_bpm().beat_type() == TAP_BPM::AUTO_BEAT )
  {
    // update random glitch
    const float randomness      = glitch_delay_interface.random_dial().value();
    const float r               = random( 100 ) / 100.0f; // max dial -> glitch 100% of the time
    if( r < randomness )
    {
      const int glitch_duration = (beat_duration * 4);
 
      glitch_delay_effect.activate_glitch( glitch_duration );
      
      glitch_delay_interface.glitch_led().flash_on( time_in_ms, glitch_duration );
    }
  }

  if( valid_bpm )
  {
    glitch_delay_effect.set_delay_time_in_ms( glitch_delay_interface.tap_bpm().beat_duration_ms() * 0.25f );    
  }
  else
  {
    const float delay = clamp( glitch_delay_interface.delay_dial().value(), 0.0f, 1.0f );
    glitch_delay_effect.set_delay_time_as_ratio( delay );
  }
  
  const float wet_dry = clamp( glitch_delay_interface.mix_dial().value(), 0.0f, 1.0f );
  wet_dry_mixer.gain( DRY_CHANNEL, 1.0f - wet_dry );
  wet_dry_mixer.gain( WET_CHANNEL, wet_dry );
  
  const float feedback = glitch_delay_interface.feedback_dial().value();
  delay_mixer.gain( FEEDBACK_CHANNEL, feedback * MAX_FEEDBACK );
  delay_mixer.gain( FEEDBACK_CHANNEL, 0.0f );

  if( glitch_delay_interface.reduced_bit_depth() )
  {
    glitch_delay_effect.set_bit_depth( 8 );
  }
  else
  {
    glitch_delay_effect.set_bit_depth( 16 );
  }


  // TEST CASE
/*
  static uint32_t next_update = 5000;
  glitch_delay_interface.update( time_in_ms );

  if( time_in_ms > next_update )
  {
    next_update = time_in_ms + 5000;
    
    if( glitch_delay_effect.can_start_glitch() )
    {
      const int glitch_duration = 3000;
      
      glitch_delay_effect.activate_glitch( glitch_duration );
      
      glitch_delay_interface.glitch_led().flash_on( time_in_ms, glitch_duration );
    }
  }
  */

  delay_mixer.gain( FEEDBACK_CHANNEL, 0.0f );
  wet_dry_mixer.gain( DRY_CHANNEL, 0.0f );
  wet_dry_mixer.gain( WET_CHANNEL, 1.0f );


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
    
#ifdef PERF_CHECK
  const int processor_usage = AudioProcessorUsage();
  if( processor_usage > 85 )
  {
    Serial.print( "Performance spike: " );
    Serial.print( processor_usage );
    Serial.print( "\n" );
  }
#endif
}




