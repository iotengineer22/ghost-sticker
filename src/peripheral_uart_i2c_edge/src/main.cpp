/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic UART Bridge Service (NUS) sample modified for Edge Impulse IMU inference.
 */
#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <soc.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

extern "C" {
#include <uart_async_adapter.h>
#include <bluetooth/services/nus.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/settings/settings.h>
}

// --- Edge Impulse SDK includes ---
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

#define LOG_MODULE_NAME peripheral_uart
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_INF);

#define STACKSIZE CONFIG_BT_NUS_THREAD_STACK_SIZE
#define PRIORITY 7

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN	(sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED DK_LED1
#define RUN_LED_BLINK_INTERVAL 1000

#define CON_STATUS_LED DK_LED2

#define KEY_PASSKEY_ACCEPT DK_BTN1_MSK
#define KEY_PASSKEY_REJECT DK_BTN2_MSK

#define UART_BUF_SIZE CONFIG_BT_NUS_UART_BUFFER_SIZE
#define UART_WAIT_FOR_BUF_DELAY K_MSEC(50)
#define UART_WAIT_FOR_RX CONFIG_BT_NUS_UART_RX_WAIT_TIME

static K_SEM_DEFINE(ble_init_ok, 0, 1);

static struct bt_conn *current_conn;
static struct bt_conn *auth_conn;
static struct k_work adv_work;

static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(nordic_nus_uart));
static struct k_work_delayable uart_work;

/* LSM6DSO constant definitions */
#define SAMPLING_RATE_HZ 104
#define SAMPLING_PERIOD_MS (1000 / SAMPLING_RATE_HZ)
#define LSM6DSO_SENSITIVITY_G (0.000061f)

#define LSM6DSO_I2C_ADDR    DT_REG_ADDR(DT_ALIAS(accel0))

#define LSM6DSO_REG_WHO_AM_I 0x0F
#define LSM6DSO_WHO_AM_I_VAL 0x6C

#define LSM6DSO_REG_CTRL1_XL 0x10
#define LSM6DSO_REG_CTRL2_G  0x11
#define LSM6DSO_REG_OUTX_L_XL 0x28
#define LSM6DSO_REG_OUTX_L_G  0x22

#define STANDARD_GRAVITY 9.80665f

// Compile-time check for expected sensor axis count
#if EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME != 3
#error "This implementation assumes 3-axis accelerometer data."
#endif

// Calculate the number of samples needed per inference window
#define NUM_SAMPLES_PER_INFERENCE   (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE / EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME)

// Static buffer to hold the features for one inference window
static float features_buffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

struct lsm6dso_raw_data {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
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

static void i2c_scan(const struct device *i2c_dev)
{
	printk("[DEBUG] Starting I2C bus scan on %s...\n", i2c_dev->name);
	for (uint8_t addr = 0x08; addr <= 0x7F; addr++) {
		struct i2c_msg msgs[1];
		uint8_t dst;
		int ret;

		msgs[0].buf = &dst;
		msgs[0].len = 1;
		msgs[0].flags = I2C_MSG_READ | I2C_MSG_STOP;

		ret = i2c_transfer(i2c_dev, msgs, 1, addr);
		if (ret == 0) {
			printk("[DEBUG] Found I2C device at address: 0x%02X\n", addr);
		}
	}
	printk("[DEBUG] I2C bus scan completed.\n");
}

static int lsm6dso_init(const struct device *i2c_dev)
{
    uint8_t who_am_i = 0;
    int ret;

    printk("[DEBUG] lsm6dso_init: Reading WHO_AM_I...\n");
    ret = lsm6dso_i2c_reg_read_byte(i2c_dev, LSM6DSO_REG_WHO_AM_I, &who_am_i);
    printk("[DEBUG] lsm6dso_init: WHO_AM_I read ret=%d, val=0x%02x\n", ret, who_am_i);
    if (ret != 0) {
        LOG_ERR("Failed to read WHO_AM_I register (err: %d)", ret);
        return ret;
    }
    if (who_am_i != LSM6DSO_WHO_AM_I_VAL) {
        LOG_ERR("Invalid WHO_AM_I: 0x%02x, expected 0x%02x", who_am_i, LSM6DSO_WHO_AM_I_VAL);
        return -ENODEV;
    }
    LOG_INF("LSM6DSO WHO_AM_I check passed. ID: 0x%02x", who_am_i);

    printk("[DEBUG] lsm6dso_init: Writing CTRL1_XL...\n");
    ret = lsm6dso_i2c_reg_write_byte(i2c_dev, LSM6DSO_REG_CTRL1_XL, 0x40);
    printk("[DEBUG] lsm6dso_init: CTRL1_XL write ret=%d\n", ret);
    if (ret != 0) {
        LOG_ERR("Failed to set CTRL1_XL register (err: %d)", ret);
        return ret;
    }

    printk("[DEBUG] lsm6dso_init: Writing CTRL2_G...\n");
    ret = lsm6dso_i2c_reg_write_byte(i2c_dev, LSM6DSO_REG_CTRL2_G, 0x40);
    printk("[DEBUG] lsm6dso_init: CTRL2_G write ret=%d\n", ret);
    if (ret != 0) {
        LOG_ERR("Failed to set CTRL2_G register (err: %d)", ret);
        return ret;
    }

    LOG_INF("LSM6DSO initialized successfully at 104 Hz.");
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

static bool nus_notifications_enabled = false;

struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_BUF_SIZE];
	uint16_t len;
};

