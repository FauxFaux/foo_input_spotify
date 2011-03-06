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
#include "../ATLHelpers/ATLHelpers.h"

#include "kdmeng.h"

#include "resource.h"

// {C14DBCDB-676E-4348-93DA-1BAB92A98727}
static const GUID guid_cfg_loop_count = 
{ 0xc14dbcdb, 0x676e, 0x4348, { 0x93, 0xda, 0x1b, 0xab, 0x92, 0xa9, 0x87, 0x27 } };

// {CDBDF0A4-3333-42EB-A445-DD1D423E9AA9}
static const GUID guid_cfg_sample_rate = 
{ 0xcdbdf0a4, 0x3333, 0x42eb, { 0xa4, 0x45, 0xdd, 0x1d, 0x42, 0x3e, 0x9a, 0xa9 } };

// {B35C089B-3285-48CC-94CF-461B520E5328}
static const GUID guid_cfg_history_rate = 
{ 0xb35c089b, 0x3285, 0x48cc, { 0x94, 0xcf, 0x46, 0x1b, 0x52, 0xe, 0x53, 0x28 } };

enum
{
	default_cfg_loop_count = 2,
	default_cfg_sample_rate = 44100
};

static cfg_int cfg_loop_count( guid_cfg_loop_count, default_cfg_loop_count );
static cfg_int cfg_sample_rate( guid_cfg_sample_rate, default_cfg_sample_rate );

class input_kdm
{
	kdmeng * m_player;

	t_filestats m_stats;

	unsigned srate, loop_count;

	long length;

	bool first_block, eof;

	pfc::array_t< t_int16 > sample_buffer;

public:
	input_kdm()
	{
		m_player = 0;
	}

	~input_kdm()
	{
		delete m_player;
	}

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_data();

		input_open_file_helper( m_file, p_path, p_reason, p_abort );

		m_stats = m_file->get_stats( p_abort );

		srate = cfg_sample_rate;

		m_player = new kdmeng( srate, 2, 2 );

		length = m_player->load( m_file, p_path, p_abort );

		if ( !length ) throw exception_io_data();
	}

	void get_info( file_info & p_info, abort_callback & p_abort )
	{
		p_info.info_set( "codec", "KDM" );
		p_info.info_set_int( "channels", 2 );
		p_info.set_length( (double) length / 1000. );
	}

	t_filestats get_file_stats( abort_callback & p_abort )
	{
		return m_stats;
	}

	void decode_initialize( unsigned p_flags, abort_callback & p_abort )
	{
		loop_count = cfg_loop_count;
		if ( p_flags & input_flag_no_looping && !loop_count ) loop_count++;

		m_player->musicoff();
		m_player->musicon();

		first_block = true;
		eof = false;
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		if ( eof ) return false;

		unsigned samples_to_do = srate / 120;

		sample_buffer.grow_size( samples_to_do * 2 );

		long repeatcount = m_player->rendersound( sample_buffer.get_ptr(), samples_to_do * 4 );

		p_chunk.set_data_fixedpoint( sample_buffer.get_ptr(), samples_to_do * 4, srate, 2, 16, audio_chunk::channel_config_stereo );

		if ( loop_count && repeatcount >= loop_count ) eof = true;

		return true;
	}

	void decode_seek( double p_seconds,abort_callback & p_abort )
	{
		long seek_ms = audio_math::time_to_samples( p_seconds, 1000 );

		m_player->seek( seek_ms );

		first_block = true;
	}

	bool decode_can_seek()
	{
		return true;
	}

	bool decode_get_dynamic_info( file_info & p_out, double & p_timestamp_delta )
	{
		if ( first_block )
		{
			first_block = false;
			p_out.info_set_int( "samplerate", srate );
			return true;
		}
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
		return !stricmp( p_extension, "kdm" );
	}
};

static cfg_dropdown_history cfg_history_rate( guid_cfg_history_rate, 16 );

static const int srate_tab[]={8000,11025,16000,22050,24000,32000,44100,48000};

class CMyPreferences : public CDialogImpl<CMyPreferences>, public preferences_page_instance {
public:
	//Constructor - invoked by preferences_page_impl helpers - don't do Create() in here, preferences_page_impl does this for us
	CMyPreferences(preferences_page_callback::ptr callback) : m_callback(callback) {}

	//Note that we don't bother doing anything regarding destruction of our class.
	//The host ensures that our dialog is destroyed first, then the last reference to our preferences_page_instance object is released, causing our object to be deleted.


	//dialog resource ID
	enum {IDD = IDD_CONFIG};
	// preferences_page_instance methods (not all of them - get_wnd() is supplied by preferences_page_impl helpers)
	t_uint32 get_state();
	void apply();
	void reset();

