/*
 * Guitar Controller TX — BLE Peripheral with LLPM
 *
 * Reads 7 buttons (5 frets + strum up/down) via GPIO interrupts,
 * sends state as BLE notifications with 1ms LLPM connection interval.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <bluetooth/hci_vs_sdc.h>
#include <sdc_hci_vs.h>

LOG_MODULE_REGISTER(guitar_tx, LOG_LEVEL_INF);

/* ── Custom GATT Service UUIDs ────────────────────────────────── */

/* Service:        e0a00001-5e00-4d74-9b80-5b1a3e6a0a1e */
/* Characteristic: e0a00002-5e00-4d74-9b80-5b1a3e6a0a1e */

static struct bt_uuid_128 guitar_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0a00001, 0x5e00, 0x4d74, 0x9b80, 0x5b1a3e6a0a1e));

static struct bt_uuid_128 guitar_btn_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xe0a00002, 0x5e00, 0x4d74, 0x9b80, 0x5b1a3e6a0a1e));

/* ── Buttons ──────────────────────────────────────────────────── */

#define BTN_GREEN      0
#define BTN_RED        1
#define BTN_YELLOW     2
#define BTN_BLUE       3
#define BTN_ORANGE     4
#define BTN_STRUM_UP   5
#define BTN_STRUM_DOWN 6
#define BTN_TILT       7
#define BTN_START      8
#define BTN_SELECT     9
#define BTN_GUIDE      10
#define NUM_BUTTONS    11

static const struct gpio_dt_spec buttons[NUM_BUTTONS] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(fret0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(fret1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(fret2), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(fret3), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(fret4), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(strum_up), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(strum_down), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(tilt), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(start), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(select), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(guide), gpios),
};

static struct gpio_callback btn_cb[NUM_BUTTONS];
static volatile uint16_t button_state;
static uint16_t last_notified_state;

/* ── LED indicator ────────────────────────────────────────────── */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void led_blink_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(led_blink_work, led_blink_handler);

static void led_blink_handler(struct k_work *work)
{
	gpio_pin_toggle_dt(&led);
	k_work_schedule(&led_blink_work, K_MSEC(500));
}

static void led_set_connected(bool connected)
{
	k_work_cancel_delayable(&led_blink_work);
	if (connected) {
		gpio_pin_set_dt(&led, 1);
	} else {
		k_work_schedule(&led_blink_work, K_NO_WAIT);
	}
}

/* ── BLE state ────────────────────────────────────────────────── */

static struct bt_conn *active_conn;
static bool notifications_enabled;

/* ── Button send (1ms debounce) ────────────────────────────────── */

static void debounce_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(debounce_work, debounce_handler);

static void debounce_handler(struct k_work *work)
{
	uint16_t state = 0;

	for (int i = 0; i < NUM_BUTTONS; i++) {
		if (gpio_pin_get_dt(&buttons[i])) {
			state |= BIT(i);
		}
	}

	button_state = state;

	if (state != last_notified_state && active_conn && notifications_enabled) {
		last_notified_state = state;
		bt_gatt_notify_uuid(active_conn, &guitar_btn_uuid.uuid,
				    NULL, &state, sizeof(state));
	}
}

/* ── GPIO ISR ─────────────────────────────────────────────────── */

static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_reschedule(&debounce_work, K_MSEC(1));
}

/* ── GATT Service ─────────────────────────────────────────────── */

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Notifications %s", notifications_enabled ? "on" : "off");
}

BT_GATT_SERVICE_DEFINE(guitar_svc,
	BT_GATT_PRIMARY_SERVICE(&guitar_svc_uuid),
	BT_GATT_CHARACTERISTIC(&guitar_btn_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(ccc_changed,
		     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* ── Advertising restart (via recycled callback) ─────────────── */

static struct k_work adv_work;

static void adv_work_handler(struct k_work *work)
{
	const struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS,
			      BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
		BT_DATA_BYTES(BT_DATA_UUID128_ALL,
			      BT_UUID_128_ENCODE(0xe0a00001, 0x5e00, 0x4d74,
						 0x9b80, 0x5b1a3e6a0a1e)),
	};
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad,
				  ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Adv restart failed: %d", err);
	} else {
		LOG_INF("Advertising restarted");
	}
}

/* ── BLE Callbacks ────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed: %u", err);
		return;
	}

	active_conn = bt_conn_ref(conn);
	led_set_connected(true);
	LOG_INF("Connected");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected: %u", reason);
	led_set_connected(false);

	if (active_conn) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}
	notifications_enabled = false;
	last_notified_state = 0xFF;
	/* Don't restart adv here — wait for recycled callback */
}

static void on_recycled(void)
{
	/* Connection object freed — safe to restart advertising */
	k_work_submit(&adv_work);
}

static struct bt_conn_cb conn_cb = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.recycled = on_recycled,
};

/* ── Main ─────────────────────────────────────────────────────── */

int main(void)
{
	int err;

	LOG_INF("Guitar TX starting");

	k_work_init(&adv_work, adv_work_handler);

	/* Init LED — start blinking (searching) */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
		led_set_connected(false);
	}

	/* Configure GPIO buttons with interrupts (non-fatal on error) */
	for (int i = 0; i < NUM_BUTTONS; i++) {
		if (!gpio_is_ready_dt(&buttons[i])) {
			LOG_WRN("GPIO not ready for button %d, skipping", i);
			continue;
		}

		err = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
		if (err) {
			LOG_WRN("Button %d configure failed: %d, skipping", i, err);
			continue;
		}

		err = gpio_pin_interrupt_configure_dt(&buttons[i],
						      GPIO_INT_EDGE_BOTH);
		if (err) {
			LOG_WRN("Button %d interrupt failed: %d", i, err);
			continue;
		}

		gpio_init_callback(&btn_cb[i], button_isr,
				   BIT(buttons[i].pin));
		gpio_add_callback(buttons[i].port, &btn_cb[i]);
		LOG_INF("Button %d OK (P%d.%02d)", i,
			buttons[i].port == DEVICE_DT_GET(DT_NODELABEL(gpio0)) ? 0 : 1,
			buttons[i].pin);
	}

	/* Init Bluetooth */
	bt_conn_cb_register(&conn_cb);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("BT init failed: %d", err);
		return err;
	}
	LOG_INF("BT ready");

	/* Enable LLPM mode */
	sdc_hci_cmd_vs_llpm_mode_set_t llpm_cmd = { .enable = true };
	err = hci_vs_sdc_llpm_mode_set(&llpm_cmd);
	LOG_INF("LLPM mode: %d", err);

	/* Start advertising */
	const struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS,
			      BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
		BT_DATA_BYTES(BT_DATA_UUID128_ALL,
			      BT_UUID_128_ENCODE(0xe0a00001, 0x5e00, 0x4d74,
						 0x9b80, 0x5b1a3e6a0a1e)),
	};
	const struct bt_data sd[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, "Guitar TX", 9),
	};

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed: %d", err);
		return err;
	}
	LOG_INF("Advertising started");

	/* All work done in ISR + BLE callbacks */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
