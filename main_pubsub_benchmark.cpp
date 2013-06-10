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
#include "board.h"
#include "chprintf.h"

#include "ch.hpp"

/*#include "rtcan.h"*/

#include "Middleware.hpp"

#define MAX_SUBSCRIBERS 20
#define BIG 0
#define REMOTE 0
#define VERBOSE 1

#define WA_SIZE_256B      THD_WA_SIZE(256)
#define WA_SIZE_512B      THD_WA_SIZE(512)
#define WA_SIZE_1K        THD_WA_SIZE(1024)


int subscribers = 0;
uint32_t cnt = 0;
systime_t start_time = 0;
systime_t end_time = 0;

Thread * pubtp = NULL;
Thread * subtp[MAX_SUBSCRIBERS] = {NULL};

/*
 * Messages.
 */

struct TestData: public BaseMessage {
	uint32_t cnt;
#if BIG
	uint8_t dummy[124];
#endif /* BIG */
}__attribute__((packed));

/*
 * Publisher threads.
 */

static msg_t PublisherThread100Hz(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("pub1");
	Publisher<TestData> pub("test");
	TestData *msg;
	systime_t time;

	(void) arg;
	chRegSetThreadName("PUB 100Hz");
#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Publisher - 100Hz\r\n");
#endif /* VERBOSE */

	mw.newNode(&n);
	n.advertise(&pub);

	time = chTimeNow();
	while (TRUE) {
		palSetPad(TEST_GPIO, TEST2);
		msg = pub.alloc();
		if (msg != NULL) {
			msg->cnt = cnt++;
			pub.broadcast(msg);
		}

		time += MS2ST(10);
		chThdSleepUntil(time);

		palSetPad(TEST_GPIO, TEST2);
		msg = pub.alloc();
		if (msg != NULL) {
			msg->cnt = cnt++;
			pub.broadcast(msg);
		}

		time += MS2ST(10);
		chThdSleepUntil(time);
	}

	mw.delNode(&n);
	chThdExit(RDY_OK);

	return 0;
}

static msg_t PublisherThreadFlood(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("pub2");
	Publisher<TestData> pub("test");
	TestData *msg;
	uint32_t * nmsg = (uint32_t *)arg;

	(void) arg;
	chRegSetThreadName("PUB FLOOD");
#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Publisher - Flood\r\n");
#endif /* VERBOSE */

	mw.newNode(&n);
	n.advertise(&pub);

	start_time = chTimeNow();

	while (cnt < *nmsg) {
		palSetPad(TEST_GPIO, TEST2);
		msg = pub.alloc();
		if (msg != NULL) {
			msg->cnt = cnt++;
			pub.broadcast(msg);
		}

		chThdYield();

		palSetPad(TEST_GPIO, TEST2);
		msg = pub.alloc();
		if (msg != NULL) {
			msg->cnt = cnt++;
			pub.broadcast(msg);
		}
		chThdYield();
	}

	end_time = chTimeNow();

	mw.delNode(&n);
	chThdExit(RDY_OK);

	return 0;
}

/*
 * Subscriber threads.
 */
static msg_t SubscriberThreadRT(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub1");
	Subscriber<TestData, 5> sub("test");
	TestData *d;
	int nsub = ++subscribers;
	uint16_t cnt = 0;

	(void) arg;
	chRegSetThreadName("SUB RT");
#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Subscriber #%d - RT\r\n", nsub);
#endif /* VERBOSE */

	mw.newNode(&n);
	n.subscribe(&sub);

	while (!chThdShouldTerminate()) {
		n.spin();
		palClearPad(TEST_GPIO, TEST2);
		if ((d = sub.get()) != NULL) {
//			if (cnt != 0 && (d->cnt - cnt) != 1) {
//				palTogglePad(LED_GPIO, LED4);
//			}
//			cnt = d->cnt;
			sub.release(d);
		}
	}

	mw.delNode(&n);
	chThdExit(RDY_OK);

	return 0;
}

static msg_t SubscriberThread10Hz(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub1");
	Subscriber<TestData, 2> sub("test");
	TestData *d;
	int nsub = ++subscribers;
	int nmsg = 0;

	(void) arg;
	chRegSetThreadName("SUB 10Hz");
#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Subscriber #%2d - 10Hz\r\n", nsub);
#endif /* VERBOSE */

	mw.newNode(&n);
	n.subscribe(&sub);

	while (!chThdShouldTerminate()) {
		n.spin();
		while ((d = sub.get()) != NULL) {
			nmsg++;
//			chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "%2d %5d %d\r\n", nsub, d->cnt, chTimeNow());
			sub.release(d);
		}
		chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "%2d %d\r\n", nsub, nmsg);
		nmsg = 0;
		chThdSleepMilliseconds(100);
	}

	mw.delNode(&n);
	chThdExit(RDY_OK);

	return 0;
}

static msg_t SubscriberThread1Hz(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub1");
	Subscriber<TestData, 2> sub("test");
	TestData *d;
	int nsub = ++subscribers;
	int nmsg = 0;

	(void) arg;
	chRegSetThreadName("SUB 1Hz");
#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Subscriber #%d - 1Hz\r\n", nsub);
#endif /* VERBOSE */

	mw.newNode(&n);
	n.subscribe(&sub);

	while (!chThdShouldTerminate()) {
		n.spin();
		while ((d = sub.get()) != NULL) {
//			chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "%2d %5d %d\r\n", nsub, d->cnt, chTimeNow());
			sub.release(d);
			nmsg++;
		}
		chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "%2d %d\r\n", nsub, nmsg);
		nmsg = 0;
		chThdSleepMilliseconds(10);
	}

	mw.delNode(&n);
	chThdExit(RDY_OK);

	return 0;
}

