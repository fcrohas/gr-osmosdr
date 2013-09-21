/* -*- c++ -*- */
/*
 * Copyright 2013 Nuand LLC
 * Copyright 2013 Dimitri Stolnikov <horiz0n@gmx.net>
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>

#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <gnuradio/io_signature.h>

#include "arg_helpers.h"
#include "bladerf_sink_c.h"

#define NUM_BUFFERS 32
#define NUM_SAMPLES_PER_BUFFER 4096

using namespace boost::assign;

/*
 * Create a new instance of bladerf_source_c and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
bladerf_sink_c_sptr make_bladerf_sink_c (const std::string &args)
{
  return gnuradio::get_initial_sptr(new bladerf_sink_c (args));
}

/*
 * Specify constraints on number of input and output streams.
 * This info is used to construct the input and output signatures
 * (2nd & 3rd args to gr_block's constructor).  The input and
 * output signatures are used by the runtime system to
 * check that a valid number and type of inputs and outputs
 * are connected to this block.  In this case, we accept
 * only 0 input and 1 output.
 */
static const int MIN_IN = 1;   // mininum number of input streams
static const int MAX_IN = 1;   // maximum number of input streams
static const int MIN_OUT = 0;  // minimum number of output streams
static const int MAX_OUT = 0;  // maximum number of output streams

/*
 * The private constructor
 */
bladerf_sink_c::bladerf_sink_c (const std::string &args)
  : gr::sync_block ("bladerf_sink_c",
                    gr::io_signature::make (MIN_IN, MAX_IN, sizeof (gr_complex)),
                    gr::io_signature::make (MIN_OUT, MAX_OUT, sizeof (gr_complex)))
{
  int ret;
  unsigned int device_number = 0;
  std::string device_name;

  dict_t dict = params_to_dict(args);

  if (dict.count("bladerf"))
  {
    std::string value = dict["bladerf"];
    if ( value.length() )
    {
      try {
        device_number = boost::lexical_cast< unsigned int >( value );
      } catch ( std::exception &ex ) {
        throw std::runtime_error(
              "Failed to use '" + value + "' as device number: " + ex.what());
      }
    }
  }

  device_name = boost::str(boost::format( "libusb:instance=%d" ) % device_number);

  /* Open a handle to the device */
  ret = bladerf_open( &_dev, device_name.c_str() );
  if ( ret != 0 ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "failed to open bladeRF device " + device_name );
  }

  if (dict.count("fw"))
  {
    std::string fw = dict["fw"];

    std::cerr << "Flashing firmware image " << fw << "..., DO NOT INTERRUPT!"
              << std::endl;
    ret = bladerf_flash_firmware( _dev, fw.c_str() );
    if ( ret != 0 )
      std::cerr << "bladerf_flash_firmware has failed with " << ret << std::endl;
    else
      std::cerr << "The firmware has been successfully flashed." << std::endl;
  }

  if (dict.count("fpga"))
  {
    std::string fpga = dict["fpga"];

    std::cerr << "Loading FPGA bitstream " << fpga << "..." << std::endl;
    ret = bladerf_load_fpga( _dev, fpga.c_str() );
    if ( ret != 0 && ret != 1 )
      std::cerr << "bladerf_load_fpga has failed with " << ret << std::endl;
    else
      std::cerr << "The FPGA bitstream has been successfully loaded." << std::endl;
  }

  std::cerr << "Using nuand LLC bladeRF #" << device_number;

  char serial[BLADERF_SERIAL_LENGTH];
  if ( bladerf_get_serial( _dev, serial ) == 0 )
    std::cerr << " SN " << serial;

  unsigned int major, minor;
  if ( bladerf_get_fw_version( _dev, &major, &minor) == 0 )
    std::cerr << " FW v" << major << "." << minor;

  if ( bladerf_get_fpga_version( _dev, &major, &minor) == 0 )
    std::cerr << " FPGA v" << major << "." << minor;

  std::cerr << std::endl;

  if ( bladerf_is_fpga_configured( _dev ) != 1 )
  {
    std::ostringstream oss;
    oss << "The FPGA is not configured! "
        << "Provide device argument fpga=/path/to/the/bitstream.rbf to load it.";

    throw std::runtime_error( oss.str() );
  }

  /* Set the range of VGA1, VGA1GAINT[7:0] */
  _vga1_range = osmosdr::gain_range_t( -35, -4, 1 );

  /* Set the range of VGA2, VGA2GAIN[4:0] */
  _vga2_range = osmosdr::gain_range_t( 0, 25, 1 );

  _num_buffers = _samples_per_buffer = 0;

  /* Initialize buffer and sample configuration */
  if (dict.count("buffers")) {
    _num_buffers = boost::lexical_cast< size_t >( dict["buffers"] );
  }

  if (dict.count("buflen")) {
    _samples_per_buffer = boost::lexical_cast< size_t >( dict["buflen"] );
  }

  unsigned int transfers = 0;
  if (dict.count("transfers")) {
    transfers = boost::lexical_cast< size_t >( dict["transfers"] );
  }

  /* Require value to be >= 2 so we can ensure we have twice as many
   * buffers as transfers */
  if (_num_buffers <= 1) {
    _num_buffers = NUM_BUFFERS;
  }

  if (0 == _samples_per_buffer) {
    _samples_per_buffer = NUM_SAMPLES_PER_BUFFER;
  } else {
    /* For SC16_Q12, 1 sample = 2 int16_t's */
    _samples_per_buffer /= 2 * sizeof(int16_t);

    if (_samples_per_buffer < 1024 || _samples_per_buffer % 1024 != 0)
      _samples_per_buffer = NUM_SAMPLES_PER_BUFFER;
  }



  if (transfers == 0 || transfers > (_num_buffers / 2)) {
      transfers = _num_buffers / 2;

  }

  /* Initialize the stream */
  ret = bladerf_init_stream( &_stream, _dev, stream_callback,
                             &_buffers, _num_buffers, BLADERF_FORMAT_SC16_Q12,
                             _samples_per_buffer, transfers, this );
  if ( ret != 0 )
    std::cerr << "bladerf_init_stream has failed with " << ret << std::endl;

  /* Initialize buffer management */
  _buf_index = _next_to_tx = 0;
  _next_value = static_cast<int16_t*>(_buffers[0]);
  _samples_left = _samples_per_buffer;

  _filled = new bool[_num_buffers];
  if (!_filled) {
      throw std::runtime_error( std::string(__FUNCTION__) + ": " +
                                "Failed to allocate _filled[]");
  }

  for (size_t i = 0; i < _num_buffers; ++i) {
    _filled[i] = false;
  }

  ret = bladerf_enable_module( _dev, BLADERF_MODULE_TX, true );
  if ( ret != 0 )
    std::cerr << "bladerf_enable_module has failed with " << ret << std::endl;

  set_running( true );
  _thread = gr::thread::thread( boost::bind(&bladerf_sink_c::write_task, this) );
}

