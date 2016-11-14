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

const int FIXED_FADE_TIME_SAMPLES( (AUDIO_SAMPLE_RATE / 1000.0f ) * 4 ); // 4ms cross fade
const float MIN_LOOP_SIZE_IN_SAMPLES( (FIXED_FADE_TIME_SAMPLES * 2) + AUDIO_BLOCK_SAMPLES );
const float MAX_LOOP_SIZE_IN_SAMPLES( AUDIO_SAMPLE_RATE * 0.35f );
const int MIN_SHIFT_SPEED( 0 );
const int MAX_SHIFT_SPEED( 100 );


/////////////////////////////////////////////////////////////////////

int delay_buffer_size_in_samples( int sample_size_in_bits )
{
  const int bytes_per_sample = sample_size_in_bits / 8;

  if( sample_size_in_bits % 8 != 0 )
  {
    return (DELAY_BUFFER_SIZE_IN_BYTES / bytes_per_sample) + 1;
  }
  else
  {
    return DELAY_BUFFER_SIZE_IN_BYTES / bytes_per_sample;
  }
}

int convert_time_in_ms_to_samples( int time_in_ms )
{
  static int num_samples_per_ms = AUDIO_SAMPLE_RATE / 1000;
  return num_samples_per_ms * time_in_ms; 
}

int fade_out_in( int x, int y, float t )
{
  // fade down then back up
  if( t < 0.5f )
  {
    // t = 0.0 -> 1, t = 0.5 -> 0
    t = 1.0f - ( t * 2.0f );

    return round( x * t );
  }
  else
  {
    // t = 0.5 -> 0, t = 1 -> 1
    t = ( t * 2.0f ) - 1.0f;

    return round( y * t );
  }
}

int cross_fade_samples( int x, int y, float t )
{
  return round( lerp<float>( x, y, t ) );
}

/////////////////////////////////////////////////////////////////////

PLAY_HEAD::PLAY_HEAD( const DELAY_BUFFER& delay_buffer ) :
  m_delay_buffer( delay_buffer ),
  m_current_play_head( 0 ),
  m_destination_play_head( 0 ),
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

int PLAY_HEAD::loop_start() const
{
  return m_loop_start;
}

int PLAY_HEAD::loop_end() const
{
  return m_loop_end;
}

bool PLAY_HEAD::position_inside_section( int position, int start, int end ) const
{
  if( end < start )
  {
    // current and destination are wrapped around
    if( position >= start || position <= end )
    {
      return true;
    }
  }
  else if( position >= start && position <= end )
  {
    return true;
  }

  return false;
}

bool PLAY_HEAD::position_inside_next_read( int position, int read_size ) const
{
  // standard delay (or crossfading into a loop)
  if( m_loop_end < 0 || (!m_initial_loop_crossfade_complete && m_fade_samples_remaining > 0) )
  {
    // cross-fading
    if( m_current_play_head != m_destination_play_head )
    {
      const int fade_read_size = min_val<int>( read_size, m_fade_samples_remaining ) - 1; // read_size -1 because if 1 sample is read start == end
      const int current_cf_end = m_delay_buffer.wrap_to_buffer( m_current_play_head + fade_read_size );
      if( position_inside_section( position, m_current_play_head, current_cf_end ) )
      {
        // inside the cross fade from current to destination
        return true;
      }

      const int destination_end = m_delay_buffer.wrap_to_buffer( m_destination_play_head + read_size - 1 ); // after fading, destination will become current, read_size samples will be read
      if( position_inside_section( position, m_destination_play_head, destination_end ) )
      {
        // inside the cross fade from current to destination
        return true;
      }   
    }
    else
    {
      // not cross-fading
      const int read_end = m_delay_buffer.wrap_to_buffer( m_current_play_head + read_size - 1);
      if( position_inside_section( position, m_current_play_head, read_end ) )
      {
        return true;
      }
    }
  }
  // otherwise looping
  else
  {
    // NOTE this tests entire loop NOT next read per-se
    const int loop_end_cf_end = m_delay_buffer.wrap_to_buffer( m_loop_end + FIXED_FADE_TIME_SAMPLES - 1 );
    if( position_inside_section( position, m_loop_start, loop_end_cf_end ) )
    {
      // inside the cross fade from current to destination
      return true;
    }    
  }

  return false;
}

