#pragma once

#include <AudioStream.h>

#define DELAY_BUFFER_SIZE_IN_BYTES     1024*50      // 50k


class AUDIO_FREEZE_EFFECT : public AudioStream
{
  byte                  m_buffer[DELAY_BUFFER_SIZE_IN_BYTES];
  audio_block_t*        m_input_queue_array[1];
  
  float                 m_head;     // read head when audio is frozen, write head when not frozen

  int                   m_sample_size_in_bits;
  int                   m_buffer_size_in_samples;

  // store 'next' values, otherwise interrupt could be called during calculation of values
  float                 m_next_sample_size_in_bits;
  float                 m_next_length;
  float                 m_next_centre;
  float                 m_next_speed;
  


  int                   wrap_index_to_loop_section( int index ) const;

  void                  write_sample( int16_t sample, int index );
  int16_t               read_sample( int index ) const;
  
  void                  write_to_buffer( const int16_t* source, int size );
  void                  read_from_buffer( int16_t* dest, int size );
  void                  read_from_buffer_with_speed( int16_t* dest, int size );
  
  void                  advance_head( float inc );

  void                  set_bit_depth_impl( int sample_size_in_bits );
  
public:

  AUDIO_FREEZE_EFFECT();

  virtual void          update();

  void                  set_bit_depth( int sample_size_in_bits );
};