/*
 * Our virtual destructor.
 */
bladerf_sink_c::~bladerf_sink_c ()
{
  int ret;

  set_running(false);

  /* Ensure work() or callbacks return from wait() calls */
  _buf_status_lock.lock();
  _samp_avail.notify_all();
  _buffer_emptied.notify_all();
  _buf_status_lock.unlock();

  _thread.join();

  ret = bladerf_enable_module( _dev, BLADERF_MODULE_TX, false );
  if ( ret != 0 )
    std::cerr << "bladerf_enable_module has failed with " << ret << std::endl;

  /* Release stream resources */
  bladerf_deinit_stream(_stream);

  /* Close the device */
  bladerf_close( _dev );

  delete[] _filled;
}

void *bladerf_sink_c::stream_callback( struct bladerf *dev,
                                       struct bladerf_stream *stream,
                                       struct bladerf_metadata *metadata,
                                       void *samples,
                                       size_t num_samples,
                                       void *user_data )
{
  bladerf_sink_c *obj = (bladerf_sink_c *) user_data;
  return obj->get_next_buffer( samples, num_samples );
}

static size_t buffer2index(void **buffers, void *current, size_t num_buffers)
{
  for (size_t i = 0; i < num_buffers; ++i) {
    if (static_cast<char*>(current) == static_cast<char*>(buffers[i]))
      return i;
  }

  throw std::runtime_error( std::string(__FUNCTION__) + " " +
                            "Has hit unexpected condition");
}

/* Fetch the next full buffer to pass down to the device */
void *bladerf_sink_c::get_next_buffer( void *samples, size_t num_samples)
{
  void *ret;
  bool running;

  {
    boost::unique_lock<boost::mutex> lock(_buf_status_lock);

    /* Mark the incoming buffer empty and notify work() */
    if (samples) {
      size_t buffer_emptied_index = buffer2index(_buffers, samples, _num_buffers);

      _filled[buffer_emptied_index] = false;
      _buffer_emptied.notify_one();
    }

    /* Wait for our next buffer to become filled */
    while ((running = is_running()) && !_filled[_next_to_tx]) {
      _samp_avail.wait(lock);
    }

    if (running) {
      ret = _buffers[_next_to_tx];
      _next_to_tx = (_next_to_tx + 1) % _num_buffers;
    } else {
      ret = NULL;
    }
  }

  return ret;
}

