#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

#include <Math.h>
#include "GlitchDelayEffect.h"
#include "CompileSwitches.h"


const float MIN_SPEED( 0.25f );
const float MAX_SPEED( 4.0f );


/////////////////////////////////////////////////////////////////////

int delay_buffer_size_in_samples( int sample_size_in_bits )
{
  const int bytes_per_sample = sample_size_in_bits / 8;
  return DELAY_BUFFER_SIZE_IN_BYTES / bytes_per_sample;
}

int convert_time_in_ms_to_samples( int time_in_ms )
{
  static int num_samples_per_ms = AUDIO_SAMPLE_RATE / 1000;
  return num_samples_per_ms * time_in_ms; 
}

/////////////////////////////////////////////////////////////////////

PLAY_HEAD::PLAY_HEAD( const GLITCH_DELAY_EFFECT& delay_buffer ) :
  m_delay_buffer( delay_buffer ),
  m_current_offset( 0 ),
  m_destination_offset( 0 ),
  m_fade_window_size_in_samples( 0 ),
  m_fade_samples_remaining( 0 )
{
  
}

int PLAY_HEAD::calculate_play_head( int write_head, int offset ) const
{
  ASSERT_MSG( offset >= 0 && offset < m_delay_buffer.m_buffer_size_in_samples - 1, "PLAY_HEAD::calculate_play_head()" );
  
  int play_head = write_head - offset;
  
  if( play_head < 0 )
  {
    play_head = m_delay_buffer.m_buffer_size_in_samples + play_head;
  }

  ASSERT_MSG( play_head >= 0 && play_head < m_delay_buffer.m_buffer_size_in_samples, "PLAY_HEAD::calculate_play_head()" );
  return play_head; 
}

int16_t PLAY_HEAD::read_sample_with_cross_fade( int write_head )
{
  if( m_fade_samples_remaining == 0 )
  {
    int play_head = calculate_play_head( write_head, m_current_offset );
    return m_delay_buffer.read_sample( play_head );
  }
  else
  {
    int current_play_head       = calculate_play_head( write_head, m_current_offset );
    int16_t current_sample      = m_delay_buffer.read_sample( current_play_head );

    int dest_play_head          = calculate_play_head( write_head, m_destination_offset );
    int16_t destination_sample  = m_delay_buffer.read_sample( dest_play_head );

    const float t               = 0.0f;
    return lerp( current_sample, destination_sample, t );
  }
}

void PLAY_HEAD::set_play_head( int offset_from_write_head )
{
  if( offset_from_write_head != m_current_offset )
  {
    // NOTE : need to deal with setting playhead whilst still interpolating
    m_destination_offset          = offset_from_write_head;
  
    static int fade_rate          = AUDIO_SAMPLE_RATE * 4;
    int distance                  = abs( m_destination_offset - m_current_offset );
    m_fade_window_size_in_samples = distance / fade_rate;
    m_fade_samples_remaining      = 0;
    m_current_offset              = offset_from_write_head;
  }
}

void PLAY_HEAD::read_from_play_head( int16_t* dest, int size, int write_head )
{
  for( int x = 0; x < size; ++x )
  {
    dest[x] = read_sample_with_cross_fade( write_head + x );
  }
}

/////////////////////////////////////////////////////////////////////

GLITCH_DELAY_EFFECT::GLITCH_DELAY_EFFECT() :
  AudioStream( 1, m_input_queue_array ),
  m_buffer(),
  m_input_queue_array(),
  m_write_head(0),
  m_play_head(*this),
  m_freeze_loop_start(0),
  m_freeze_loop_end(0),
  m_sample_size_in_bits(16),
  m_buffer_size_in_samples( delay_buffer_size_in_samples( 16 ) ),
  m_freeze_active(false),
  m_next_sample_size_in_bits(16),
  m_next_play_head_offset_in_samples(1)
{
  memset( m_buffer, 0, sizeof(m_buffer) );
}

void GLITCH_DELAY_EFFECT::write_sample( int16_t sample, int index )
{
  ASSERT_MSG( index >= 0 && index < m_buffer_size_in_samples, "GLITCH_DELAY_EFFECT::write_sample() writing outside buffer" );

  switch( m_sample_size_in_bits )
  {
    case 8:
    {
      int8_t sample8                      = (sample >> 8) & 0xff;
      int8_t* sample_buffer               = reinterpret_cast<int8_t*>(m_buffer);
      sample_buffer[ index ]              = sample8;
      break;
    }
    case 16:
    {
      int16_t* sample_buffer             = reinterpret_cast<int16_t*>(m_buffer);
      sample_buffer[ index ]             = sample;
      break;
    }
  }
}

int16_t GLITCH_DELAY_EFFECT::read_sample( int index ) const
{
  ASSERT_MSG( index >= 0 && index < m_buffer_size_in_samples, "GLITCH_DELAY_EFFECT::read_sample() writing outside buffer" );
 
  switch( m_sample_size_in_bits )
  {
    case 8:
    {
         const int8_t* sample_buffer    = reinterpret_cast<const int8_t*>(m_buffer);
        const int8_t sample            = sample_buffer[ index ];

        int16_t sample16               = sample;
        sample16                       <<= 8;

        return sample16;
    }
    case 16:
    {
        const int16_t* sample_buffer    = reinterpret_cast<const int16_t*>(m_buffer);
        const int16_t sample            = sample_buffer[ index ];
        return sample;
    }
  }

  return 0;
}

