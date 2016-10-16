#pragma once

#include <AudioStream.h>

#define DELAY_BUFFER_SIZE_IN_BYTES     1024*50      // 50k

////////////////////////////////////

class DELAY_BUFFER;

////////////////////////////////////

class PLAY_HEAD
{
  const DELAY_BUFFER&         m_delay_buffer;     // TODO pass in to save storage?
  
  int                         m_current_offset;
  int                         m_destination_offset;
  int                         m_fade_window_size_in_samples;
  int                         m_fade_samples_remaining;

  int16_t                     read_sample_with_cross_fade( int write_head );
   
public:

  PLAY_HEAD( const DELAY_BUFFER& delay_buffer );

  void                        set_play_head( int offset_from_write_head );
  void                        read_from_play_head( int16_t* dest, int size, int write_head );  
};

////////////////////////////////////

class DELAY_BUFFER
{
  friend PLAY_HEAD;
  
  byte                        m_buffer[DELAY_BUFFER_SIZE_IN_BYTES];
  int                         m_buffer_size_in_samples;
  int                         m_sample_size_in_bits;

  int                         m_write_head;

public:

  DELAY_BUFFER();

  int                         position_offset_from_head( int current_write_head, int offset ) const;
  int                         delay_offset_from_ratio( float ratio ) const;
  int                         delay_offset_from_time( int time_in_ms ) const;

  int                         write_head() const { return m_write_head; } // remove when play head stores index

  void                        write_sample( int16_t sample, int index );
  int16_t                     read_sample( int index ) const;
  
  void                        write_to_buffer( const int16_t* source, int size );

  void                        set_bit_depth( int sample_size_in_bits );
};

////////////////////////////////////

class GLITCH_DELAY_EFFECT : public AudioStream
{  
  audio_block_t*        m_input_queue_array[1];
  DELAY_BUFFER          m_delay_buffer;

  PLAY_HEAD             m_play_head;

  // store 'next' values, otherwise interrupt could be called during calculation of values
  int                   m_next_sample_size_in_bits;
  int                   m_next_play_head_offset_in_samples;
 
  void                  set_freeze_impl( bool active, int loop_size_in_samples );
  
public:

  GLITCH_DELAY_EFFECT();

  virtual void          update();

  void                  set_delay_time_in_ms( int time_in_ms );
  void                  set_delay_time_as_ratio( float ratio_of_max_delay );
  void                  set_bit_depth( int sample_size_in_bits );
};