static K_FIFO_DEFINE(fifo_uart_tx_data);
static K_FIFO_DEFINE(fifo_uart_rx_data);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

#ifdef CONFIG_UART_ASYNC_ADAPTER
UART_ASYNC_ADAPTER_INST_DEFINE(async_adapter);
#else
#define async_adapter NULL
#endif

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);

	static size_t aborted_len;
	struct uart_data_t *buf;
	static uint8_t *aborted_buf;
	static bool disable_req;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("UART_TX_DONE");
		if ((evt->data.tx.len == 0) ||
		    (!evt->data.tx.buf)) {
			return;
		}

		if (aborted_buf) {
			buf = CONTAINER_OF(aborted_buf, struct uart_data_t,
					   data[0]);
			aborted_buf = NULL;
			aborted_len = 0;
		} else {
			buf = CONTAINER_OF(evt->data.tx.buf, struct uart_data_t,
					   data[0]);
		}

		k_free(buf);

		buf = (struct uart_data_t *)k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
		if (!buf) {
			return;
		}

		if (uart_tx(uart, buf->data, buf->len, SYS_FOREVER_MS)) {
			LOG_WRN("Failed to send data over UART");
		}

		break;

	case UART_RX_RDY:
		LOG_DBG("UART_RX_RDY");
		buf = CONTAINER_OF(evt->data.rx.buf, struct uart_data_t, data[0]);
		buf->len += evt->data.rx.len;

		if (disable_req) {
			return;
		}

		if ((evt->data.rx.buf[buf->len - 1] == '\n') ||
		    (evt->data.rx.buf[buf->len - 1] == '\r')) {
			disable_req = true;
			uart_rx_disable(uart);
		}

		break;

	case UART_RX_DISABLED:
		LOG_DBG("UART_RX_DISABLED");
		disable_req = false;

		buf = (struct uart_data_t *)k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
			k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
			return;
		}

		uart_rx_enable(uart, buf->data, sizeof(buf->data),
			       UART_WAIT_FOR_RX);

		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("UART_RX_BUF_REQUEST");
		buf = (struct uart_data_t *)k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
			uart_rx_buf_rsp(uart, buf->data, sizeof(buf->data));
		} else {
			LOG_WRN("Not able to allocate UART receive buffer");
		}

		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("UART_RX_BUF_RELEASED");
		buf = CONTAINER_OF(evt->data.rx_buf.buf, struct uart_data_t,
				   data[0]);

		if (buf->len > 0) {
			k_fifo_put(&fifo_uart_rx_data, buf);
		} else {
			k_free(buf);
		}

		break;

	case UART_TX_ABORTED:
		LOG_DBG("UART_TX_ABORTED");
		if (!aborted_buf) {
			aborted_buf = (uint8_t *)evt->data.tx.buf;
		}

		aborted_len += evt->data.tx.len;
		buf = CONTAINER_OF((void *)aborted_buf, struct uart_data_t,
				   data);

		uart_tx(uart, &buf->data[aborted_len],
			buf->len - aborted_len, SYS_FOREVER_MS);

		break;

	default:
		break;
	}
}

