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

/////////////////////////////////////////////////////////////////////

AUDIO_FREEZE_EFFECT::AUDIO_FREEZE_EFFECT() :
  AudioStream( 1, m_input_queue_array ),
  m_buffer(),
  m_input_queue_array(),
  m_head(0),
  m_sample_size_in_bits(16),
  m_buffer_size_in_samples( delay_buffer_size_in_samples( 16 ) ),
  m_next_sample_size_in_bits(16)
{
  memset( m_buffer, 0, sizeof(m_buffer) );
}

/*
int AUDIO_FREEZE_EFFECT::wrap_index_to_loop_section( int index ) const
{
  if( index > m_loop_end )
  {
    return m_loop_start + ( index - m_loop_end ) - 1;
  }
  else if( index < m_loop_start )
  {
    return m_loop_end - ( m_loop_start - index ) - 1;
  }
  else
  {
    return index;
  }
}
*/

void AUDIO_FREEZE_EFFECT::write_sample( int16_t sample, int index )
{
  ASSERT_MSG( index >= 0 && index < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::write_sample() writing outside buffer" );

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

int16_t AUDIO_FREEZE_EFFECT::read_sample( int index ) const
{
  ASSERT_MSG( index >= 0 && index < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::read_sample() writing outside buffer" );
 
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

void AUDIO_FREEZE_EFFECT::write_to_buffer( const int16_t* source, int size )
{
  ASSERT_MSG( trunc_to_int(m_head) >= 0 && trunc_to_int(m_head) < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::write_to_buffer()" );
  
  for( int x = 0; x < size; ++x )
  {
    write_sample( source[x], trunc_to_int(m_head) );

    if( trunc_to_int(++m_head) == m_buffer_size_in_samples )
    {
      m_head                        = 0.0f;
    }
  }
}

void AUDIO_FREEZE_EFFECT::read_from_buffer( int16_t* dest, int size )
{
   ASSERT_MSG( trunc_to_int(m_head) >= 0 && trunc_to_int(m_head) < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::read_from_buffer()" );
 
  for( int x = 0; x < size; ++x )
  {
    dest[x]                   = read_sample( trunc_to_int(m_head) );
    
    // head will have limited movement in freeze mode
    if( ++m_head >= m_buffer_size_in_samples )
    {
      m_head                  = 0;
    } 
  }
}

/*
void AUDIO_FREEZE_EFFECT::read_from_buffer_with_speed( int16_t* dest, int size )
{          
    ASSERT_MSG( trunc_to_int(m_head) >= 0 && trunc_to_int(m_head) < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::read_from_buffer_with_speed()" );

    for( int x = 0; x < size; ++x )
    {
      if( m_speed < 1.0f )
      {
        float prev_head         = m_head;
        int curr_index          = truncf( m_head );
        advance_head( m_speed );
        int next_index          = truncf( m_head );

        ASSERT_MSG( curr_index >= 0 && curr_index < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::read_from_buffer_with_speed()" );
        ASSERT_MSG( next_index >= 0 && next_index < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::read_from_buffer_with_speed()" );

        int16_t sample( 0 );

        if( curr_index == next_index )
        {
          // both current and next are in the same sample
          sample                = read_sample(curr_index);
        }
        else
        {
          // crossing 2 samples - calculate how much of each sample to use, then lerp between them
          // use the fractional part - if 0.3 'into' next sample, then we mix 0.3 of next and 0.7 of current
          double int_part;
          float rem             = m_reverse ? modf( prev_head, &int_part ) : modf( m_head, &int_part );
          const float t         = rem / m_speed;      
          sample                = lerp( read_sample(curr_index), read_sample(next_index), t );          
       }

        dest[x]                 = sample;
      }
      else
      {
        dest[x]                 = read_sample( trunc_to_int(m_head) );
        
        advance_head( m_speed );
      }
    }
}
*/

void AUDIO_FREEZE_EFFECT::advance_head( float inc )
{
  // advance the read head - clamped between start and end
  // will read backwards when in reverse mode
/* TODO!!!!
  bool end_reached    = false;

  if( m_loop_start == m_loop_end )
  {
    // play head cannot be incremented
#ifdef DEBUG_OUTPUT
    Serial.print("loop length is 1");
#endif
    m_head      = m_loop_start;   
    end_reached = true;
  }
  else
  {
    ASSERT_MSG( truncf(m_head) >= 0 && truncf(m_head) < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::next_head()" );
  
    inc = min( inc, m_loop_end - m_loop_start - 1 ); // clamp the increment to the loop length
    //ASSERT_MSG( inc > 0 && inc < m_loop_end - m_loop_start, "Invalid inc AUDIO_FREEZE_EFFECT::next_head()" );
    
    if( m_reverse )
    {
      m_head               -= inc;
      if( m_head < m_loop_start )
      {
        const float rem       = m_loop_start - m_head - 1.0f;
        m_head                = m_loop_end - rem;
      }
  
      ASSERT_MSG( truncf(m_head) >= 0 && truncf(m_head) < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::next_head()" );
    }
    else
    {
      m_head                += inc;
      if( m_head > m_loop_end )
      {
        const float rem     = m_head - m_loop_end - 1.0f;
        m_head              = m_loop_start + rem;
        end_reached         = true;
      }
  
      ASSERT_MSG( truncf(m_head) >= 0 && truncf(m_head) < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::next_head()" );
  
#ifdef DEBUG_OUTPUT
      if( truncf(m_head) < 0 || truncf(m_head) >= m_buffer_size_in_samples )
      {
        Serial.print("next_head ");
        Serial.print(m_head);
        Serial.print(" rem  ");
        Serial.print(m_head - m_loop_end - 1.0f);
        Serial.print( " loop start ");
        Serial.print(m_loop_start);
        Serial.print( " loop end ");
        Serial.print(m_loop_end);
        Serial.print( " inc ");
        Serial.print(inc);
        Serial.print( "\n");          
      }
#endif
    }
  }

  if( end_reached && m_one_shot )
  {
    m_paused = true;
  }
*/
}

void AUDIO_FREEZE_EFFECT::update()
{      
  set_bit_depth_impl( m_next_sample_size_in_bits);

  ASSERT_MSG( trunc_to_int(m_head) >= 0 && trunc_to_int(m_head) < m_buffer_size_in_samples, "AUDIO_FREEZE_EFFECT::update()" );

  audio_block_t* block        = receiveReadOnly();

  if( block != nullptr )
  {
    write_to_buffer( block->data, AUDIO_BLOCK_SAMPLES );

    transmit( block, 0 );

    release( block );
  }

  /*
  if( m_freeze_active )
  {
    if( !m_paused )
    {    
      audio_block_t* block        = allocate();
  
      if( block != nullptr )
      {
        //read_from_buffer( block->data, AUDIO_BLOCK_SAMPLES );
        read_from_buffer_with_speed( block->data, AUDIO_BLOCK_SAMPLES );
    
        transmit( block, 0 );
      
        release( block );    
      }
    }
  }
  else
  {
    audio_block_t* block        = receiveReadOnly();

    if( block != nullptr )
    {
      write_to_buffer( block->data, AUDIO_BLOCK_SAMPLES );
  
      transmit( block, 0 );
  
      release( block );
    }
  }
  */
}

void AUDIO_FREEZE_EFFECT::set_bit_depth_impl( int sample_size_in_bits )
{
  if( sample_size_in_bits != m_sample_size_in_bits )
  {
      m_sample_size_in_bits       = sample_size_in_bits;
      m_buffer_size_in_samples    = delay_buffer_size_in_samples( m_sample_size_in_bits );
      
      m_head                      = 0.0f;
    
      memset( m_buffer, 0, sizeof(m_buffer) );

#ifdef DEBUG_OUTPUT
      Serial.print("Set bit depth:");
      Serial.print( m_sample_size_in_bits );
      Serial.print("\n");
#endif
  }
}

void AUDIO_FREEZE_EFFECT::set_bit_depth( int sample_size_in_bits )
{
  m_next_sample_size_in_bits = sample_size_in_bits;
  //set_bit_depth_impl( sample_size_in_bits );
}
