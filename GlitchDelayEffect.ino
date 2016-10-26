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

const float LOOP_SIZE_IN_S( 0.05f );
const int FIXED_FADE_TIME_SAMPLES( (AUDIO_SAMPLE_RATE / 1000.0f ) * 10 ); // 10ms cross fade
const int SHIFT_SPEED( 30 );


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
  // standard delay
  if( m_loop_end < 0 )
  {
    if( m_current_play_head != m_destination_play_head )
    {
      const int current_cf_end = m_delay_buffer.wrap_to_buffer( m_current_play_head + m_fade_samples_remaining );
      if( position_inside_section( position, m_current_play_head, current_cf_end ) )
      {
        // inside the cross fade from current to destination
        return true;
      }

      const int destination_end = m_delay_buffer.wrap_to_buffer( m_destination_play_head + max_val<int>( read_size, m_fade_samples_remaining ) );
      if( position_inside_section( position, m_destination_play_head, destination_end ) )
      {
        // inside the cross fade from current to destination
        return true;
      }   
    }
    else
    {
      // not cross-fading
      const int read_end = m_delay_buffer.wrap_to_buffer( m_current_play_head + read_size );
      if( position_inside_section( position, m_current_play_head, read_end ) )
      {
        return true;
      }
    }
  }
  // otherwise looping
  else
  {
    const int loop_end_cf_end = m_delay_buffer.wrap_to_buffer( m_loop_end + FIXED_FADE_TIME_SAMPLES );
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

void DELAY_BUFFER::set_write_head( int new_write_head )
{
  m_write_head = new_write_head;
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

/////////////////////////////////////////////////////////////////////

GLITCH_DELAY_EFFECT::GLITCH_DELAY_EFFECT() :
  AudioStream( 1, m_input_queue_array ),
  m_input_queue_array(),
  m_delay_buffer(),
  m_play_head(m_delay_buffer),
  m_current_play_head_offset_in_samples(1),
  m_next_sample_size_in_bits(16),
  m_next_play_head_offset_in_samples(1),
  m_pending_glitch_time_in_ms(0),
  m_glitch_updates(-1),
  m_shift_forwards(true)
{

}

bool GLITCH_DELAY_EFFECT::can_start_glitch() const
{
  return !glitch_active() && !m_delay_buffer.write_buffer_fading_in();
}

bool GLITCH_DELAY_EFFECT::glitch_active() const
{
  return m_glitch_updates >= 0;
}

void GLITCH_DELAY_EFFECT::start_glitch()
{
  ASSERT_MSG( can_start_glitch(), "Invalid glitch start\n" );
  ASSERT_MSG( m_glitch_updates == -1, "GLITCH_DELAY_EFFECT::start_glitch() trying to start a glitch during a glitch\n" );
  
  if( m_play_head.crossfade_active() )
  {
    return;
  }
  
  const float updates_per_ms              = ( AUDIO_SAMPLE_RATE_EXACT / AUDIO_BLOCK_SAMPLES ) / 1000.0f;

  m_glitch_updates                        = round( m_pending_glitch_time_in_ms * updates_per_ms );
  m_shift_forwards                        = true;

  // set the loop so it ends just before the write buffer - otherwise we'll loop over a jump in audio
  const int loop_size                     = round( AUDIO_SAMPLE_RATE * LOOP_SIZE_IN_S );

  // number of write blocks needed to do the fade (write head will jump in these amounts
  int fade_blocks                         = FIXED_FADE_TIME_SAMPLES / AUDIO_BLOCK_SAMPLES;
  if( FIXED_FADE_TIME_SAMPLES % FIXED_FADE_TIME_SAMPLES > 0 )
  {
    ++fade_blocks;
  }
  const int fade_samples                  = fade_blocks * AUDIO_BLOCK_SAMPLES;
  
  const int loop_start                    = m_delay_buffer.wrap_to_buffer( m_delay_buffer.write_head() + fade_samples + 1 );
  const int loop_end                      = m_delay_buffer.wrap_to_buffer( loop_start + loop_size );

  ASSERT_MSG( loop_size + FIXED_FADE_TIME_SAMPLES + 1 < DELAY_BUFFER_SIZE_IN_BYTES, "Loop size too large\n" );
  ASSERT_MSG( loop_size > FIXED_FADE_TIME_SAMPLES * 2, "Loop size too small\n" );
  
  m_play_head.enable_loop( loop_start, loop_end );

  m_pending_glitch_time_in_ms             = 0;
}

void GLITCH_DELAY_EFFECT::update_glitch()
{
  if( m_glitch_updates > 0 )
  {
    --m_glitch_updates;

    if( m_play_head.initial_loop_crossfade_complete() )
    {
      if( m_shift_forwards )
      {
        const int new_loop_end = m_delay_buffer.wrap_to_buffer( m_play_head.loop_end() + (SHIFT_SPEED + FIXED_FADE_TIME_SAMPLES) );
  
        // check for crossing boundary
        if( new_loop_end == m_delay_buffer.write_head() ||
            (new_loop_end > m_delay_buffer.write_head()) != (m_play_head.loop_end() > m_delay_buffer.write_head()) )
        {
          m_shift_forwards = false;
        }
      }
      else
      {
        const int new_loop_start = m_delay_buffer.wrap_to_buffer( m_play_head.loop_start() - (SHIFT_SPEED + FIXED_FADE_TIME_SAMPLES) );
  
        if( new_loop_start == m_delay_buffer.write_head() ||
            (new_loop_start <= m_delay_buffer.write_head()) != (m_play_head.loop_start() <= m_delay_buffer.write_head()) )
        {
          m_shift_forwards = true;
        }      
      }
  
      if( m_shift_forwards )
      {
        m_play_head.shift_loop( SHIFT_SPEED );
      }
      else
      {
        m_play_head.shift_loop( -SHIFT_SPEED );
      }
    }
  }
  
  if( m_glitch_updates == 0 && !m_play_head.crossfade_active() )
  {    
    const int new_write_head = m_delay_buffer.wrap_to_buffer( m_play_head.current_position() + m_current_play_head_offset_in_samples );

    m_play_head.disable_loop();

    // as we stopped writing whilst glitching, we need to fade the new audio on-top of the old written audio

    // set the write head outside of the glitch loop window
    m_delay_buffer.set_write_head( new_write_head );
    m_delay_buffer.fade_in_write();

    m_glitch_updates = -1; // glitch not active
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

  update_glitch();

  audio_block_t* block        = receiveWritable();

  if( block != nullptr )
  {
    if( glitch_active() )
    {  
        // write head frozen during glitch (after initial cross fade)
        if( !m_play_head.initial_loop_crossfade_complete() )
        {
          m_delay_buffer.write_to_buffer( block->data, AUDIO_BLOCK_SAMPLES );
        }

        ASSERT_MSG( !m_play_head.position_inside_next_read( m_delay_buffer.write_head() + 1, AUDIO_BLOCK_SAMPLES ), "Glitch - reading over write buffer\n" ); // position after write head is OLD DATA
        m_play_head.read_from_play_head( block->data, AUDIO_BLOCK_SAMPLES );
    }
    else
    {
      // update the play head position
      if( m_next_play_head_offset_in_samples != m_current_play_head_offset_in_samples )
      {
        m_current_play_head_offset_in_samples = m_next_play_head_offset_in_samples;
        const int new_playhead                = m_delay_buffer.position_offset_from_head( m_next_play_head_offset_in_samples );
        m_play_head.set_play_head( new_playhead );

#ifdef DEBUG_OUTPUT
        Serial.print( "Set playhead " );
        Serial.print( new_playhead );
        Serial.print( "\n" );
#endif      
      }

      m_delay_buffer.write_to_buffer( block->data, AUDIO_BLOCK_SAMPLES );

      ASSERT_MSG( !m_play_head.position_inside_next_read( m_delay_buffer.write_head() + 1, AUDIO_BLOCK_SAMPLES ), "Non - reading over write buffer\n" ); // position after write head is OLD DATA
      m_play_head.read_from_play_head( block->data, AUDIO_BLOCK_SAMPLES );
    }

    transmit( block, 0 );
  
    release( block );
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

