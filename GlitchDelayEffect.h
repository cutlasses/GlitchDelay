#pragma once

#include <AudioStream.h>

#define DELAY_BUFFER_SIZE_IN_BYTES     1024*50      // 50k


class GLITCH_DELAY_EFFECT : public AudioStream
{
  byte                  m_buffer[DELAY_BUFFER_SIZE_IN_BYTES];
  audio_block_t*        m_input_queue_array[1];
  
  int                   m_write_head;     // read head when audio is frozen, write head when not frozen
  int                   m_play_head_offset_in_samples;

  int                   m_sample_size_in_bits;
  int                   m_buffer_size_in_samples;

  // store 'next' values, otherwise interrupt could be called during calculation of values
  float                 m_next_sample_size_in_bits;
  float                 m_next_play_head_offset_in_samples;
  

  void                  write_sample( int16_t sample, int index );
  int16_t               read_sample( int index ) const;

  int                   calculate_play_head() const;
  
  void                  write_to_buffer( const int16_t* source, int size );
  void                  read_from_buffer( int16_t* dest, int size );
 
  void                  set_bit_depth_impl( int sample_size_in_bits );
  void                  set_play_head_offset_in_samples_impl( int play_head_offset_in_samples );
  
public:

  GLITCH_DELAY_EFFECT();

  virtual void          update();

  void                  set_delay_time( float ratio_of_max_delay );
  void                  set_bit_depth( int sample_size_in_bits );
};