static void uart_work_handler(struct k_work *item)
{
	struct uart_data_t *buf;

	buf = (struct uart_data_t *)k_malloc(sizeof(*buf));
	if (buf) {
		buf->len = 0;
	} else {
		LOG_WRN("Not able to allocate UART receive buffer");
		k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
		return;
	}

	uart_rx_enable(uart, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
}

static bool uart_test_async_api(const struct device *dev)
{
	const struct uart_driver_api *api =
			(const struct uart_driver_api *)dev->api;

	return (api->callback_set != NULL);
}

static int uart_init(void)
{
	int err;
	int pos;
	struct uart_data_t *rx;
	struct uart_data_t *tx;

	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	rx = (struct uart_data_t *)k_malloc(sizeof(*rx));
	if (rx) {
		rx->len = 0;
	} else {
		return -ENOMEM;
	}

	k_work_init_delayable(&uart_work, uart_work_handler);


	if (IS_ENABLED(CONFIG_UART_ASYNC_ADAPTER) && !uart_test_async_api(uart)) {
		/* Implement API adapter */
		uart_async_adapter_init(async_adapter, uart);
		uart = async_adapter;
	}

	err = uart_callback_set(uart, uart_cb, NULL);
	if (err) {
		k_free(rx);
		LOG_ERR("Cannot initialize UART callback");
		return err;
	}

	if (IS_ENABLED(CONFIG_UART_LINE_CTRL)) {
		LOG_INF("Wait for DTR");
		while (true) {
			uint32_t dtr = 0;

			uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr);
			if (dtr) {
				break;
			}
			/* Give CPU resources to low priority threads. */
			k_sleep(K_MSEC(100));
		}
		LOG_INF("DTR set");
		err = uart_line_ctrl_set(uart, UART_LINE_CTRL_DCD, 1);
		if (err) {
			LOG_WRN("Failed to set DCD, ret code %d", err);
		}
		err = uart_line_ctrl_set(uart, UART_LINE_CTRL_DSR, 1);
		if (err) {
			LOG_WRN("Failed to set DSR, ret code %d", err);
		}
	}

	tx = (struct uart_data_t *)k_malloc(sizeof(*tx));

	if (tx) {
		pos = snprintf((char *)tx->data, sizeof(tx->data),
			       "NUS EdgeAI Inference demo\r\n");

		if ((pos < 0) || ((size_t)pos >= sizeof(tx->data))) {
			k_free(rx);
			k_free(tx);
			LOG_ERR("snprintf returned %d", pos);
			return -ENOMEM;
		}

		tx->len = pos;
	} else {
		k_free(rx);
		return -ENOMEM;
	}

	err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
	if (err) {
		k_free(rx);
		k_free(tx);
		LOG_ERR("Cannot display welcome message (err: %d)", err);
		return err;
	}

	err = uart_rx_enable(uart, rx->data, sizeof(rx->data), UART_WAIT_FOR_RX);
	if (err) {
		LOG_ERR("Cannot enable uart reception (err: %d)", err);
		/* Free the rx buffer only because the tx buffer will be handled in the callback */
		k_free(rx);
	}

	return err;
}

static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed, err 0x%02x %s", err, bt_hci_err_to_str(err));
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected %s", addr);

	current_conn = bt_conn_ref(conn);

	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s, reason 0x%02x %s", addr, reason, bt_hci_err_to_str(reason));

	if (auth_conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
		dk_set_led_off(CON_STATUS_LED);
	}
}

static void recycled_cb(void)
{
	LOG_INF("Connection object available from previous conn. Disconnect is complete!");
	advertising_start();
}

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
	} else {
		LOG_WRN("Security failed: %s level %u err %d %s", addr, level, err,
			bt_security_err_to_str(err));
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_NUS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	auth_conn = bt_conn_ref(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u", addr, passkey);

	if (IS_ENABLED(CONFIG_SOC_SERIES_NRF54HX) || IS_ENABLED(CONFIG_SOC_SERIES_NRF54LX)) {
		LOG_INF("Press Button 0 to confirm, Button 1 to reject.");
	} else {
		LOG_INF("Press Button 1 to confirm, Button 2 to reject.");
	}
}


static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing failed conn: %s, reason %d %s", addr, reason,
		bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
			  uint16_t len)
{
	int err;
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

	LOG_INF("Received data from: %s", addr);

	for (uint16_t pos = 0; pos != len;) {
		struct uart_data_t *tx = (struct uart_data_t *)k_malloc(sizeof(*tx));

		if (!tx) {
			LOG_WRN("Not able to allocate UART send data buffer");
			return;
		}

		uint16_t tx_data_size = (uint16_t)(sizeof(tx->data) - 1);

		if ((len - pos) > tx_data_size) {
			tx->len = tx_data_size;
		} else {
			tx->len = (len - pos);
		}

		memcpy(tx->data, &data[pos], tx->len);

		pos += tx->len;

		/* Append the LF character when the CR character triggered
		 * transmission from the peer.
		 */
		if ((pos == len) && (data[len - 1] == '\r')) {
			tx->data[tx->len] = '\n';
			tx->len++;
		}

		err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
		if (err) {
			k_fifo_put(&fifo_uart_tx_data, tx);
		}
	}
}

