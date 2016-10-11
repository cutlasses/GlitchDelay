#pragma once

#include <AudioStream.h>

#define DELAY_BUFFER_SIZE_IN_BYTES     1024*50      // 50k

////////////////////////////////////

class GLITCH_DELAY_EFFECT;

////////////////////////////////////

class PLAY_HEAD
{
  const GLITCH_DELAY_EFFECT&  m_delay_buffer;
  
  int                         m_current_offset;
  int                         m_destination_offset;
  int                         m_fade_window_size_in_samples;
  int                         m_fade_samples_remaining;

  int                         calculate_play_head( int write_head, int offset ) const;
  int16_t                     read_sample_with_cross_fade( int write_head );
   
public:

  PLAY_HEAD( const GLITCH_DELAY_EFFECT& delay_buffer );

  void                        set_play_head( int offset_from_write_head );
  void                        read_from_play_head( int16_t* dest, int size, int write_head );  
};

////////////////////////////////////

class GLITCH_DELAY_EFFECT : public AudioStream
{
  friend class PLAY_HEAD; // TODO create DELAY_BUFFER class
  
  byte                  m_buffer[DELAY_BUFFER_SIZE_IN_BYTES];
  audio_block_t*        m_input_queue_array[1];
  
  int                   m_write_head;     // read head when audio is frozen, write head when not frozen
  PLAY_HEAD             m_play_head;

  int                   m_freeze_loop_start;
  int                   m_freeze_loop_end;

  int                   m_sample_size_in_bits;
  int                   m_buffer_size_in_samples;
  bool                  m_freeze_active;

  // store 'next' values, otherwise interrupt could be called during calculation of values
  float                 m_next_sample_size_in_bits;
  int                   m_next_play_head_offset_in_samples;
  

  void                  write_sample( int16_t sample, int index );
  int16_t               read_sample( int index ) const;
  
  void                  write_to_buffer( const int16_t* source, int size );
 
  void                  set_bit_depth_impl( int sample_size_in_bits );
  void                  set_freeze_impl( bool active, int loop_size_in_samples );
  
public:

  GLITCH_DELAY_EFFECT();

  bool                  freeze_active() const;

  virtual void          update();

  void                  set_delay_time_in_ms( int time_in_ms );
  void                  set_delay_time_as_ratio( float ratio_of_max_delay );
  void                  set_bit_depth( int sample_size_in_bits );
  void                  set_freeze( bool active, int loop_size_in_ms = -1 );
};


