/*
 ChibiOS/RT - Copyright (C) 2006,2007,2008,2009,2010,
 2011 Giovanni Di Sirio.

 This file is part of ChibiOS/RT.

 ChibiOS/RT is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 ChibiOS/RT is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ch.h"
#include "hal.h"
#include "halconf.h"
#include "shell.h"
#include "chprintf.h"

#include "rtcan.h"
#include "Middleware.hpp"
#include "topics.h"

#define WA_SIZE_256B      THD_WA_SIZE(256)
#define WA_SIZE_512B      THD_WA_SIZE(512)
#define WA_SIZE_1K        THD_WA_SIZE(1024)

static msg_t RTCANThread(void *arg);
static msg_t SerialThread(void *arg);

void stm32_reset(void) {

	chThdSleep(MS2ST(10));

	/* Ensure completion of memory access. */
	__DSB();

	/* Generate reset by setting VECTRESETK and SYSRESETREQ, keeping priority group unchanged.
	 * If only SYSRESETREQ used, no reset is triggered, discovered while debugging.
	 * If only VECTRESETK is used, if you want to read the source of the reset afterwards
	 * from (RCC->CSR & RCC_CSR_SFTRSTF),
	 * it won't be possible to see that it was a software-triggered reset.
	 * */

	SCB->AIRCR = ((0x5FA << SCB_AIRCR_VECTKEY_Pos)
			| (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) | SCB_AIRCR_VECTRESET_Msk
			| SCB_AIRCR_SYSRESETREQ_Msk);

	/* Ensure completion of memory access. */
	__DSB();

	/* Wait for reset. */
	while (1)
		;
}

/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/

#define SHELL_WA_SIZE   THD_WA_SIZE(4096)
#define TEST_WA_SIZE    THD_WA_SIZE(1024)

static void cmd_mem(BaseSequentialStream *chp, int argc, char *argv[]) {
	size_t n, size;

	(void) argv;
	if (argc > 0) {
		chprintf(chp, "Usage: mem\r\n");
		return;
	}
	n = chHeapStatus(NULL, &size);
	chprintf(chp, "core free memory : %u bytes\r\n", chCoreStatus());
	chprintf(chp, "heap fragments   : %u\r\n", n);
	chprintf(chp, "heap free total  : %u bytes\r\n", size);
}

static void cmd_threads(BaseSequentialStream *chp, int argc, char *argv[]) {
	static const char *states[] = { THD_STATE_NAMES };
	Thread *tp;

	(void) argv;
	if (argc > 0) {
		chprintf(chp, "Usage: threads\r\n");
		return;
	}
	chprintf(chp, "    addr    stack prio refs     state time\r\n");
	tp = chRegFirstThread();
	do {
		chprintf(chp, "%.8lx %.8lx %4lu %4lu %9s %lu\r\n", (uint32_t) tp,
				(uint32_t) tp->p_ctx.r13, (uint32_t) tp->p_prio,
				(uint32_t)(tp->p_refs - 1), states[tp->p_state],
				(uint32_t) tp->p_time);
		tp = chRegNextThread(tp);
	} while (tp != NULL);
}

static void cmd_reset(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;
	stm32_reset();
}

static void cmd_rtcan(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;

	chThdCreateFromHeap(NULL, WA_SIZE_512B, NORMALPRIO, RTCANThread, NULL);
}

static void cmd_serial(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;

	chThdCreateFromHeap(NULL, WA_SIZE_512B, NORMALPRIO, SerialThread, NULL);
}

static const ShellCommand commands[] = { { "mem", cmd_mem }, { "threads",
		cmd_threads }, { "reset", cmd_reset }, { "r", cmd_rtcan }, { "s", cmd_serial },
		{ NULL, NULL } };

static const ShellConfig shell_cfg1 = { (BaseSequentialStream *) &SERIAL_DRIVER,
		commands };

/*===========================================================================*/
/* PubSub test related.                                                      */
/*===========================================================================*/

struct LEDData: public BaseMessage {
	uint8_t pin;
	bool_t set;
};

Middleware<RTCAN> mw;

/*
 * Publisher threads.
 */
static msg_t PublisherThread1(void *arg) {
	Node n(&mw);
	Publisher < LEDData > pub("led23");
	LEDData *d;

	chRegSetThreadName("PUB THD #1");

	mw.newNode(&n);

	if (n.advertise(&pub)) {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER, "led23 pub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER, "led23 pub FAIL\r\n");
		mw.delNode(&n);
		return 0;
	}

	while (TRUE) {
		palSetPad(TEST_GPIO, TEST1);
		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED2;
			d->set = true;
			pub.broadcast(d);
		}
		palClearPad(TEST_GPIO, TEST1);

		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED3;
			d->set = false;
			pub.broadcast(d);
		}

		chThdSleepMilliseconds(500);

		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED2;
			d->set = false;
			pub.broadcast(d);
		}

		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED3;
			d->set = true;
			pub.broadcast(d);
		}

		chThdSleepMilliseconds(500);
	}

	return 0;
}

