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

#include "ch.hpp"

/*#include "rtcan.h"*/

#include "Middleware.hpp"

#define WA_SIZE_256B      THD_WA_SIZE(256)
#define WA_SIZE_512B      THD_WA_SIZE(512)
#define WA_SIZE_1K        THD_WA_SIZE(1024)

static msg_t PublisherThread2(void *arg);
static msg_t SubscriberThread2(void *arg);
static msg_t SubscriberThread3(void *arg);

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
#define WA_SIZE_1K    THD_WA_SIZE(1024)

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

static void cmd_pub2(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;
	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO, PublisherThread2, NULL);
}

static void cmd_sub2(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;
	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO, SubscriberThread2, NULL);
}

static void cmd_sub3(BaseSequentialStream *chp, int argc, char *argv[]) {

	(void) chp;
	(void) argc;
	(void) argv;
	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO, SubscriberThread3, NULL);
}

static const ShellCommand commands[] =
		{ { "mem", cmd_mem }, { "threads", cmd_threads },
				{ "reset", cmd_reset }, { "p2", cmd_pub2 }, { "s2", cmd_sub2 }, { "s3", cmd_sub3 }, { NULL, NULL } };

static const ShellConfig shell_cfg1 = { (BaseSequentialStream *) &SERIAL_DRIVER,
		commands };

/*===========================================================================*/
/* PubSub test related.                                                      */
/*===========================================================================*/

struct LEDData: public BaseMessage {
	uint8_t pin;
	uint8_t set;
}__attribute__((packed));

/*
 * Publisher threads.
 */
static msg_t PublisherThread1(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("pub1");
	Publisher<LEDData> pub1("led2");
	Publisher<LEDData> pub2("led3");
	LEDData *d;

	(void) arg;
	chRegSetThreadName("PUB THD #1");

	mw.newNode(&n);

	if (n.advertise(&pub1)) {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led2 pub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led2 pub FAIL\r\n");
	}

	if (n.advertise(&pub2)) {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led3 pub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led3 pub FAIL\r\n");
	}

	while (TRUE) {

		d = pub1.alloc();
		if (d != NULL) {
			d->pin = LED2;
			d->set = true;
			pub1.broadcast(d);
		}

		d = pub2.alloc();
		if (d != NULL) {
			d->pin = LED3;
			d->set = false;
			pub2.broadcast(d);
		}

		chThdSleepMilliseconds(500);

		d = pub1.alloc();
		if (d != NULL) {
			d->pin = LED2;
			d->set = false;
			pub1.broadcast(d);
		}

		d = pub2.alloc();
		if (d != NULL) {
			d->pin = LED3;
			d->set = true;
			pub2.broadcast(d);
		}

		chThdSleepMilliseconds(500);
	}

	return 0;
}

static msg_t PublisherThread2(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("pub2");
	Publisher<LEDData> pub("led4");
	LEDData *d;
	systime_t time;

	(void) arg;
	chRegSetThreadName("PUB #2");

	mw.newNode(&n);

	if (n.advertise(&pub)) {
		chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "led4 pub OK\r\n");
	} else {
		chprintf((BaseSequentialStream *) &SERIAL_DRIVER, "led4 pub FAIL\r\n");
	}

	time = chTimeNow();
	while (TRUE) {
		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED4;
			d->set = true;
			pub.broadcast(d);
		}

		time += MS2ST(10);
		chThdSleepUntil(time);

		d = pub.alloc();
		if (d != NULL) {
			d->pin = LED4;
			d->set = false;
			pub.broadcast(d);
		}

		time += MS2ST(10);
		chThdSleepUntil(time);
	}

	return 0;
}

/*
 * Subscriber threads.
 */

void cb_led2(BaseMessage * msg) {
	LEDData * d = (LEDData *) msg;

	if (d->set)
		palSetPad(LED_GPIO, d->pin);
	else
		palClearPad(LED_GPIO, d->pin);
}

