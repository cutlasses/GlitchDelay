#pragma once

#include "CompileSwitches.h"

#ifdef DEBUG_OUTPUT

bool _assert_fail( const char* assert, const char* msg )
{
  Serial.print(assert);
  Serial.print(" ");
  Serial.print(msg);
  Serial.print("\n");

  return true;
}

#define ASSERT_MSG(x, msg) ((void)((x) || (_assert_fail(#x,msg))))
#else
#define ASSERT_MSG(x, msg)
#endif

template <typename T>
T clamp( const T& value, const T& min, const T& max )
{
  if( value < min )
  {
    return min;
  }
  if( value > max )
  {
    return max;
  }
  return value;
}

template <typename T>
T max_val( const T& v1, const T& v2 )
{
  if( v1 > v2 )
  {
    return v1;
  }
  else
  {
    return v2;
  }
}

template <typename T>
T min_val( const T& v1, const T& v2 )
{
  if( v1 < v2 )
  {
    return v1;
  }
  else
  {
    return v2;
  }
}

template <typename T>
T lerp( const T& v1, const T& v2, float t )
{
  return v1 + ( (v2 - v1) * t );
}

int trunc_to_int( float v )
{
  return static_cast<int>( trunc(v) );
}

template < typename TYPE, int CAPACITY >
class RUNNING_AVERAGE
{
  TYPE                    m_values[ CAPACITY ];
  int                     m_current;
  int                     m_size;

public:

  RUNNING_AVERAGE() :
    m_values(),
    m_current(0),
    m_size(0)
  {
  }

  void add( TYPE value )
  {
    m_values[ m_current ] = value;
    m_current             = ( m_current + 1 ) % CAPACITY;
    ++m_size;
    if( m_size > CAPACITY )
    {
      m_size              = CAPACITY;
    }
  }

  void reset()
  {
    m_size                = 0;
    m_current             = 0;
  }
  
  TYPE average() const
  {
    TYPE average = 0;
    for( int x = 0; x < m_size; ++x )
    {
      average += m_values[ x ];
    }
  }
};

