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

int cross_fade_samples( int x, int y, float t )
{
  /*
  t = (t * 2.0f) - 1.0f;

  float fx = x;
  float fy = y;

  return sqrt( 0.5f * (fx + t) ) + sqrt( 0.5f * (fy -t ) );
  */
  /*
  float a = t * M_PI * 0.5f;
  float ax = cos(a);
  float ay = sin(a);

  Serial.print("cf a:");
  Serial.print(a);
  Serial.print(" ax:");
  Serial.print(ax);
  Serial.print(" ay:");
  Serial.print(ay);
  Serial.print("\n");

  return (ax * x) + (ay * y);
  */

  return round( lerp( x, y, t ) );
}

/////////////////////////////////////////////////////////////////////

PLAY_HEAD::PLAY_HEAD( const DELAY_BUFFER& delay_buffer ) :
  m_delay_buffer( delay_buffer ),
  m_current_play_head( 0 ),
  m_destination_play_head( 0 ),
  m_fade_window_size_in_samples( 0 ),
  m_fade_samples_remaining( 0 ),
  m_loop_start( -1 ),
  m_loop_end( -1 )
{
  
}

int PLAY_HEAD::current_position() const
{
  return m_current_play_head;
}

int PLAY_HEAD::destination_position() const
{
  return m_destination_play_head;
}

int16_t PLAY_HEAD::read_sample_with_cross_fade()
{
  ASSERT_MSG( m_fade_samples_remaining >= 0, "PLAY_HEAD::read_sample_with_cross_fade()" );

  int16_t sample(0);

  // not cross-fading
  if( m_fade_samples_remaining == 0 )
  {
    m_current_play_head         = m_destination_play_head;
    sample                      = m_delay_buffer.read_sample( m_current_play_head );

    m_delay_buffer.increment_head( m_current_play_head );
    m_destination_play_head     = m_current_play_head;
  }
  // cross-fading
  else
  {
    int16_t current_sample      = m_delay_buffer.read_sample( m_current_play_head );

     int16_t destination_sample  = m_delay_buffer.read_sample( m_destination_play_head );

    const float t               = static_cast<float>(m_fade_samples_remaining) / m_fade_window_size_in_samples; // t=0 at destination, t=1 at current
    --m_fade_samples_remaining;

    sample                      = cross_fade_samples( destination_sample, current_sample, t );

    m_delay_buffer.increment_head( m_current_play_head );
    m_delay_buffer.increment_head( m_destination_play_head );
  }

  return sample;
}

void PLAY_HEAD::set_play_head( int new_play_head )
{
  // already at this offset (or currently fading to it)
  if( new_play_head == m_destination_play_head )
  {  
    return;
  }

  // currently cross fading
  if( m_current_play_head != m_destination_play_head )
  {
    return;
  }
    
  m_destination_play_head       = new_play_head;

  static int FIXED_FADE_TIME    = (AUDIO_SAMPLE_RATE / 1000.0f ) * 5; // 5ms cross fade

  m_fade_window_size_in_samples = FIXED_FADE_TIME;
  m_fade_samples_remaining      = m_fade_window_size_in_samples;

/*
#ifdef DEBUG_OUTPUT
  Serial.print("PLAY_HEAD::set_play_head() fade window size:");
  Serial.print(m_fade_window_size_in_samples);
  Serial.print("\n");
#endif
*/
}

void PLAY_HEAD::read_from_play_head( int16_t* dest, int size )
{
  for( int x = 0; x < size; ++x )
  {
    if( m_loop_end >= 0 && m_current_play_head > m_loop_end )
    {
      ASSERT_MSG( m_loop_start >= 0, "PLAY_HEAD::read_from_play_head() invalid loop start" );
      set_play_head( m_loop_start );  
    }
    
    dest[x] = read_sample_with_cross_fade();
  }
}

void PLAY_HEAD::enable_loop( int start, int end )
{
  m_loop_start  = start;
  m_loop_end    = end;
}

void PLAY_HEAD::disable_loop()
{
  set_play_head( m_loop_start );
  
  m_loop_start            = -1;
  m_loop_end              = -1;
}

/////////////////////////////////////////////////////////////////////

DELAY_BUFFER::DELAY_BUFFER() :
  m_buffer(),
  m_buffer_size_in_samples(0),
  m_sample_size_in_bits(0),
  m_write_head(0)
{
  set_bit_depth( 16 );
}

int DELAY_BUFFER::position_offset_from_head( int offset ) const
{
  ASSERT_MSG( offset >= 0 && offset < m_buffer_size_in_samples - 1, "DELAY_BUFFER::position_offset_from_head()" );
  
  int position = m_write_head - offset;
  
  if( position < 0 )
  {
    position = m_buffer_size_in_samples + position;
  }

  ASSERT_MSG( position >= 0 && position < m_buffer_size_in_samples, "DELAY_BUFFER::position_offset_from_head()" );
  return position;   
}

int DELAY_BUFFER::delay_offset_from_ratio( float ratio_of_max_delay ) const
{
  int offset = trunc_to_int( ratio_of_max_delay * ( m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES ) );
  ASSERT_MSG( offset >= 0 && offset <= m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES, "DELAY_BUFFER::delay_offset_from_ratio()" );
  return offset;
}

int DELAY_BUFFER::delay_offset_from_time( int time_in_ms ) const
{
  int offset   = convert_time_in_ms_to_samples( time_in_ms );

  if( offset > m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES )
  {
    offset = m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES;
  }

  ASSERT_MSG( offset >= 0 && offset <= m_buffer_size_in_samples - AUDIO_BLOCK_SAMPLES, "DELAY_BUFFER::delay_offset_from_time()" );   
  return offset; 
}

