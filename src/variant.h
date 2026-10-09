#pragma once
// Edition switches.  The public edition is the default; -DINTRANET_BUILD produces the intranet edition.
// The two editions use different file names, task name, window class and single-instance mutex so both
// can be installed on the same computer without sharing settings or blocking each other.
#ifdef INTRANET_BUILD
#define V_TITLE_SUFFIX  L" 内网版"
#define V_FILE_STEM     L"NTPSyncIntranet"
#define V_TASK_NAME     L"NTPTimeSyncToolIntranet"
#define V_WND_CLASS     L"NTPTimeSyncToolIntranetWnd"
#define V_MUTEX_NAME    L"Local\\NTPTimeSyncToolIntranet_SingleInstance"
#else
#define V_TITLE_SUFFIX  L""
#define V_FILE_STEM     L"NTPSync"
#define V_TASK_NAME     L"NTPTimeSyncTool"
#define V_WND_CLASS     L"NTPTimeSyncToolWnd"
#define V_MUTEX_NAME    L"Local\\NTPTimeSyncTool_SingleInstance"
#endif