bool PLAY_HEAD::initial_loop_crossfade_complete() const
{
  return m_initial_loop_crossfade_complete;
}

bool PLAY_HEAD::crossfade_active() const
{
  return m_current_play_head != m_destination_play_head;
}

int16_t PLAY_HEAD::read_sample_with_cross_fade()
{
  ASSERT_MSG( m_fade_samples_remaining >= 0, "PLAY_HEAD::read_sample_with_cross_fade()" );

  int16_t sample(0);

  // cross-fading
  if( m_fade_samples_remaining > 0 )
  {
    int16_t current_sample            = m_delay_buffer.read_sample( m_current_play_head );

    int16_t destination_sample        = m_delay_buffer.read_sample( m_destination_play_head );

    const float t                     = static_cast<float>(m_fade_samples_remaining) / FIXED_FADE_TIME_SAMPLES; // t=0 at destination, t=1 at current
    --m_fade_samples_remaining;

    sample                            = cross_fade_samples( destination_sample, current_sample, t );

    m_delay_buffer.increment_head( m_current_play_head );
    m_delay_buffer.increment_head( m_destination_play_head );
  }
  // not cross-fading
  else
  {
    m_initial_loop_crossfade_complete = true;
    
    m_current_play_head               = m_destination_play_head;
    sample                            = m_delay_buffer.read_sample( m_current_play_head );
    
    m_delay_buffer.increment_head( m_current_play_head );
    m_destination_play_head           = m_current_play_head;
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

  m_fade_samples_remaining      = FIXED_FADE_TIME_SAMPLES;
}

void PLAY_HEAD::read_from_play_head( int16_t* dest, int size )
{
  for( int x = 0; x < size; ++x )
  {
    if( m_loop_end >= 0 && !position_inside_section( m_destination_play_head, m_loop_start, m_loop_end ) )
    {
      ASSERT_MSG( m_loop_start >= 0, "PLAY_HEAD::read_from_play_head() invalid loop start" );
      ASSERT_MSG( m_initial_loop_crossfade_complete, "looping before we've finished the cross-fade into the loop\n" );
      set_play_head( m_loop_start );  
    }
    
    dest[x] = read_sample_with_cross_fade();
  }
}

void PLAY_HEAD::enable_loop( int start, int end )
{ 
  m_loop_start  = start;
  m_loop_end    = end;

  m_initial_loop_crossfade_complete = false;

  // force a new cross fade
  m_destination_play_head           = m_loop_start;
  m_fade_samples_remaining          = FIXED_FADE_TIME_SAMPLES;
}

void PLAY_HEAD::disable_loop()
{  
  m_loop_start                      = -1;
  m_loop_end                        = -1;
}

void PLAY_HEAD::shift_loop( int offset )
{
  m_loop_start      = m_delay_buffer.wrap_to_buffer( m_loop_start + offset );
  m_loop_end        = m_delay_buffer.wrap_to_buffer( m_loop_end + offset );
}

#ifdef DEBUG_OUTPUT
void PLAY_HEAD::debug_output()
{
  Serial.print("PLAY_HEAD current:");
  Serial.print(m_current_play_head);
  Serial.print(" destination:");
  Serial.print(m_destination_play_head);
  Serial.print(" loop start:");
  Serial.print(m_loop_start);
  Serial.print(" loop end:");
  Serial.print(m_loop_end);
  Serial.print(" fade samples:");
  Serial.print(m_fade_samples_remaining);
  if( !m_initial_loop_crossfade_complete )
  {
    Serial.print(" INITIAL CF");
  }
  Serial.print("\n");
}
#endif

/////////////////////////////////////////////////////////////////////

DELAY_BUFFER::DELAY_BUFFER() :
  m_buffer(),
  m_buffer_size_in_samples(0),
  m_sample_size_in_bits(0),
  m_write_head(0),
  m_fade_samples_remaining(0)
{
  set_bit_depth( 16 );
}

int DELAY_BUFFER::position_offset_from_head( int offset ) const
{
  ASSERT_MSG( offset >= 0 && offset < m_buffer_size_in_samples - 1, "DELAY_BUFFER::position_offset_from_head()" );
  
  int position = wrap_to_buffer( m_write_head - offset );
  
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

int DELAY_BUFFER::write_head() const
{
  return m_write_head;
}

int DELAY_BUFFER::wrap_to_buffer( int position ) const
{
  if( position < 0 )
  {
    return m_buffer_size_in_samples + position;
  }
  else if( position >= m_buffer_size_in_samples )
  {
    return position - m_buffer_size_in_samples;
  }

  return position;
}

bool DELAY_BUFFER::write_buffer_fading_in() const
{
  return m_fade_samples_remaining > 0;  
}

void DELAY_BUFFER::write_sample( int16_t sample, int index )
{
  ASSERT_MSG( index >= 0 && index < m_buffer_size_in_samples, "DELAY_BUFFER::write_sample() writing outside buffer" );

  switch( m_sample_size_in_bits )
  {
    case 8:
    {
      int8_t sample8                      = (sample >> 8) & 0x00ff;
      int8_t* sample_buffer               = reinterpret_cast<int8_t*>(m_buffer);
      sample_buffer[ index ]              = sample8;
      break;
    }
    case 12:
    {
      int8_t* sample_buffer               = reinterpret_cast<int8_t*>(m_buffer);
      if( index & 1 )
      {
        // odd indices
        sample_buffer[ index ]            &= ( 0x00f0 | (( sample & 0xf000 ) >> 12) );
        sample_buffer[ index + 1 ]        = (( sample & 0x0ff0 ) >> 4);
      }
      else
      {
        // even indices
        sample_buffer[ index ]            = (sample >> 8);
        sample_buffer[ index+1 ]          &= ( ( sample & 0x00f0 ) | 0x000f );
      }
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
    case 12:
    {
      const int8_t* sample_buffer    = reinterpret_cast<const int8_t*>(m_buffer);
      if( index & 1 )
      {
        // odd indices
        return ( (( sample_buffer[index] & 0x000f ) << 12) | sample_buffer [index + 1 ] );
      }
      else
      {
        // even indices
        return ( ( sample_buffer[index] << 8 ) | ( sample_buffer[index+1] & 0x00f0 ) );
      }
      break;
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
    // fading in the write head
    if( m_fade_samples_remaining > 0 )
    {
       int16_t old_sample       = read_sample( m_write_head );
       int16_t new_sample       = source[x];

       const float t            = static_cast<float>(m_fade_samples_remaining) / FIXED_FADE_TIME_SAMPLES; // t=1 at old t=0 at new
      --m_fade_samples_remaining;
  
      int16_t cf_sample         = cross_fade_samples( new_sample, old_sample, t ); 

       write_sample( cf_sample, m_write_head );
    }
    else
    {
      write_sample( source[x], m_write_head );
    }

    // increment write head
    increment_head( m_write_head );
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

void DELAY_BUFFER::fade_in_write()
{
  ASSERT_MSG( m_fade_samples_remaining == 0, "DELAY_BUFFER::fade_in_write() trying to start a fade during a fade" );
  m_fade_samples_remaining = FIXED_FADE_TIME_SAMPLES;
}

void DELAY_BUFFER::set_write_head( int new_write_head )
{
  m_write_head = new_write_head;
}

void DELAY_BUFFER::set_loop_behind_write_head( PLAY_HEAD& play_head, int loop_size, int speed_in_samples ) const
{

  int loop_end                            = m_write_head - ( FIXED_FADE_TIME_SAMPLES + AUDIO_BLOCK_SAMPLES + speed_in_samples );
  loop_end                                = wrap_to_buffer( loop_end );
  const int loop_start                    = wrap_to_buffer( loop_end - loop_size );

  ASSERT_MSG( loop_size + FIXED_FADE_TIME_SAMPLES + 1 < DELAY_BUFFER_SIZE_IN_BYTES, "Loop size too large\n" );
  ASSERT_MSG( loop_size > FIXED_FADE_TIME_SAMPLES * 2, "Loop size too small\n" );
  
  play_head.enable_loop( loop_start, loop_end );  
}

#ifdef DEBUG_OUTPUT
void DELAY_BUFFER::debug_output()
{
 Serial.print("DELAY_BUFFER write head:");
 Serial.print(m_write_head);
 Serial.print(" fade samples:");
 Serial.print(m_fade_samples_remaining);
 Serial.print("\n"); 
}
#endif

/////////////////////////////////////////////////////////////////////

GLITCH_DELAY_EFFECT::GLITCH_DELAY_EFFECT() :
  AudioStream( 1, m_input_queue_array ),
  m_input_queue_array(),
  m_delay_buffer(),
  m_play_head(m_delay_buffer),
  m_speed_ratio(1.0f),
  m_speed_in_samples(MAX_SHIFT_SPEED),
  m_loop_size_ratio(1.0f),
  m_loop_size_in_samples(MAX_LOOP_SIZE_IN_SAMPLES),
  m_next_sample_size_in_bits(16)
{
  start_glitch();
}

void GLITCH_DELAY_EFFECT::start_glitch()
{
  if( m_play_head.crossfade_active() )
  {
    return;
  }

  float r = (random(1000) / 1000.0f) * 0.25f;
  r += 0.75f;
  
  // set the loop so it ends just before the write buffer - otherwise we'll loop over a jump in audio
  m_loop_size_in_samples                  = round( lerp<float>( MIN_LOOP_SIZE_IN_SAMPLES, MAX_LOOP_SIZE_IN_SAMPLES, m_loop_size_ratio * r ) );
  m_speed_in_samples                      = round( lerp<float>( MIN_SHIFT_SPEED, MAX_SHIFT_SPEED, m_speed_ratio * r ) );

  m_delay_buffer.set_loop_behind_write_head( m_play_head, m_loop_size_in_samples, m_speed_in_samples );
}

void GLITCH_DELAY_EFFECT::update_glitch()
{
  ASSERT_MSG( !m_play_head.position_inside_next_read( m_delay_buffer.write_head(), AUDIO_BLOCK_SAMPLES ), "GLITCH_DELAY_EFFECT::update_glitch() shift error" );

  // check whether the write head is about to run over the read head, in which case cross fade read head to new position
  const int extended_start = m_delay_buffer.wrap_to_buffer( m_play_head.loop_start() - ( FIXED_FADE_TIME_SAMPLES + FIXED_FADE_TIME_SAMPLES + AUDIO_BLOCK_SAMPLES ) );
  if( m_play_head.position_inside_section( m_delay_buffer.write_head(), extended_start, m_play_head.loop_end() ) )
  {
    start_glitch();
  }

  if( m_play_head.initial_loop_crossfade_complete() )
  {
    m_play_head.shift_loop( m_speed_in_samples );
  }

  ASSERT_MSG( !m_play_head.position_inside_next_read( m_delay_buffer.write_head(), AUDIO_BLOCK_SAMPLES ), "GLITCH_DELAY_EFFECT::update_glitch() shift error" );
}

void GLITCH_DELAY_EFFECT::update()
{        
  m_delay_buffer.set_bit_depth( m_next_sample_size_in_bits );

  update_glitch();

  audio_block_t* block        = receiveWritable();

  if( block != nullptr )
  {
    m_delay_buffer.write_to_buffer( block->data, AUDIO_BLOCK_SAMPLES );

    ASSERT_MSG( !m_play_head.position_inside_next_read( m_delay_buffer.write_head(), AUDIO_BLOCK_SAMPLES ), "Non - reading over write buffer\n" ); // position after write head is OLD DATA
    m_play_head.read_from_play_head( block->data, AUDIO_BLOCK_SAMPLES );

    transmit( block, 0 );
  
    release( block );
  }
}

void GLITCH_DELAY_EFFECT::set_bit_depth( int sample_size_in_bits )
{
  m_next_sample_size_in_bits = sample_size_in_bits;
  //set_bit_depth_impl( sample_size_in_bits );
}

void GLITCH_DELAY_EFFECT::set_speed( float speed )
{
  m_speed_ratio = speed;
}

void GLITCH_DELAY_EFFECT::set_loop_size( float loop_size )
{
  m_loop_size_ratio = loop_size;
}

