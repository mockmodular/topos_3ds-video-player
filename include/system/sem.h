#if !defined(DEF_SEM_H)
#define DEF_SEM_H
#include <stdbool.h>
#include <stdint.h>

#define DEF_SEM_ENABLE_ICON
//#define DEF_SEM_ENABLE_NAME
#define DEF_SEM_ICON_PATH				(const char*)"romfs:/gfx/draw/icon/sem_icon.t3x"
#define DEF_SEM_NAME					(const char*)"Settings"

typedef uint8_t Sem_screen_mode;
/** 0=Auto (SBS per video res), 1=2D (400px top), 2=3D. Persisted in settings.txt <11>; schema in <8> disambiguates legacy values. */
#define DEF_SEM_SCREEN_MODE_AUTO		(Sem_screen_mode)(0)
#define DEF_SEM_SCREEN_MODE_400PX		(Sem_screen_mode)(1)
#define DEF_SEM_SCREEN_MODE_3D			(Sem_screen_mode)(2)
#define DEF_SEM_SCREEN_MODE_MAX			(Sem_screen_mode)(DEF_SEM_SCREEN_MODE_3D + 1)

typedef uint8_t Sem_wifi_signal;
#define DEF_SEM_WIFI_SIGNAL_BAD_WITH_INTERNET			(Sem_wifi_signal)(0)
#define DEF_SEM_WIFI_SIGNAL_NORMAL_WITH_INTERNET		(Sem_wifi_signal)(1)
#define DEF_SEM_WIFI_SIGNAL_GOOD_WITH_INTERNET			(Sem_wifi_signal)(2)
#define DEF_SEM_WIFI_SIGNAL_EXCELLENT_WITH_INTERNET		(Sem_wifi_signal)(3)
#define DEF_SEM_WIFI_SIGNAL_BAD_WITHOUT_INTERNET		(Sem_wifi_signal)(4)
#define DEF_SEM_WIFI_SIGNAL_NORMAL_WITHOUT_INTERNET		(Sem_wifi_signal)(5)
#define DEF_SEM_WIFI_SIGNAL_GOOD_WITHOUT_INTERNET		(Sem_wifi_signal)(6)
#define DEF_SEM_WIFI_SIGNAL_EXCELLENT_WITHOUT_INTERNET	(Sem_wifi_signal)(7)
#define DEF_SEM_WIFI_SIGNAL_DISABLED					(Sem_wifi_signal)(8)

typedef uint8_t Sem_model;
/** Stored console generation for fake mode & logic: 0 = old-gen (O3DS family), 1 = new-gen (N3DS family). */
#define DEF_SEM_MODEL_O3DS					(Sem_model)(0)
#define DEF_SEM_MODEL_N3DS					(Sem_model)(1)
#define DEF_SEM_MODEL_MAX					(Sem_model)(2)
#define DEF_SEM_MODEL_IS_NEW(model)			(bool)((model) == DEF_SEM_MODEL_N3DS)
/** Legacy identifiers — map onto the two generations above (XL / small not distinguished). */
#define DEF_SEM_MODEL_OLD3DS				DEF_SEM_MODEL_O3DS
#define DEF_SEM_MODEL_OLD3DSXL				DEF_SEM_MODEL_O3DS
#define DEF_SEM_MODEL_NEW3DS				DEF_SEM_MODEL_N3DS
#define DEF_SEM_MODEL_NEW3DSXL				DEF_SEM_MODEL_N3DS

/* 在线更新 / 录屏：工程内无对应实现；勿再保留易误导的 URL 或「已开启」宏。 */
#define DEF_SEM_ENABLE_UPDATER				/*(bool)(*/false/*)*/

typedef struct
{
	bool is_debug;					//Whether debug mode is enabled.
	bool is_night;					//Whether night mode is enabled.
	bool is_flash;					//Whether flash mode is enabled.
	bool is_eco;					//Whether eco mode is enabled.
	bool is_wifi_on;				//Whether Wifi is enabled.
	bool is_top_lcd_on;				//Whether top is enabled.
	/* 底屏唯一开关：true=亮 false=息。硬件背光见 sem.c Sem_hw_config_thread；Select/空白/自动息屏只改此字段 */
	bool is_bottom_lcd_on;
	uint16_t time_to_turn_off_lcd;	//Screen timeout in seconds, 0 to disable it.
	uint16_t time_to_enter_sleep;	//Sleep timeout in seconds, 0 to disable it.
	double scroll_speed;			//Scroll sensitivity.
	char lang[8];					//Language.
	Sem_screen_mode screen_mode;	//Current screen mode.
} Sem_config;

typedef struct
{
	uint16_t years;			//Year, e.g. 2011.
	uint8_t months;			//Month, e.g. 2.
	uint8_t days;			//Day, e.g. 26.
	uint8_t hours;			//Hour, e.g. 23.
	uint8_t minutes;		//Minute, e.g. 59.
	uint8_t seconds;		//Second, e.g. 59.
} Sem_time;

typedef struct
{
	bool is_charging;				//Whether charger is active.
	uint8_t battery_level;			//Battery level in %.
	uint8_t battery_temp;			//Battery temperature in degrees celsius (℃).
	uint32_t free_ram;				//Free heap in bytes.
	uint32_t free_linear_ram;		//Free linear RAM in bytes.
	uint32_t num_of_launch;			//Number of application launches.
	double battery_voltage;			//Battery voltage in volts.
	char connected_wifi[33];		//Connected network (access point) name (empty string if not connected).
	char msg[128];					//Preformatted status message.
	Sem_wifi_signal wifi_signal;	//Wifi signal strength and whether connected to the Internet.
	Sem_model console_model;		/* N3DS 档=探测到 4 核可用，否则 O3DS 档（非 CFG） */
	Sem_time time;					//The time.
} Sem_state;

bool Sem_query_init_flag(void);

void Sem_get_config(Sem_config* config);

void Sem_set_config(Sem_config* new_config);

void Sem_get_state(Sem_state* state);

/** Fake model: 255=off（按 CPU 可用核数：4→N3DS 档否则 O3DS 档），0/1=强制 O/N 档。 */
uint8_t Sem_query_fake_model(void);
void Sem_set_fake_model(uint8_t fake_model);

/** Queue a log dump (same worker as sem Advanced “Dump logs”; ignored if one is already pending). */
void Sem_request_log_dump(void);

void Sem_init(void);

void Sem_exit(void);

#endif //!defined(DEF_SEM_H)
