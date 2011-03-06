#define MYVERSION "1.0"

/*
	changelog

2010-06-20 07:05 UTC - kode54
- Initial release
- Version is now 1.0

*/

#define _WIN32_WINNT 0x0501

#include <foobar2000.h>

#include "../helpers/dropdown_helper.h"

class input_kdm
{
	t_filestats m_stats;

	pfc::array_t< t_int16 > sample_buffer;

public:
	input_kdm()
	{
	}

	~input_kdm()
	{
	}

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_data();
	}

	void get_info( file_info & p_info, abort_callback & p_abort )
	{
		p_info.set_length(1337);
	}

	t_filestats get_file_stats( abort_callback & p_abort )
	{
		return m_stats;
	}

	void decode_initialize( unsigned p_flags, abort_callback & p_abort )
	{
		//loop_count = cfg_loop_count;
		//if ( p_flags & input_flag_no_looping && !loop_count ) loop_count++;

		//m_player->musicoff();
		//m_player->musicon();

		//first_block = true;
		//eof = false;
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		//if ( eof ) return false;

		//unsigned samples_to_do = srate / 120;

		//sample_buffer.grow_size( samples_to_do * 2 );

		//long repeatcount = m_player->rendersound( sample_buffer.get_ptr(), samples_to_do * 4 );

		//p_chunk.set_data_fixedpoint( sample_buffer.get_ptr(), samples_to_do * 4, srate, 2, 16, audio_chunk::channel_config_stereo );

		//if ( loop_count && repeatcount >= loop_count ) eof = true;

		return true;
	}

	void decode_seek( double p_seconds,abort_callback & p_abort )
	{
		long seek_ms = audio_math::time_to_samples( p_seconds, 1000 );

		//m_player->seek( seek_ms );

		//first_block = true;
	}

	bool decode_can_seek()
	{
		return true;
	}

	bool decode_get_dynamic_info( file_info & p_out, double & p_timestamp_delta )
	{
		//if ( first_block )
		//{
		//	first_block = false;
		//	p_out.info_set_int( "samplerate", srate );
		//	return true;
		//}
		return false;
	}

	bool decode_get_dynamic_info_track( file_info & p_out, double & p_timestamp_delta )
	{
		return false;
	}

	void decode_on_idle( abort_callback & p_abort ) { }

	void retag( const file_info & p_info, abort_callback & p_abort )
	{
		throw exception_io_data();
	}

	static bool g_is_our_content_type( const char * p_content_type )
	{
		return false;
	}

	static bool g_is_our_path( const char * p_full_path, const char * p_extension )
	{
		return !strncmp( p_full_path, "spotify:", strlen("spotify:") );
	}
};

static input_singletrack_factory_t< input_kdm >             g_input_factory_kdm;

DECLARE_COMPONENT_VERSION("Spotify Decoder", MYVERSION, "Support for spotify: urls.");