static void bt_send_enabled_cb(enum bt_nus_send_status status)
{
	if (status == BT_NUS_SEND_STATUS_ENABLED) {
		nus_notifications_enabled = true;
		printk("[DEBUG] NUS notifications enabled\n");
	} else {
		nus_notifications_enabled = false;
		printk("[DEBUG] NUS notifications disabled\n");
	}
}

static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
	.send_enabled = bt_send_enabled_cb,
};

void error(void)
{
	dk_set_leds_state(DK_ALL_LEDS_MSK, DK_NO_LEDS_MSK);

	while (true) {
		/* Spin for ever */
		k_sleep(K_MSEC(1000));
	}
}

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
static void num_comp_reply(bool accept)
{
	if (accept) {
		bt_conn_auth_passkey_confirm(auth_conn);
		LOG_INF("Numeric Match, conn %p", (void *)auth_conn);
	} else {
		bt_conn_auth_cancel(auth_conn);
		LOG_INF("Numeric Reject, conn %p", (void *)auth_conn);
	}

	bt_conn_unref(auth_conn);
	auth_conn = NULL;
}

void button_changed(uint32_t button_state, uint32_t has_changed)
{
	uint32_t buttons = button_state & has_changed;

	if (auth_conn) {
		if (buttons & KEY_PASSKEY_ACCEPT) {
			num_comp_reply(true);
		}

		if (buttons & KEY_PASSKEY_REJECT) {
			num_comp_reply(false);
		}
	}
}
#endif /* CONFIG_BT_NUS_SECURITY_ENABLED */

static void configure_gpio(void)
{
	int err;

#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Cannot init buttons (err: %d)", err);
	}
#endif /* CONFIG_BT_NUS_SECURITY_ENABLED */

	err = dk_leds_init();
	if (err) {
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}

static int raw_feature_get_data(size_t offset, size_t length, float *out_ptr)
{
    memcpy(out_ptr, features_buffer + offset, length * sizeof(float));
    return 0;
}

static void collect_sensor_data(const struct device *i2c_dev, int64_t &led_timer_ms)
{
    struct lsm6dso_raw_data sensor_data;


    for (uint32_t i = 0; i < NUM_SAMPLES_PER_INFERENCE; i++) {
        int64_t start_time_ms = k_uptime_get();

        if (lsm6dso_fetch_raw_data(i2c_dev, &sensor_data) == 0) {
            float ax_g = (float)(sensor_data.accel_x * LSM6DSO_SENSITIVITY_G);
            float ay_g = (float)(sensor_data.accel_y * LSM6DSO_SENSITIVITY_G);
            float az_g = (float)(sensor_data.accel_z * LSM6DSO_SENSITIVITY_G);

            size_t current_index = i * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;
            features_buffer[current_index + 0] = ax_g;
            features_buffer[current_index + 1] = ay_g;
            features_buffer[current_index + 2] = az_g;
        } else {
            LOG_WRN("Failed to read LSM6DSO sensor data, retrying...");
            i--; // Retry this sample
        }

        // LED control: 1 second interval, ON for 100ms, OFF for 900ms
        int64_t now_ms = k_uptime_get();
        int64_t elapsed_ms = now_ms - led_timer_ms;
        if (elapsed_ms >= 1000) {
            led_timer_ms = now_ms;
            elapsed_ms = 0;
        }

        if (elapsed_ms < 100) {
            dk_set_led(DK_LED1, 1);
            dk_set_led(DK_LED2, 1);
        } else {
            dk_set_led(DK_LED1, 0);
            dk_set_led(DK_LED2, 0);
        }

        // Maintain sampling rate
        int64_t processing_time_ms = k_uptime_get() - start_time_ms;
        int sleep_time_ms = SAMPLING_PERIOD_MS - (int)processing_time_ms;
        if (sleep_time_ms > 0) {
            k_sleep(K_MSEC(sleep_time_ms));
        }
    }
}

static void run_inference(void)
{
    signal_t features_signal;
    features_signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    features_signal.get_data = &raw_feature_get_data;

    ei_impulse_result_t result = {0};

    // Run the classifier (DSP + Neural Network)
    EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);

    if (res != EI_IMPULSE_OK) {
        LOG_ERR("Classifier returned error: %d", res);
    } else {
        char ble_buf[256];
        int ble_len = 0;

        // Build the single-line string
        ble_len += snprintf(ble_buf + ble_len, sizeof(ble_buf) - ble_len,
                            "Predictions (DSP: %d ms, Class: %d ms): ",
                            result.timing.dsp, result.timing.classification);

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            ble_len += snprintf(ble_buf + ble_len, sizeof(ble_buf) - ble_len,
                                "%s: %.3f%s",
                                result.classification[ix].label,
                                (double)result.classification[ix].value,
                                (ix == EI_CLASSIFIER_LABEL_COUNT - 1) ? "" : ", ");
        }