void DELAY_BUFFER::write_sample( int16_t sample, int index )
{
  ASSERT_MSG( index >= 0 && index < m_buffer_size_in_samples, "DELAY_BUFFER::write_sample() writing outside buffer" );

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

int16_t DELAY_BUFFER::read_sample( int index ) const
{
  ASSERT_MSG( index >= 0 && index < m_buffer_size_in_samples, "DELAY_BUFFER::read_sample() writing outside buffer" );
 
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

void DELAY_BUFFER::increment_head( int& head ) const
{
  ++head;

  if( head >= m_buffer_size_in_samples )
  {
    head = 0;
  }
}

void DELAY_BUFFER::write_to_buffer( const int16_t* source, int size )
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

void DELAY_BUFFER::set_bit_depth( int sample_size_in_bits )
{
  // NOTE - do not print in this function, it is called before Serial is configured
  if( sample_size_in_bits != m_sample_size_in_bits )
  {
    m_sample_size_in_bits       = sample_size_in_bits;
    m_buffer_size_in_samples    = delay_buffer_size_in_samples( m_sample_size_in_bits );
        
    m_write_head                = 0;
      
    memset( m_buffer, 0, sizeof(m_buffer) );
  }
}

/////////////////////////////////////////////////////////////////////

GLITCH_DELAY_EFFECT::GLITCH_DELAY_EFFECT() :
  AudioStream( 1, m_input_queue_array ),
  m_input_queue_array(),
  m_delay_buffer(),
  m_play_head(m_delay_buffer),
  m_next_sample_size_in_bits(16),
  m_next_play_head_offset_in_samples(1),
  m_pending_glitch_time_in_ms(0),
  m_glitch_updates(0)
{

}

bool GLITCH_DELAY_EFFECT::glitch_active() const
{
  return m_glitch_updates > 0;
}

void GLITCH_DELAY_EFFECT::start_glitch()
{
  const float updates_per_ms              = ( AUDIO_SAMPLE_RATE_EXACT / AUDIO_BLOCK_SAMPLES ) / 1000.0f;

  m_glitch_updates                        = round( m_pending_glitch_time_in_ms * updates_per_ms );

  const int loop_size                     = AUDIO_SAMPLE_RATE * 0.02f; // 20 ms
  const int loop_start                    = m_play_head.current_position();
  
  m_play_head.enable_loop( loop_start, loop_start + loop_size );

  m_pending_glitch_time_in_ms             = 0;

#ifdef DEBUG_OUTPUT
  Serial.print("GLITCH_DELAY_EFFECT::start_glitch() num_updates ");
  Serial.print(m_glitch_updates);
  Serial.print("\n");
#endif
}

void GLITCH_DELAY_EFFECT::update_glitch()
{
  --m_glitch_updates;

  if( m_glitch_updates == 0 )
  {
    m_play_head.disable_loop();

#ifdef DEBUG_OUTPUT
  Serial.print("GLITCH_DELAY_EFFECT::update_glitch() END\n");
#endif

  }
}

void GLITCH_DELAY_EFFECT::update()
{      
  m_delay_buffer.set_bit_depth( m_next_sample_size_in_bits);

  // starting glitch
  if( m_pending_glitch_time_in_ms > 0 )
  {
    start_glitch();
  }

  if( glitch_active() )
  {
    update_glitch();
    
    audio_block_t* block        = allocate();

    // write head frozen during glitch
    
    m_play_head.read_from_play_head( block->data, AUDIO_BLOCK_SAMPLES );

    transmit( block, 0 );
    
    release( block );
  }
  else
  {
    // update the play head position
    const int new_playhead      = m_delay_buffer.position_offset_from_head( m_next_play_head_offset_in_samples );
    m_play_head.set_play_head( new_playhead );
    
    audio_block_t* block        = receiveWritable();
  
    if( block != nullptr )
    {
      m_delay_buffer.write_to_buffer( block->data, AUDIO_BLOCK_SAMPLES );
      
      m_play_head.read_from_play_head( block->data, AUDIO_BLOCK_SAMPLES );
    
      transmit( block, 0 );
    
      release( block );
    }
  }
}

void GLITCH_DELAY_EFFECT::set_delay_time_in_ms( int time_in_ms )
{
  m_next_play_head_offset_in_samples = m_delay_buffer.delay_offset_from_time( time_in_ms );
}

void GLITCH_DELAY_EFFECT::set_delay_time_as_ratio( float ratio_of_max_delay )
{
  ASSERT_MSG( ratio_of_max_delay >= 0.0f && ratio_of_max_delay <= 1.0f, "GLITCH_DELAY_EFFECT::set_delay_time()" );

  // quantize to 32 steps to avoid small fluctuations
  ratio_of_max_delay = ( static_cast<int>( ratio_of_max_delay * 32.0f ) ) / 32.0f;

  m_next_play_head_offset_in_samples = m_delay_buffer.delay_offset_from_ratio( ratio_of_max_delay );
}

void GLITCH_DELAY_EFFECT::set_bit_depth( int sample_size_in_bits )
{
  m_next_sample_size_in_bits = sample_size_in_bits;
  //set_bit_depth_impl( sample_size_in_bits );
}

void GLITCH_DELAY_EFFECT::activate_glitch( int active_time_in_ms )
{
  m_pending_glitch_time_in_ms = active_time_in_ms;
}