void cb_led3(BaseMessage * msg) {
	LEDData * d = (LEDData *) msg;

	if (d->set)
		palSetPad(LED_GPIO, d->pin);
	else
		palClearPad(LED_GPIO, d->pin);
}

static msg_t SubscriberThread1(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub1");
	Subscriber<LEDData, 5> sub1("led2", cb_led2);
	Subscriber<LEDData, 5> sub2("led3", cb_led3);

	(void) arg;
	chRegSetThreadName("SUB THD #1");

	mw.newNode(&n);

	if (n.subscribe(&sub1)) {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led2 sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led2 sub QUEUED\r\n");
	}

	if (n.subscribe(&sub2)) {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led3 sub OK\r\n");
	} else {
		chprintf((BaseSequentialStream*)&SERIAL_DRIVER, "led3 sub QUEUED\r\n");
	}

	while (TRUE) {
		n.spin();
	}


	return 0;
}



static msg_t SubscriberThread2(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub2");
	Subscriber<LEDData, 2> sub("led4");
	LEDData *d;

	(void) arg;
	chRegSetThreadName("SUB #2");

	mw.newNode(&n);

	n.subscribe(&sub);

	while (TRUE) {
		n.spin();
		if ((d = sub.get()) != NULL) {
			if (d->set)
				palSetPad(LED_GPIO, d->pin);
			else
				palClearPad(LED_GPIO, d->pin);

			sub.release(d);
		}
	}


	return 0;
}

static msg_t SubscriberThread3(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub2");
	Subscriber<LEDData, 2> sub("led4");
	LEDData *d;

	(void) arg;
	chRegSetThreadName("SUB #3");

	mw.newNode(&n);
	n.subscribe(&sub);

	while (TRUE) {
		n.spin();
		if ((d = sub.get()) != NULL) {
			palTogglePad(LED_GPIO, LED3);
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
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "core free memory : %u bytes\r\n", chCoreStatus());

	/*
	 * Shell manager initialization.
	 */
	shellInit();

/*	rtcanInit();
	rtcanStart (NULL);*/

	/*
	 * Creates the blinker thread.
	 */
	chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, Thread1, NULL);


	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "\r\n");
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(LEDData) %d\r\n", sizeof(LEDData));


	Node n("test");
	Publisher<LEDData> pub("test");
	Subscriber<LEDData, 1> sub("test");
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(Middleware) %d\r\n", sizeof(Middleware));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(Node) %d\r\n", sizeof(n));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(BasePublisher) %d\r\n", sizeof(BasePublisher));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(BaseSubscriber) %d\r\n", sizeof(BaseSubscriber));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(LocalPublisher) %d\r\n", sizeof(LocalPublisher));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(LocalSubscriber) %d\r\n", sizeof(LocalSubscriber));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(RemotePublisher) %d\r\n", sizeof(RemotePublisher));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(RemoteSubscriber) %d\r\n", sizeof(RemoteSubscriber));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(Publisher<LEDData>) %d\r\n", sizeof(pub));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(Subscriber<LEDData, 1>) %d\r\n", sizeof(sub));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(Mailbox) %d\r\n", sizeof(Mailbox));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(MemoryPool) %d\r\n", sizeof(MemoryPool));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(Thread) %d\r\n", sizeof(Thread));
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(BaseThread) %d\r\n", sizeof(chibios_rt::BaseThread));

	chThdSleepMilliseconds(500);

	/*
	 * Creates the publisher thread #1.
	 */
	chThdCreateFromHeap (NULL, WA_SIZE_1K, NORMALPRIO, PublisherThread1, NULL);

	/*
	 * Creates the subscriber thread #1.
	 */
	chThdCreateFromHeap (NULL, WA_SIZE_1K, NORMALPRIO, SubscriberThread1, NULL);

	/*
	 * Creates the subscriber thread #2.
	 */
	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 5, SubscriberThread2, NULL);

	chThdSleepMilliseconds(200);

	/*
	 * Creates the publisher thread #2.
	 */
	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 1, PublisherThread2, NULL);

	chThdSleepMilliseconds(200);

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
