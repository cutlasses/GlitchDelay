#pragma once

#include <AudioStream.h>

#define DELAY_BUFFER_SIZE_IN_BYTES     1024*50      // 50k

////////////////////////////////////

class DELAY_BUFFER;

////////////////////////////////////

class PLAY_HEAD
{
  const DELAY_BUFFER&         m_delay_buffer;     // TODO pass in to save storage?
  
  int                         m_current_play_head;
  int                         m_destination_play_head;
  int                         m_fade_window_size_in_samples;
  int                         m_fade_samples_remaining;

  int                         m_loop_start;
  int                         m_loop_end;

  bool                        m_initial_loop_crossfade_complete;

  int16_t                     read_sample_with_cross_fade();
   
public:

  PLAY_HEAD( const DELAY_BUFFER& delay_buffer );

  int                         current_position() const;
  int                         destination_position() const;

  int                         loop_start() const;
  int                         loop_end() const;

  bool                        position_inside_crossfade( int position ) const;
  bool                        position_inside_section( int position, int start, int end ) const;
  bool                        crossfade_active() const;
  bool                        initial_loop_crossfade_complete() const;

  void                        set_play_head( int offset_from_write_head );
  void                        read_from_play_head( int16_t* dest, int size );  

  void                        enable_loop( int start, int end );
  void                        disable_loop();
  void                        shift_loop( const DELAY_BUFFER& delay_buffer, int offset );
};

////////////////////////////////////

class DELAY_BUFFER
{
  friend PLAY_HEAD;
  
  byte                        m_buffer[DELAY_BUFFER_SIZE_IN_BYTES];
  int                         m_buffer_size_in_samples;
  int                         m_sample_size_in_bits;

  int                         m_write_head;

  int                         m_fade_samples_remaining;

public:

  DELAY_BUFFER();

  int                         position_offset_from_head( int offset ) const;
  int                         delay_offset_from_ratio( float ratio ) const;
  int                         delay_offset_from_time( int time_in_ms ) const;
  int                         write_head() const;
  int                         wrap_to_buffer( int position ) const;

  void                        write_sample( int16_t sample, int index );
  int16_t                     read_sample( int index ) const;

  void                        increment_head( int& head ) const;
  
  void                        write_to_buffer( const int16_t* source, int size );

  void                        set_write_head( int new_write_head );
  void                        set_bit_depth( int sample_size_in_bits );

  void                        fade_in_write();
};

////////////////////////////////////

class GLITCH_DELAY_EFFECT : public AudioStream
{  
  audio_block_t*        m_input_queue_array[1];
  DELAY_BUFFER          m_delay_buffer;

  PLAY_HEAD             m_play_head;

  int                   m_current_play_head_offset_in_samples;

  // store 'next' values, otherwise interrupt could be called during calculation of values
  int                   m_next_sample_size_in_bits;
  int                   m_next_play_head_offset_in_samples;
  int                   m_pending_glitch_time_in_ms;

  int                   m_glitch_updates;
  bool                  m_shift_forwards;
  
  void                  start_glitch();
  void                  update_glitch();
  
public:

  GLITCH_DELAY_EFFECT();

  bool                  glitch_active() const;

  virtual void          update();

  void                  set_delay_time_in_ms( int time_in_ms );
  void                  set_delay_time_as_ratio( float ratio_of_max_delay );
  void                  set_bit_depth( int sample_size_in_bits );

  void                  activate_glitch( int active_time_in_ms );
};


