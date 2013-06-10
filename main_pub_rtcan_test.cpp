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
#include "shell.h"
#include "chprintf.h"
#include "board.h"

#include "rtcan.h"
#include "Middleware.hpp"
#include "topics.h"

#include "uid.h"

#include "hrt.h"

#define WA_SIZE_256B      THD_WA_SIZE(256)
#define WA_SIZE_512B      THD_WA_SIZE(512)
#define WA_SIZE_1K        THD_WA_SIZE(1024)
#define WA_SIZE_2K        THD_WA_SIZE(2048)

void remote_sub(const char * topic);

void stm32_reset(void) {

	chThdSleep(MS2ST(10) );

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
				(uint32_t) (tp->p_refs - 1), states[tp->p_state],
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

static void cmd_remote(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;
	remote_sub("led23");
}

static const ShellCommand commands[] = { { "mem", cmd_mem }, { "threads",
		cmd_threads }, { "reset", cmd_reset }, { "r", cmd_remote },
		{ NULL, NULL } };

static const ShellConfig shell_cfg1 = { (BaseSequentialStream *) &SERIAL_DRIVER,
		commands };

/*===========================================================================*/
/* PubSub test related.                                                      */
/*===========================================================================*/

struct LEDData: public BaseMessage {
	uint8_t pin;
	bool_t set;
}__attribute__((packed));

struct LEDDataDebug: public BaseMessage {
	uint8_t pin;
	bool_t set;
	uint8_t cnt;
}__attribute__((packed));

/*
 * Publisher threads.
 */
static msg_t PublisherThread1(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("pub1");
	Publisher<LEDDataDebug> pub("led23");
	LEDDataDebug *d;
	uint8_t cnt = 0;
	uint32_t nd;

	(void) arg;
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
		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED2;
			d->set = true;
			d->cnt = cnt++;
			nd = pub.broadcast(d);
			chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "A: %x %d\r\n", d,
					nd);
		}

		chThdSleepMilliseconds(1);

		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED3;
			d->set = false;
			d->cnt = cnt++;
			nd = pub.broadcast(d);
			chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "B: %x %d\r\n", d,
					nd);
		}

		chThdSleepMilliseconds(1);

		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED2;
			d->set = false;
			d->cnt = cnt++;
			nd = pub.broadcast(d);
			chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "C: %x %d\r\n", d,
					nd);
		}

		chThdSleepMilliseconds(1);

		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED3;
			d->set = true;
			d->cnt = cnt++;
			nd = pub.broadcast(d);
			chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "D: %x %d\r\n", d,
					nd);
		}

		chThdSleepMilliseconds(1);
	}

	return 0;
}

/*
 * Subscriber threads.
 */
static msg_t SubscriberThread1(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub1");
	Subscriber<LEDDataDebug, 5> sub("led23");
	LEDDataDebug *d;

	(void) arg;
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

// FIXME
RemoteSubscriberT<LEDDataDebug, 5> rsub("led23");

void remote_sub(const char * topic) {
	Middleware & mw = Middleware::instance();
	LocalPublisher * pub;

	pub = mw.findLocalPublisher(topic);

	if (pub) {
		rsub.id(999);
		rsub.subscribe(pub);
	}
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
	rtcanStart(NULL);

	/*
	 * Creates the blinker thread.
	 */
	chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);

	/*
	 * Creates the publisher thread #1.
	 */
	chThdCreateFromHeap(NULL, WA_SIZE_2K, NORMALPRIO + 2, PublisherThread1,
			NULL);

	/*
	 * Creates the subscriber thread #1.
	 */
	chThdCreateFromHeap(NULL, WA_SIZE_2K, NORMALPRIO + 1, SubscriberThread1,
			NULL);

	chThdSleepMilliseconds(100);

	/*
	 * Remote subscriber.
	 */
	remote_sub("led23");

	chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "UID8: %d\r\n", uid8());
	chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "sizeof(LEDData): %d",
			sizeof(LEDData));
	chprintf((BaseSequentialStream *) &SERIAL_DRIVER,
			"sizeof(LEDDataDebug): %d", sizeof(LEDDataDebug));

	chprintf((BaseSequentialStream *) &SERIAL_DRIVER,
			"sizeof(rtcan_msg_t): %d", sizeof(rtcan_msg_t));


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

		chThdSleepMilliseconds(100);
	}
}