void bladerf_sink_c::write_task()
{
  int status;

  /* Start stream and stay there until we kill the stream */
  status = bladerf_stream(_stream, BLADERF_MODULE_TX);

  if (status < 0)
      std::cerr << "Sink stream error: " << bladerf_strerror(status) << std::endl;

  set_running( false );
}

int bladerf_sink_c::work( int noutput_items,
                          gr_vector_const_void_star &input_items,
                          gr_vector_void_star &output_items )
{
  const gr_complex *in = (const gr_complex *) input_items[0];
  int num_samples, to_copy;
  bool running = is_running();

  /* Total samples we want to process */
  num_samples = noutput_items;

  /* While there are still samples to copy out ... */
  while (running && num_samples > 0) {

    while (_samples_left && num_samples) {

      /* Scale and sign extend I and then Q */
      *_next_value = (int16_t)(real(*in) * 2000);
      _next_value++;

      *_next_value = (int16_t)(imag(*in) * 2000);
      _next_value++;

      /* Advance to next sample */
      in++;
      num_samples--;
      _samples_left--;
    }

    /* Advance to the next buffer if the current one is filled */
    if (_samples_left == 0) {
      {
        boost::unique_lock<boost::mutex> lock(_buf_status_lock);

        _filled[_buf_index] = true;
        _buf_index = (_buf_index + 1) % _num_buffers;
        _next_value = static_cast<int16_t*>(_buffers[_buf_index]);
        _samples_left = _samples_per_buffer;

        /* Signal that we have filled a buffer */
        _samp_avail.notify_one();

        /* Wait here if the next buffer isn't full. The callback will
         * signal us when it has freed up a buffer */
        while (_filled[_buf_index] && running) {
          _buffer_emptied.wait(lock);
          running = is_running();
        }
      }
    }
  }

  return running ? noutput_items : 0;
}

std::vector<std::string> bladerf_sink_c::get_devices()
{
  return bladerf_common::devices();
}

size_t bladerf_sink_c::get_num_channels()
{
  /* We only support a single channel for each bladeRF */
  return 1;
}

osmosdr::meta_range_t bladerf_sink_c::get_sample_rates()
{
  return sample_rates();
}

double bladerf_sink_c::set_sample_rate(double rate)
{
  int ret;
  uint32_t actual;
  /* Set the Si5338 to be 2x this sample rate */

  /* Check to see if the sample rate is an integer */
  if( (uint32_t)round(rate) == (uint32_t)rate )
  {
    ret = bladerf_set_sample_rate( _dev, BLADERF_MODULE_TX, (uint32_t)rate, &actual );
    if( ret ) {
      throw std::runtime_error( std::string(__FUNCTION__) + " " +
                                "has failed to set integer rate, error " +
                                boost::lexical_cast<std::string>(ret) );
    }
  } else {
    /* TODO: Fractional sample rate */
    ret = bladerf_set_sample_rate( _dev, BLADERF_MODULE_TX, (uint32_t)rate, &actual );
    if( ret ) {
      throw std::runtime_error( std::string(__FUNCTION__) + " " +
                                "has failed to set fractional rate, error " +
                                boost::lexical_cast<std::string>(ret) );
    }
  }

  return get_sample_rate();
}

double bladerf_sink_c::get_sample_rate()
{
  int ret;
  unsigned int rate = 0;

  ret = bladerf_get_sample_rate( _dev, BLADERF_MODULE_TX, &rate );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "has failed to get sample rate, error " +
                              boost::lexical_cast<std::string>(ret) );
  }

  return (double)rate;
}

osmosdr::freq_range_t bladerf_sink_c::get_freq_range( size_t chan )
{
  return freq_range();
}

double bladerf_sink_c::set_center_freq( double freq, size_t chan )
{
  int ret;

  /* Check frequency range */
  if( freq < get_freq_range( chan ).start() ||
      freq > get_freq_range( chan ).stop() ) {
    std::cerr << "Failed to set out of bound frequency: " << freq << std::endl;
  } else {
    ret = bladerf_set_frequency( _dev, BLADERF_MODULE_TX, (uint32_t)freq );
    if( ret ) {
      throw std::runtime_error( std::string(__FUNCTION__) + " " +
                                "failed to set center frequency " +
                                boost::lexical_cast<std::string>(freq) +
                                ", error " +
                                boost::lexical_cast<std::string>(ret) );
    }
  }

  return get_center_freq( chan );
}

double bladerf_sink_c::get_center_freq( size_t chan )
{
  uint32_t freq;
  int ret;

  ret = bladerf_get_frequency( _dev, BLADERF_MODULE_TX, &freq );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "failed to get center frequency, error " +
                              boost::lexical_cast<std::string>(ret) );
  }

  return (double)freq;
}

