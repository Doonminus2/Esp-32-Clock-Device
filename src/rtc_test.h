#pragma once

// TEMPORARY hardware check for the DS1302 RTC. Never returns.
// Remove the call from main.c once the RTC works.
void rtc_smoke_test(void);