#if EI_CLASSIFIER_HAS_ANOMALY == 1
        ble_len += snprintf(ble_buf + ble_len, sizeof(ble_buf) - ble_len,
                            ", anomaly: %.3f", (double)result.anomaly);
#endif

        // Log to Zephyr logger (without trailing \r\n)
        LOG_INF("%s", ble_buf);

        // Print to standard console (with trailing \r\n)
        ble_len += snprintf(ble_buf + ble_len, sizeof(ble_buf) - ble_len, "\r\n");
        printf("%s", ble_buf);

        // Send over BLE
        if (current_conn && nus_notifications_enabled) {
            bt_nus_send(current_conn, (uint8_t *)ble_buf, ble_len);
        }
    }
}

extern "C" int main(void)
{
	int err = 0;
	const struct device *i2c_dev = DEVICE_DT_GET(DT_ALIAS(sensor_i2c));

	// Disable buffering for stdout to ensure immediate output for printf.
	setvbuf(stdout, NULL, _IONBF, 0);

	printk("[DEBUG] main: Starting configure_gpio...\n");
	configure_gpio();

	printk("[DEBUG] main: Starting uart_init...\n");
	err = uart_init();
	printk("[DEBUG] main: uart_init completed ret=%d\n", err);
	if (err) {
		error();
	}

	if (IS_ENABLED(CONFIG_BT_NUS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			LOG_ERR("Failed to register authorization callbacks. (err: %d)", err);
			return 0;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			LOG_ERR("Failed to register authorization info callbacks. (err: %d)", err);
			return 0;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		error();
	}

	LOG_INF("Bluetooth initialized");

	k_sem_give(&ble_init_ok);

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_nus_init(&nus_cb);
	if (err) {
		LOG_ERR("Failed to initialize UART service (err: %d)", err);
		return 0;
	}

	k_work_init(&adv_work, adv_work_handler);
	advertising_start();

	// 10-second delay for SENSOR operations only (BLE advertising has already started)
	printk("[DEBUG] main: Waiting 10 seconds before starting sensor...\n");
	k_sleep(K_MSEC(10000));
	printk("[DEBUG] main: 10 seconds wait completed. Starting sensor operations.\n");

	printk("[DEBUG] main: Checking device_is_ready(i2c_dev)...\n");
	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device %s is not ready!", i2c_dev->name);
		return 0;
	}
	printk("[DEBUG] main: i2c_dev is ready.\n");

	i2c_scan(i2c_dev);

	printk("[DEBUG] main: Starting lsm6dso_init...\n");
	if (lsm6dso_init(i2c_dev) != 0) {
		LOG_ERR("Failed to initialize LSM6DSO sensor.");
		return 0;
	}
	printk("[DEBUG] main: lsm6dso_init completed successfully.\n");

	int64_t led_timer_ms = k_uptime_get();

	for (;;) {
        // --- Phase 1: Collect data for one inference window ---
        collect_sensor_data(i2c_dev, led_timer_ms);

        // --- Phase 2: Run inference on the collected data ---
        run_inference();
	}
}

void ble_write_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Don't go any further until BLE is initialized */
	k_sem_take(&ble_init_ok, K_FOREVER);
	struct uart_data_t nus_data = {
		.fifo_reserved = NULL,
		.data = {0},
		.len = 0,
	};

	for (;;) {
		/* Wait indefinitely for data to be sent over bluetooth */
		struct uart_data_t *buf = (struct uart_data_t *)k_fifo_get(&fifo_uart_rx_data,
						     K_FOREVER);

		int plen = MIN((int)sizeof(nus_data.data) - nus_data.len, buf->len);
		int loc = 0;

		while (plen > 0) {
			memcpy(&nus_data.data[nus_data.len], &buf->data[loc], plen);
			nus_data.len += plen;
			loc += plen;

			if (nus_data.len >= sizeof(nus_data.data) ||
			   (nus_data.data[nus_data.len - 1] == '\n') ||
			   (nus_data.data[nus_data.len - 1] == '\r')) {
				if (bt_nus_send(NULL, nus_data.data, nus_data.len)) {
					LOG_WRN("Failed to send data over BLE connection");
				}
				nus_data.len = 0;
			}

			plen = MIN((int)sizeof(nus_data.data), buf->len - loc);
		}

		k_free(buf);
	}
}

K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL,
		NULL, PRIORITY, 0, 0);
