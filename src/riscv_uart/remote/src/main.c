/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(remote, LOG_LEVEL_INF);

/* LSM6DSO constant definitions */
#define LSM6DSO_I2C_ADDR    DT_REG_ADDR(DT_NODELABEL(lsm6dso))

#define LSM6DSO_REG_WHO_AM_I 0x0F
#define LSM6DSO_WHO_AM_I_VAL 0x6C

#define LSM6DSO_REG_CTRL1_XL 0x10
#define LSM6DSO_REG_CTRL2_G  0x11
#define LSM6DSO_REG_OUTX_L_XL 0x28

#define LSM6DSO_SENSITIVITY_G (0.000061f)

struct lsm6dso_raw_data {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
};

static int lsm6dso_i2c_reg_write_byte(const struct device *i2c_dev, uint8_t reg_addr, uint8_t value)
{
    uint8_t tx_buf[2] = {reg_addr, value};
    return i2c_write(i2c_dev, tx_buf, sizeof(tx_buf), LSM6DSO_I2C_ADDR);
}

static int lsm6dso_i2c_reg_read_byte(const struct device *i2c_dev, uint8_t reg_addr, uint8_t *value)
{
    return i2c_reg_read_byte(i2c_dev, LSM6DSO_I2C_ADDR, reg_addr, value);
}

static int lsm6dso_i2c_reg_read_bytes(const struct device *i2c_dev, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    return i2c_burst_read(i2c_dev, LSM6DSO_I2C_ADDR, reg_addr, data, len);
}

static int lsm6dso_init(const struct device *i2c_dev)
{
    uint8_t who_am_i = 0;
    int ret;

    LOG_INF("lsm6dso_init: Reading WHO_AM_I...");
    ret = lsm6dso_i2c_reg_read_byte(i2c_dev, LSM6DSO_REG_WHO_AM_I, &who_am_i);
    if (ret != 0) {
        LOG_ERR("Failed to read WHO_AM_I register (err: %d)", ret);
        return ret;
    }
    if (who_am_i != LSM6DSO_WHO_AM_I_VAL) {
        LOG_ERR("Invalid WHO_AM_I: 0x%02x, expected 0x%02x", who_am_i, LSM6DSO_WHO_AM_I_VAL);
        return -ENODEV;
    }
    LOG_INF("LSM6DSO WHO_AM_I check passed. ID: 0x%02x", who_am_i);

    ret = lsm6dso_i2c_reg_write_byte(i2c_dev, LSM6DSO_REG_CTRL1_XL, 0x40);
    if (ret != 0) {
        LOG_ERR("Failed to set CTRL1_XL register (err: %d)", ret);
        return ret;
    }

    ret = lsm6dso_i2c_reg_write_byte(i2c_dev, LSM6DSO_REG_CTRL2_G, 0x40);
    if (ret != 0) {
        LOG_ERR("Failed to set CTRL2_G register (err: %d)", ret);
        return ret;
    }

    LOG_INF("LSM6DSO initialized successfully.");
    return 0;
}

static int lsm6dso_fetch_raw_data(const struct device *i2c_dev, struct lsm6dso_raw_data *raw_data_out)
{
    uint8_t accel_data[6];
    int ret;

    ret = lsm6dso_i2c_reg_read_bytes(i2c_dev, LSM6DSO_REG_OUTX_L_XL, accel_data, 6);
    if (ret != 0) {
        return ret;
    }
    raw_data_out->accel_x = (int16_t)(accel_data[0] | (accel_data[1] << 8));
    raw_data_out->accel_y = (int16_t)(accel_data[2] | (accel_data[3] << 8));
    raw_data_out->accel_z = (int16_t)(accel_data[4] | (accel_data[5] << 8));
    
    return 0;
}

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

static void format_accel(char *buf, size_t buf_size, int16_t raw_val)
{
	int32_t val_millionths = (int32_t)raw_val * 61;
	int32_t val_hundredths;
	if (val_millionths >= 0) {
		val_hundredths = (val_millionths + 5000) / 10000;
	} else {
		val_hundredths = (val_millionths - 5000) / 10000;
	}
	bool is_neg = (val_hundredths < 0);
	int32_t abs_val = is_neg ? -val_hundredths : val_hundredths;
	int integral = abs_val / 100;
	int fractional = abs_val % 100;
	snprintk(buf, buf_size, "%s%d.%02d", is_neg ? "-" : "", integral, fractional);
}

int main(void)
{
	const struct device *ipc0_instance;
	const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c21));
	struct lsm6dso_raw_data sensor_data;
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

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device %s is not ready!", i2c_dev->name);
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

	if (lsm6dso_init(i2c_dev) != 0) {
		LOG_ERR("Failed to initialize LSM6DSO sensor.");
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

		/* Fetch accelerometer data */
		int16_t rx = 0, ry = 0, rz = 0;
		bool read_success = false;
		if (lsm6dso_fetch_raw_data(i2c_dev, &sensor_data) == 0) {
			rx = sensor_data.accel_x;
			ry = sensor_data.accel_y;
			rz = sensor_data.accel_z;
			read_success = true;
			LOG_INF("Raw accel: %d, %d, %d", rx, ry, rz);
		} else {
			LOG_WRN("Failed to fetch sensor data");
		}

		/* Format status message with Accelerometer values only using helper */
		char msg_buf[64];
		if (read_success) {
			char ax_str[16];
			char ay_str[16];
			char az_str[16];
			format_accel(ax_str, sizeof(ax_str), rx);
			format_accel(ay_str, sizeof(ay_str), ry);
			format_accel(az_str, sizeof(az_str), rz);
			snprintk(msg_buf, sizeof(msg_buf), "RV:%s,%s,%s", ax_str, ay_str, az_str);
		} else {
			snprintk(msg_buf, sizeof(msg_buf), "RV:ERR");
		}
		ret = ipc_service_send(&ep, msg_buf, strlen(msg_buf) + 1);
		if (ret < 0) {
			LOG_ERR("Failed to send IPC message: %d", ret);
		} else {
			LOG_INF("Sent LED/Sensor status: %s", msg_buf);
		}

		k_msleep(1000);
	}

	return 0;
}
