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

#include "stdlib.h"

#include "ch.h"
#include "hal.h"
#include "halconf.h"
#include "test.h"
#include "shell.h"
#include "chprintf.h"

#include "rtcan.h"
#include "Middleware.hpp"
#include "topics.h"

#include "uid.h"

#define WA_SIZE_256B      THD_WA_SIZE(256)
#define WA_SIZE_512B      THD_WA_SIZE(512)
#define WA_SIZE_1K        THD_WA_SIZE(1024)

extern RTCANDriver RTCAND;

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

static void cmd_test(BaseSequentialStream *chp, int argc, char *argv[]) {
	Thread *tp;

	(void) argc;
	(void) argv;
	if (argc > 0) {
		chprintf(chp, "Usage: test\r\n");
		return;
	}
	tp = chThdCreateFromHeap(NULL, TEST_WA_SIZE, chThdGetPriority(), TestThread,
			chp);
	if (tp == NULL) {
		chprintf(chp, "out of memory\r\n");
		return;
	}
	chThdWait(tp);
}

static void cmd_reset(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;
	stm32_reset();
}

static void cmd_id(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) argc;
	(void) argv;
	chprintf(chp, "UID: %d\r\n", uid8());
}

static void cmd_srt(BaseSequentialStream *chp, int argc, char *argv[]) {
	rtcan_msg_t *msg_p;

	if ((argc < 1) || (argc > 4)) {
		chprintf(chp, "Usage: srt ID <size> <data> <period_in_ms>\r\n");
		return;
	}

	if ((argc > 1) && (atoi(argv[1]) > 64)) {
		chprintf(chp, "Maximum payload is 64 bytes\r\n");
		return;
	}

	if ((argc > 3) && (atoi(argv[3]) < 0)) {
		chprintf(chp, "Period not valid\r\n");
		return;
	}

	msg_p = (rtcan_msg_t *)chHeapAlloc(NULL, sizeof(rtcan_msg_t));
	msg_p->type = RTCAN_SRT;
	msg_p->callback = NULL;
	msg_p->status = RTCAN_MSG_READY;
	msg_p->id = atoi(argv[0]);

	if (argc > 1) {
		uint8_t *buffer_p;
		uint8_t *tmp_p;
		uint16_t i;

		msg_p->size = atoi(argv[1]);
		buffer_p = (uint8_t *)chHeapAlloc(NULL, msg_p->size);
		msg_p->data = buffer_p;
		msg_p->ptr = buffer_p;

		tmp_p = (uint8_t *) argv[2];

		for (i = 0; i < (msg_p->size); i++) {
			*(buffer_p++) = *(tmp_p++);
		}

	} else {
		msg_p->size = 0;
	}

	if ((argc != 4) || (*(argv[3]) == '0')) {
		rtcanSendSrt(msg_p, 100);

		while (msg_p->status != RTCAN_MSG_READY) {
			chThdYield();
		}
	} else {
		Thread *tp;
		rtcan_test_msg_t test_msg;

		test_msg.rtcan_p = &RTCAND;
		test_msg.msg_p = msg_p;
		test_msg.period = atoi(argv[3]);

		tp = chThdCreateFromHeap(NULL, WA_SIZE_256B, NORMALPRIO + 1, rtcanTxTestThread, &test_msg);

		if (tp == NULL) {
			chprintf(chp, "out of memory\r\n");
			return;
		}

		while (test_msg.msg_p != NULL) {
			chThdYield();
		}
	}
}

static const ShellCommand commands[] = { { "mem", cmd_mem }, { "threads",
		cmd_threads }, { "test", cmd_test }, { "reset", cmd_reset }, { "id",
		cmd_id }, { "srt", cmd_srt}, { NULL, NULL } };

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
 * RX & Publisher thread & callback.
 */

/*
Publisher<LEDDataDebug> pub("led23");

static void msg1_cb(rtcan_msg_t *msg_p) {
	LEDDataDebug* d;

	d = pub.alloc();
	if (d != NULL) {
		// FIXME
		d->pin = msg_p->data[0];
		d->set = msg_p->data[1];
		d->cnt = msg_p->data[5];
		pub.broadcast(d);
	}
}

static msg_t RxThread(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("rxnode");
	rtcan_msg_t msg;
	uint8_t data[sizeof(LEDDataDebug) - 2];

	chRegSetThreadName("RX THD");

	mw.newNode(&n);

	if (n.advertise(&pub)) {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led23 pub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led23 pub FAIL\r\n");
			mw.delNode(&n);
			return 0;
	}


	(void) arg;
	chRegSetThreadName("RX");

	msg.id = 999;
	msg.type = RTCAN_SRT;
	msg.callback = msg1_cb;
	msg.params = NULL;
	msg.size = sizeof(LEDDataDebug) - 2;
	msg.data = (uint8_t *) &data;
	msg.status = RTCAN_MSG_READY;

	rtcanRegister(&RTCAND, &msg);

	while (TRUE) {
		chThdSleepMilliseconds(500);
	}

	return 0;
}
*/
/*
 * Subscriber threads.
 */
static msg_t SubscriberThread1(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub1");
	Subscriber<LEDDataDebug, 5> sub("led23");
	LEDDataDebug *d;

	chRegSetThreadName("SUB THD #1");

	mw.newNode(&n);

	if (n.subscribe(&sub)) {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led23 sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led23 sub QUEUED\r\n");
	}

	while (TRUE) {
		n.spin();
		while ((d = sub.get()) != NULL) {
			palSetPad(TEST_GPIO, TEST1);
			if (d->set)
				palSetPad(LED_GPIO, d->pin);
			else
				palClearPad(LED_GPIO, d->pin);

			sub.release(d);
			palClearPad(TEST_GPIO, TEST1);
		}
	}

	return 0;
}

static msg_t SubscriberThread2(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub2");
	Subscriber<LEDDataDebug, 5> sub("led23");
	LEDDataDebug *d;
	int cnt = 0;

	chRegSetThreadName("SUB THD #2");

	mw.newNode(&n);

	if (n.subscribe(&sub)) {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led23 sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led23 sub QUEUED\r\n");
	}

	while (TRUE) {
		n.spin();
		while ((d = sub.get()) != NULL) {
			chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "pin(%d) = %d\r\n", d->pin, d->set);
			sub.release(d);
		}

	}

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
		switch (RTCAND.state) {
		case RTCAN_ERROR:
			palTogglePad(LED_GPIO, LED4);
			chThdSleepMilliseconds(200);
			break;
		default:
			palTogglePad(LED_GPIO, LED1);
			chThdSleepMilliseconds(500);
			break;
		}
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
	 * Creates the subscriber thread #1.
	 */
	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 1, SubscriberThread1, NULL);

	/*
	 * Creates the subscriber thread #2.
	 */
	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 1, SubscriberThread2, NULL);

	/*
	 * Creates the RX thread.
	 */
//	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 2, RxThread, NULL);
	Middleware & mw = Middleware::instance();
	RemotePublisher rpub("led23", sizeof(LEDDataDebug));
	mw.advertise(&rpub);

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