/*
 * Subscriber threads.
 */
static msg_t SubscriberThread1(void *arg) {
	Node n(&mw);
	Subscriber<LEDData, 5> sub("led23");
	LEDData *d;

	chRegSetThreadName("SUB THD #1");

	mw.newNode(&n);

	if (n.subscribe(&sub)) {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER, "led23 sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER,
				"led23 sub QUEUED\r\n");
	}

	while (TRUE) {
		n.spin();
		while ((d = sub.get()) != NULL) {
			if (d->set)
				palSetPad(LED_GPIO, d->pin);
			else
				palClearPad(LED_GPIO, d->pin);

			sub.release(d);
			palClearPad(TEST_GPIO, TEST2);
		}
	}
	return 0;
}

static msg_t TxThread(void *arg) {
	Node n(&mw);
	Subscriber<LEDData, 5> sub("led23");
	LEDData *d;

	rtcan_msg_t msg;

	(void) arg;
	chRegSetThreadName("TX THD");

	msg.id = LED23_ID;
	msg.type = RTCAN_SRT;
	msg.callback = NULL;
	// FIXME
	msg.size = sizeof(LEDData) - 2;
	msg.status = RTCAN_MSG_READY;

	mw.newNode(&n);

	if (n.subscribe(&sub)) {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER, "led23 sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER,
				"led23 sub QUEUED\r\n");
	}

	while (TRUE) {
		n.spin();
		while ((d = sub.get()) != NULL) {
			// FIXME
			msg.data = (((uint8_t *) d) + 2);
			rtcanSendSrt(&msg, 50);

			// FIXME
			while (msg.status != RTCAN_MSG_READY) {
				chThdYield();
			}

			sub.release(d);

		}
	}

	return 0;
}

static msg_t RTCANThread(void *arg) {
	Node n(&mw);
	RTCANSubscriber<LEDData, 5> sub("led23");

	(void) arg;
	chRegSetThreadName("RTCAN THD");

	mw.newNode(&n);

	if (n.subscribe(&sub)) {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER,
				"led23 remote sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER,
				"led23 remote sub QUEUED\r\n");
	}

	while (TRUE) {
		chThdSleepMilliseconds(100);
	}

	return 0;
}

static msg_t SerialThread(void *arg) {
/*
	Node n(&mw);
	SerialSubscriber<LEDData, 5> sub("led23");

	(void) arg;
	chRegSetThreadName("SERIAL THD");

	mw.newNode(&n);

	if (n.subscribe(&sub)) {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER,
				"led23 remote sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*) &SERIAL_DRIVER,
				"led23 remote sub QUEUED\r\n");
	}

	while (TRUE) {
		chThdSleepMilliseconds(100);
	}
*/
	return 0;
}

/*===========================================================================*/
/* Application threads.                                                      */
/*===========================================================================*/

/*
 * Red LED blinker thread, times are in milliseconds.
 */
static WORKING_AREA(waThread1, 128);
static msg_t Thread1(void *arg) {

	(void) arg;
	chRegSetThreadName("blinker");

	while (TRUE) {
		palTogglePad(LED_GPIO, LED1);
		chThdSleepMilliseconds(500);
	}

	return 0;
}

/*
 * Application entry point.
 */
int main(void) {
	Thread *shelltp = NULL;

	/*
	 * System initializations.
	 * - HAL initialization, this also initializes the configured device drivers
	 *   and performs the board-specific initializations.
	 * - Kernel initialization, the main() function becomes a thread and the
	 *   RTOS is active.
	 */
	halInit();
	chSysInit();

	/*
	 * Activates the serial driver 1 using the driver default configuration.
	 */
	sdStart(&SERIAL_DRIVER, NULL);

	/*
	 * Shell manager initialization.
	 */
	shellInit();

	rtcanInit();
	rtcanStart (NULL);

	/*
	 * Creates the blinker thread.
	 */
	chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);

	/*
	 * Creates the publisher thread #1.
	 */
	chThdCreateFromHeap(NULL, WA_SIZE_512B, NORMALPRIO + 2, PublisherThread1,
			NULL);

	/*
	 * Creates the subscriber thread #1.
	 */
	chThdCreateFromHeap(NULL, WA_SIZE_512B, NORMALPRIO + 1, SubscriberThread1,
			NULL);

	/*
	 * Creates the TX thread.
	 */
//	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO, TxThread, NULL);
	/*
	 * Normal main() thread activity, in this demo it does nothing except
	 * sleeping in a loop and check the button state.
	 */
	while (TRUE) {
		if (!shelltp)
			shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO - 1);
		else if (chThdTerminated(shelltp)) {
			chThdRelease(shelltp);
			shelltp = NULL;
		}
		chThdSleepMilliseconds(200);
	}
}
