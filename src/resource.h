#define IDR_MAINMENU 101
#define IDR_LICENSE_TXT 102
#define IDM_HELP_DOCUMENTATION 40001
#define IDM_HELP_LICENSE 40002
#define IDM_HELP_ABOUT 40003
#define IDM_VIEW_LOG 40004

/* Current version */
#define VER_VERSION 1, 4, 1, 2

#define VERSION_STR_EXPAND(x, y, z, w) #x "." #y "." #z "." #w "\0"
#define VERSION_STR(x) VERSION_STR_EXPAND(x)
#define VER_VERSION_STR VERSION_STR(VER_VERSION)
