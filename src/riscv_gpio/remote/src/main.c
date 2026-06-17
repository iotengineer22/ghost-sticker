/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(remote, LOG_LEVEL_INF);

/* The devicetree node identifier for the "led0" and "led1" aliases. */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

#if !DT_NODE_EXISTS(LED0_NODE)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if !DT_NODE_EXISTS(LED1_NODE)
#error "Unsupported board: led1 devicetree alias is not defined"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

#if defined(CONFIG_MULTITHREADING)
K_SEM_DEFINE(bound_sem, 0, 1);
#else
volatile uint32_t bound_sem = 1;
#endif

static void ep_bound(void *priv)
{
#if defined(CONFIG_MULTITHREADING)
	k_sem_give(&bound_sem);
#else
	bound_sem = 0;
#endif
	LOG_INF("Ep bounded");
}

static void ep_recv(const void *data, size_t len, void *priv)
{
	/* Host messages are ignored in this implementation */
}

static struct ipc_ept_cfg ep_cfg = {
	.cb = {
		.bound    = ep_bound,
		.received = ep_recv,
	},
};

int main(void)
{
	const struct device *ipc0_instance;
	struct ipc_ept ep;
	int ret;
	bool led_state = true;

	LOG_INF("RISC-V Alternate LED Blinky with IPC started");

	if (!gpio_is_ready_dt(&led0)) {
		LOG_ERR("LED0 GPIO device not ready");
		return 0;
	}

	if (!gpio_is_ready_dt(&led1)) {
		LOG_ERR("LED1 GPIO device not ready");
		return 0;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE); /* ON initially */
	if (ret < 0) {
		LOG_ERR("Failed to configure LED0");
		return 0;
	}

	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE); /* OFF initially */
	if (ret < 0) {
		LOG_ERR("Failed to configure LED1");
		return 0;
	}

	/* IPC Service setup */
	ipc0_instance = DEVICE_DT_GET(DT_NODELABEL(ipc0));

	ret = ipc_service_open_instance(ipc0_instance);
	if ((ret < 0) && (ret != -EALREADY)) {
		LOG_ERR("ipc_service_open_instance() failure: %d", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc0_instance, &ep, &ep_cfg);
	if (ret < 0) {
		LOG_ERR("ipc_service_register_endpoint() failure: %d", ret);
		return ret;
	}

#if defined(CONFIG_MULTITHREADING)
	k_sem_take(&bound_sem, K_FOREVER);
#else
	while (bound_sem != 0) {
	};
#endif

	LOG_INF("IPC bound. Commencing alternating LED blinky and state transmission...");

	while (1) {
		/* Toggle state variable */
		led_state = !led_state;

		ret = gpio_pin_set_dt(&led0, led_state ? 1 : 0);
		if (ret < 0) {
			LOG_ERR("Failed to set LED0");
			return 0;
		}
		ret = gpio_pin_set_dt(&led1, led_state ? 0 : 1);
		if (ret < 0) {
			LOG_ERR("Failed to set LED1");
			return 0;
		}

		/* Format status message */
		char msg_buf[64];
		snprintk(msg_buf, sizeof(msg_buf), "RISC-V_LED0:%s,1:%s",
			 led_state ? "1" : "0", led_state ? "0" : "1");

		/* Send via IPC */
		ret = ipc_service_send(&ep, msg_buf, strlen(msg_buf) + 1);
		if (ret < 0) {
			LOG_ERR("Failed to send IPC message: %d", ret);
		} else {
			LOG_INF("Sent LED status: %s", msg_buf);
		}

		k_msleep(1000);
	}

	return 0;
}