void GLITCH_DELAY_EFFECT::write_to_buffer( const int16_t* source, int size )
{
  ASSERT_MSG( m_write_head >= 0 && m_write_head < m_buffer_size_in_samples, "GLITCH_DELAY_EFFECT::write_to_buffer()" );
  
  for( int x = 0; x < size; ++x )
  {
    write_sample( source[x], m_write_head );

    if( ++m_write_head >= m_buffer_size_in_samples )
    {
      m_write_head            = 0;
    }
  }
}

/*
int GLITCH_DELAY_EFFECT::read_from_buffer( int16_t* dest, int size, int play_head, int buffer_start, int buffer_end )
{
  ASSERT_MSG( play_head >= 0 && play_head < m_buffer_size_in_samples, "GLITCH_DELAY_EFFECT::read_from_buffer()" );
 
  for( int x = 0; x < size; ++x )
  {
    dest[x]                   = read_sample( play_head );

    // deal with end being behind start due to wraparound
    if( buffer_start > buffer_end )
    {
      buffer_end              = m_buffer_size_in_samples - 1;
    }
    
    if( ++play_head > buffer_end )
    {
      play_head               = buffer_start;
    } 
  }

  return play_head;
}
*/

bool GLITCH_DELAY_EFFECT::freeze_active() const
{
  return m_freeze_active;
}

void GLITCH_DELAY_EFFECT::update()
{      
  set_bit_depth_impl( m_next_sample_size_in_bits);
  m_play_head.set_play_head( m_next_play_head_offset_in_samples );

  ASSERT_MSG( m_write_head >= 0 && m_write_head < m_buffer_size_in_samples, "GLITCH_DELAY_EFFECT::update()" );

  /*
  if( m_freeze_active )
  {
    audio_block_t* block        = allocate();

    m_write_head                = read_from_buffer( block->data, AUDIO_BLOCK_SAMPLES, m_write_head, m_freeze_loop_start, m_freeze_loop_end );

    transmit( block, 0 );
    
    release( block );
  }
  else*/
  {
    audio_block_t* block        = receiveWritable();
  
    if( block != nullptr )
    {
      const int prev_write_head = m_write_head;
      write_to_buffer( block->data, AUDIO_BLOCK_SAMPLES );
    
      //read_from_buffer( block->data, AUDIO_BLOCK_SAMPLES, play_head, 0, m_buffer_size_in_samples - 1 );
      m_play_head.read_from_play_head( block->data, AUDIO_BLOCK_SAMPLES, prev_write_head );
    
      transmit( block, 0 );
    
      release( block );
    }
  }
}

void GLITCH_DELAY_EFFECT::set_bit_depth_impl( int sample_size_in_bits )
{
  if( sample_size_in_bits != m_sample_size_in_bits )
  {
      m_sample_size_in_bits       = sample_size_in_bits;
      m_buffer_size_in_samples    = delay_buffer_size_in_samples( m_sample_size_in_bits );
      
      m_write_head                = 0;
    
      memset( m_buffer, 0, sizeof(m_buffer) );

#ifdef DEBUG_OUTPUT
      Serial.print("Set bit depth:");
      Serial.print( m_sample_size_in_bits );
      Serial.print("\n");
#endif
  }
}

void GLITCH_DELAY_EFFECT::set_freeze_impl( bool active, int loop_size_in_ms )
{
  m_freeze_active           = active;

  if( active )
  {
    int loop_size_in_samples  = convert_time_in_ms_to_samples( loop_size_in_ms );
    m_freeze_loop_start       = m_write_head;
    m_freeze_loop_end         = ( m_write_head + loop_size_in_samples ) % m_buffer_size_in_samples;
  
    m_write_head              = 0;   // use the write head as the play head when frozen
  }
}

void GLITCH_DELAY_EFFECT::set_delay_time_in_ms( int time_in_ms )
{
  int offset_in_samples   = convert_time_in_ms_to_samples( time_in_ms );

  if( offset_in_samples > m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES )
  {
    offset_in_samples = m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES;
  }

  m_next_play_head_offset_in_samples = offset_in_samples;
  ASSERT_MSG( m_next_play_head_offset_in_samples >= 0 && m_next_play_head_offset_in_samples <= m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES, "GLITCH_DELAY_EFFECT::set_delay_time()" );  
}

void GLITCH_DELAY_EFFECT::set_delay_time_as_ratio( float ratio_of_max_delay )
{
  ASSERT_MSG( ratio_of_max_delay >= 0.0f && ratio_of_max_delay <= 1.0f, "GLITCH_DELAY_EFFECT::set_delay_time()" );

  // quantize to 32 steps to avoid small fluctuations
  ratio_of_max_delay = ( static_cast<int>( ratio_of_max_delay * 32.0f ) ) / 32.0f;
  
  m_next_play_head_offset_in_samples = trunc_to_int( ratio_of_max_delay * ( m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES ) );
  ASSERT_MSG( m_next_play_head_offset_in_samples >= 0 && m_next_play_head_offset_in_samples <= m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES, "GLITCH_DELAY_EFFECT::set_delay_time()" );
}

void GLITCH_DELAY_EFFECT::set_bit_depth( int sample_size_in_bits )
{
  m_next_sample_size_in_bits = sample_size_in_bits;
  //set_bit_depth_impl( sample_size_in_bits );
}

void GLITCH_DELAY_EFFECT::set_freeze( bool active, int loop_size_in_ms )
{
  set_freeze_impl( active, loop_size_in_ms );  
}