double bladerf_sink_c::set_freq_corr( double ppm, size_t chan )
{
  /* TODO: Write the VCTCXO with a correction value (also changes RX ppm value!) */
  return get_freq_corr( chan );
}

double bladerf_sink_c::get_freq_corr( size_t chan )
{
  /* TODO: Return back the frequency correction in ppm */
  return 0;
}

std::vector<std::string> bladerf_sink_c::get_gain_names( size_t chan )
{
  std::vector< std::string > names;

  names += "VGA1", "VGA2";

  return names;
}

osmosdr::gain_range_t bladerf_sink_c::get_gain_range( size_t chan )
{
  /* TODO: This is an overall system gain range. Given the VGA1 and VGA2
  how much total gain can we have in the system */
  return get_gain_range( "VGA2", chan ); /* we use only VGA2 here for now */
}

osmosdr::gain_range_t bladerf_sink_c::get_gain_range( const std::string & name, size_t chan )
{
  osmosdr::gain_range_t range;

  if( name == "VGA1" ) {
    range = _vga1_range;
  } else if( name == "VGA2" ) {
    range = _vga2_range;
  } else {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "requested an invalid gain element " + name );
  }

  return range;
}

bool bladerf_sink_c::set_gain_mode( bool automatic, size_t chan )
{
  return false;
}

bool bladerf_sink_c::get_gain_mode( size_t chan )
{
  return false;
}

double bladerf_sink_c::set_gain( double gain, size_t chan )
{
  return set_gain( gain, "VGA2", chan ); /* we use only VGA2 here for now */
}

double bladerf_sink_c::set_gain( double gain, const std::string & name, size_t chan)
{
  int ret = 0;

  if( name == "VGA1" ) {
    ret = bladerf_set_txvga1( _dev, (int)gain );
  } else if( name == "VGA2" ) {
    ret = bladerf_set_txvga2( _dev, (int)gain );
  } else {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "requested to set the gain "
                              "of an unknown gain element " + name );
  }

  /* Check for errors */
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set " + name + " gain, error " +
                              boost::lexical_cast<std::string>(ret) );
  }

  return get_gain( name, chan );
}

double bladerf_sink_c::get_gain( size_t chan )
{
  return get_gain( "VGA2", chan ); /* we use only VGA2 here for now */
}

double bladerf_sink_c::get_gain( const std::string & name, size_t chan )
{
  int g;
  int ret = 0;

  if( name == "VGA1" ) {
    ret = bladerf_get_txvga1( _dev, &g );
  } else if( name == "VGA2" ) {
    ret = bladerf_get_txvga2( _dev, &g );
  } else {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "requested to get the gain "
                              "of an unknown gain element " + name );
  }

  /* Check for errors */
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not get " + name + " gain, error " +
                              boost::lexical_cast<std::string>(ret) );
  }

  return (double)g;
}

double bladerf_sink_c::set_bb_gain( double gain, size_t chan )
{
  /* for TX, only VGA1 is in the BB path */
  osmosdr::gain_range_t bb_gains = get_gain_range( "VGA1", chan );

  double clip_gain = bb_gains.clip( gain, true );
  gain = set_gain( clip_gain, "VGA1", chan );

  return gain;
}

std::vector< std::string > bladerf_sink_c::get_antennas( size_t chan )
{
  std::vector< std::string > antennas;

  antennas += get_antenna( chan );

  return antennas;
}

std::string bladerf_sink_c::set_antenna( const std::string & antenna, size_t chan )
{
  return get_antenna( chan );
}

std::string bladerf_sink_c::get_antenna( size_t chan )
{
  /* We only have a single transmit antenna here */
  return "TX";
}

double bladerf_sink_c::set_bandwidth( double bandwidth, size_t chan )
{
  int ret;
  uint32_t actual;

  ret = bladerf_set_bandwidth( _dev, BLADERF_MODULE_TX, (uint32_t)bandwidth, &actual );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not set bandwidth, error " +
                              boost::lexical_cast<std::string>(ret) );
  }

  return get_bandwidth();
}

double bladerf_sink_c::get_bandwidth( size_t chan )
{
  uint32_t bandwidth;
  int ret;

  ret = bladerf_get_bandwidth( _dev, BLADERF_MODULE_TX, &bandwidth );
  if( ret ) {
    throw std::runtime_error( std::string(__FUNCTION__) + " " +
                              "could not get bandwidth, error " +
                              boost::lexical_cast<std::string>(ret) );
  }

  return (double)bandwidth;
}

osmosdr::freq_range_t bladerf_sink_c::get_bandwidth_range( size_t chan )
{
  return filter_bandwidths();
}