static msg_t SubscriberThreadRT2(void *arg) {
	Middleware & mw = Middleware::instance();
	Node n("sub1");
	Subscriber<TestData, 5> sub("test");
	TestData *d;
	int nsub = ++subscribers;
	uint16_t cnt = 0;

	(void) arg;
	chRegSetThreadName("SUB RT2");
#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Subscriber #%d - RT2\r\n", nsub);
#endif /* VERBOSE */

	mw.newNode(&n);
	n.subscribe(&sub);

	while (!chThdShouldTerminate()) {
		n.spin();
		if ((d = sub.get()) != NULL) {
//			if (cnt != 0 && (d->cnt - cnt) != 1) {
//				palTogglePad(LED_GPIO, LED3);
//			}
//			cnt = d->cnt;
			sub.release(d);
		}
	}

	mw.delNode(&n);
	chThdExit(RDY_OK);

	return 0;
}

/*
 * Benchmarks.
 */
void terminate_subscribers(void) {
	uint32_t n = 0;

	for (n = 0; n < MAX_SUBSCRIBERS; n++) {
		if (subtp[n] != NULL) {
			chThdTerminate(subtp[n]);
			chSysLock();
			chSchReadyI(subtp[n]);
			chSysUnlock();
		}
	}

	chSchDoReschedule();

	for (n = 0; n < MAX_SUBSCRIBERS; n++) {
		if (subtp[n] != NULL) {
			chThdWait(subtp[n]);
			subtp[n] = NULL;
		}
	}
#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "All subscribers deleted - core free memory : %u bytes\r\n", chCoreStatus());
#endif /* VERBOSE */

	chThdSleepMilliseconds(100);
}

void latency_test(int nsub) {
	while (1) {
		if (--nsub <= 0) {
			break;
		}
		if (chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO, SubscriberThread10Hz, NULL) == NULL) {
			chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Memory full\r\n", chCoreStatus());
			break;
		}
		chThdSleepMilliseconds(100);
		if (--nsub <= 0) {
			break;
		}
		if (chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO, SubscriberThread1Hz, NULL) == NULL) {
			chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Memory full\r\n", chCoreStatus());
			break;
		}
	}

	chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 2, SubscriberThreadRT, NULL);
	chThdSleepMilliseconds(100);

#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Total subscribers : %d\r\n", subscribers);
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "core free memory : %u bytes\r\n", chCoreStatus());
#endif /* VERBOSE */

	chThdSleepMilliseconds(100);

	if (chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 1, PublisherThread100Hz, NULL) != NULL) {
#if VERBOSE
		chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Publisher created\r\n", chCoreStatus());
#endif
	} else {
		chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Memory full - no publisher\r\n", chCoreStatus());
	}
}

void throughput_test(uint32_t nsub, uint32_t nmsg) {
	uint32_t n = 0;

#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "TEST STARTING - core free memory : %u bytes\r\n", chCoreStatus());
#endif

	chThdSleepMilliseconds(100);

	while (n < (nsub - 1)) {
		if ((subtp[n] = chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 2, SubscriberThreadRT2, NULL)) == NULL) {
			chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Memory full creating subscriber\r\n", chCoreStatus());
			break;
		}
		n++;
		chThdSleepMilliseconds(100);
	}

	if ((subtp[n] = chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 2, SubscriberThreadRT, NULL)) == NULL) {
		chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Memory full creating subscriber\r\n", chCoreStatus());
	}

	chThdSleepMilliseconds(100);

	if ((pubtp = chThdCreateFromHeap (NULL, WA_SIZE_512B, NORMALPRIO + 1, PublisherThreadFlood, &nmsg)) == NULL) {
		chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Memory full creating publisher\r\n", chCoreStatus());
	}

	chThdWait(pubtp);
	pubtp = NULL;

#if VERBOSE
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Throughput test ended: %d subscribers - %d messages in %d ms\r\n", subscribers, cnt, (end_time - start_time));
#else
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "%d %d %d\r\n", subscribers, cnt, (end_time - start_time));
#endif /* VERBOSE */

	terminate_subscribers();
	subscribers = 0;
	cnt = 0;
}
/*
 * Application entry point.
 */
int main(void) {

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
	rtcanInit();
	rtcanStart (NULL);
*/

	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "\r\n");
	chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "sizeof(TestData) %d\r\n", sizeof(TestData));


	chThdSleepMilliseconds(1000);

//	latency_test(20);
	throughput_test(1, 1000000);

/*
	for (uint32_t n = 1; n <= 20; n++) {
		throughput_test(n, 1000000);

		palClearPort(LED_GPIO, LED1 | LED2 | LED3 | LED4);
		chThdSleepMilliseconds(500);
		palSetPort(LED_GPIO, LED1 | LED2 | LED3 | LED4);
	}
*/
#if REMOTE
	Middleware & mw = Middleware::instance();
	RemoteSubscriberT<TestData, 2> rsub("test");
	LocalPublisher * pub;

	pub = mw.findLocalPublisher("test");

	if (pub) {
		rsub.subscribe(pub);
		chprintf((BaseSequentialStream *)&SERIAL_DRIVER, "Remote subscriber started\r\n");
	}
#endif /* REMOTE */

	/* Test ended. */
	palClearPad(LED_GPIO, LED1);
	palClearPad(LED_GPIO, LED2);
	palClearPad(LED_GPIO, LED3);
	palClearPad(LED_GPIO, LED4);

	while(1);
}
