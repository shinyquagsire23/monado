// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Arduino based felxable input device code.
 * @author Pete Black <pete.black@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_arduino
 */

#include "mbed.h"
#include <stdint.h>
#include <ArduinoBLE.h>
#include <Arduino_LSM9DS1.h>


#define USE_SERIAL
#ifdef USE_SERIAL
#define LOG(...) Serial.print(__VA_ARGS__)
#define LOG_LN(...) Serial.println(__VA_ARGS__)
#else
#define LOG(...) (void)NULL
#define LOG_LN(...) (void)NULL
#endif

#define SET(o, v)                                                              \
	do {                                                                   \
		uint32_t val = v;                                              \
		buffer[o + 0] = val >> 8;                                      \
		buffer[o + 1] = val;                                           \
	} while (false)

struct Sample
{
	int32_t acc_x = 0;
	int32_t acc_y = 0;
	int32_t acc_z = 0;
	int32_t gyr_x = 0;
	int32_t gyr_y = 0;
	int32_t gyr_z = 0;
	uint32_t time = 0;
};


/*
 *
 * Global variables
 *
 */

volatile bool isConnected;

BLEService service("00004242-0000-1000-8000-004242424242"); // create service

// create switch characteristic and allow remote device to read and write
BLECharacteristic notify("00000001-1000-1000-8000-004242424242",
                         BLERead | BLENotify,
                         20,
                         true);

rtos::Mutex mutex;
rtos::Thread thread;
rtos::Mail<Sample, 100> mail;


/*
 *
 * IMU functions.
 *
 */

void
imu_loop()
{
	if (isConnected == false) {
		return;
	}

	if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
		uint32_t time = micros();
		float gyr_x, gyr_y, gyr_z;
		float acc_x, acc_y, acc_z;
		IMU.readGyroscope(gyr_x, gyr_y, gyr_z);
		IMU.readAcceleration(acc_x, acc_y, acc_z);

		Sample *sample = mail.alloc();
		sample->time = time;
		sample->acc_x = acc_x * (32768.0 / 4.0);
		sample->acc_y = acc_y * (32768.0 / 4.0);
		sample->acc_z = acc_z * (32768.0 / 4.0);

		sample->gyr_x = gyr_x * (32768.0 / 2000.0);
		sample->gyr_y = gyr_y * (32768.0 / 2000.0);
		sample->gyr_z = gyr_z * (32768.0 / 2000.0);
		mail.put(sample);
	}
}

void
imu_thread()
{
	while (true) {
		imu_loop();
		delay(1);
	}
}


/*
 *
 * BLE functions.
 *
 */

void
loop()
{
	bool got_mail = false;
	int32_t acc_x = 0, acc_y = 0, acc_z = 0;
	int32_t gyr_x = 0, gyr_y = 0, gyr_z = 0;
	int32_t count = 0;
	uint32_t time;

	uint8_t buffer[20];
	static int countPacket = 0;

	BLE.poll();
	if (isConnected == false) {
		return;
	}

	while (true) {
		osEvent evt = mail.get(0);
		if (evt.status != osEventMail) {
			break;
		}

		Sample *sample = (Sample *)evt.value.p;
		acc_x += sample->acc_x;
		acc_y += sample->acc_y;
		acc_z += sample->acc_z;
		gyr_x += sample->gyr_x;
		gyr_y += sample->gyr_y;
		gyr_z += sample->gyr_z;
		time = sample->time;
		mail.free(sample);

		count++;
		got_mail = true;
	}

	if (!got_mail) {
		delay(1);
		return;
	}

	buffer[0] = countPacket++;
	buffer[1] = count;
	buffer[2] = 0;

	buffer[3] = time >> 16;
	buffer[4] = time >> 8;
	buffer[5] = time;

	SET(6, (acc_x / count));
	SET(8, (acc_y / count));
	SET(10, (acc_z / count));
	SET(12, (gyr_x / count));
	SET(14, (gyr_y / count));
	SET(16, (gyr_z / count));
	buffer[18] = 0;
	buffer[19] = 0;

	notify.writeValue(buffer, 20);
}

void
ble_peripheral_connect_handler(BLEDevice peer)
{
	LOG("Connected event, central: ");
	LOG_LN(peer.address());
	isConnected = true;
	BLE.poll();
}

void
ble_peripheral_disconnect_handler(BLEDevice peer)
{
	LOG("Disconnected event, central: ");
	LOG_LN(peer.address());
	isConnected = false;
	BLE.poll();
}


/*
 *
 * Main functions.
 *
 */

void
block()
{
	while (true) {
		LOG_LN("BLOCKED");
		delay(1000);
	}
}

void
setup()
{
#ifdef USE_SERIAL
	Serial.begin(9600);
	while (!Serial) {
	}
#endif

	if (!BLE.begin()) {
		LOG_LN("BLE initialisation failed!");
		block();
	}

	if (!IMU.begin()) {
		LOG_LN("IMU initialisation failed!");
		block();
	}

	BLE.setLocalName("Monado Flexible Controller");
	BLE.setAdvertisedService(service);
	service.addCharacteristic(notify);
	BLE.addService(service);

	// Assign event handlers for connected, disconnected to peripheral.
	BLE.setEventHandler(BLEConnected, ble_peripheral_connect_handler);
	BLE.setEventHandler(BLEDisconnected, ble_peripheral_disconnect_handler);

	notify.setValue(0);

	// Start advertising.
	BLE.advertise();

	isConnected = false;

	// IMU has it's own thread.
	thread.start(imu_thread);
}