	//WTL message map
	BEGIN_MSG_MAP(CMyPreferences)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_SAMPLERATE, CBN_EDITCHANGE, OnEditChange)
		COMMAND_HANDLER_EX(IDC_SAMPLERATE, CBN_SELCHANGE, OnSelectionChange)
		DROPDOWN_HISTORY_HANDLER(IDC_SAMPLERATE, cfg_history_rate)
	END_MSG_MAP()
private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnEditChange(UINT, int, CWindow);
	void OnSelectionChange(UINT, int, CWindow);
	bool HasChanged();
	void OnChanged();

	void enable_vgm_loop_count(BOOL);

	const preferences_page_callback::ptr m_callback;
};

BOOL CMyPreferences::OnInitDialog(CWindow, LPARAM) {
	CWindow w;
	char temp[16];
	int n;
	for(n=tabsize(srate_tab);n--;)
	{
		if (srate_tab[n] != cfg_sample_rate)
		{
			itoa(srate_tab[n], temp, 10);
			cfg_history_rate.add_item(temp);
		}
	}
	itoa(cfg_sample_rate, temp, 10);
	cfg_history_rate.add_item(temp);
	w = GetDlgItem( IDC_SAMPLERATE );
	cfg_history_rate.setup_dropdown( w );
	::SendMessage( w, CB_SETCURSEL, 0, 0 );

	w = GetDlgItem( IDC_LOOPCOUNT );
	uSendMessageText( w, CB_ADDSTRING, 0, "Indefinite" );
	for ( n = 1; n <= 10; n++ )
	{
		itoa( n, temp, 10 );
		uSendMessageText( w, CB_ADDSTRING, 0, temp );
	}
	::SendMessage( w, CB_SETCURSEL, cfg_loop_count, 0 );
	
	return FALSE;
}

void CMyPreferences::OnEditChange(UINT, int, CWindow) {
	OnChanged();
}

void CMyPreferences::OnSelectionChange(UINT, int, CWindow) {
	OnChanged();
}

t_uint32 CMyPreferences::get_state() {
	t_uint32 state = preferences_state::resettable;
	if (HasChanged()) state |= preferences_state::changed;
	return state;
}

void CMyPreferences::reset() {
	SetDlgItemInt( IDC_SAMPLERATE, default_cfg_sample_rate );
	SendDlgItemMessage( IDC_LOOPCOUNT, CB_SETCURSEL, default_cfg_loop_count );
	
	OnChanged();
}

void CMyPreferences::apply() {
	char temp[16];
	int t = GetDlgItemInt( IDC_SAMPLERATE, NULL, FALSE );
	if ( t < 6000 ) t = 6000;
	else if ( t > 96000 ) t = 96000;
	SetDlgItemInt( IDC_SAMPLERATE, t, FALSE );
	itoa( t, temp, 10 );
	cfg_history_rate.add_item( temp );
	cfg_sample_rate = t;

	cfg_loop_count = SendDlgItemMessage( IDC_LOOPCOUNT, CB_GETCURSEL );
	
	OnChanged(); //our dialog content has not changed but the flags have - our currently shown values now match the settings so the apply button can be disabled
}

bool CMyPreferences::HasChanged() {
	return GetDlgItemInt( IDC_SAMPLERATE, NULL, FALSE ) != cfg_sample_rate
		|| SendDlgItemMessage( IDC_LOOPCOUNT, CB_GETCURSEL ) != cfg_loop_count;
}
void CMyPreferences::OnChanged() {
	//tell the host that our state has changed to enable/disable the apply button appropriately.
	m_callback->on_state_changed();
}

class preferences_page_myimpl : public preferences_page_impl<CMyPreferences> {
	// preferences_page_impl<> helper deals with instantiation of our dialog; inherits from preferences_page_v3.
public:
	const char * get_name() {return "KDM Decoder";}
	GUID get_guid() {
		// {518B5BE8-34DD-4ADF-822C-EB36085537F6}
		static const GUID guid = { 0x518b5be8, 0x34dd, 0x4adf, { 0x82, 0x2c, 0xeb, 0x36, 0x8, 0x55, 0x37, 0xf6 } };
		return guid;
	}
	GUID get_parent_guid() {return guid_input;}
};

static input_singletrack_factory_t< input_kdm >             g_input_factory_kdm;
static preferences_page_factory_t <preferences_page_myimpl> g_config_dsdiff_factory;

DECLARE_FILE_TYPE("KDM Files", "*.kdm");

DECLARE_COMPONENT_VERSION("KDM Decoder", MYVERSION, "Decodes Ken Silverman's KDM music files.");

VALIDATE_COMPONENT_FILENAME("foo_input_kdm.dll");
