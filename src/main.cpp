#include "main.h"

/** Loop thread ID */
osThreadId loop_thread = NULL;

/** Timer for periodic sending */
TimerEvent_t app_timer;

/** Data packet */
env_data_s g_env_data;

/**
 * @brief Timer callback to request new packet sending
 * 
 */
void trigger_sending(void)
{
	if (loop_thread != NULL)
	{
		osSignalSet(loop_thread, SIGNAL_SEND);
	}
}

/**
 * @brief Arduino setup
 * 
 */
void setup(void)
{
	delay(500);
	Serial.begin(115200);
	time_t timeout = millis();
	while (!Serial)
	{
		if ((millis() - timeout) < 5000)
		{
			delay(100);
		}
		else
		{
			break;
		}
	}

	// Get default credentials
	init_flash();

	Serial1.begin(115200);

	// Initialize the BME680 sensor
	init_bme();

	// Start the BME680 sensor
	start_bme();

	// Initialize the battery readings
	init_batt();

	DualSerial("====================================\n");
	DualSerial("RAK11300 Environment Sensor Firmware\n");
	DualSerial("SW Version %d.%d.%d\n", g_sw_ver_1, g_sw_ver_2, g_sw_ver_3);
	DualSerial("LoRa(R) is a registered trademark or service mark of Semtech Corporation or its affiliates.\nLoRaWAN(R) is a licensed mark.\n");
	DualSerial("====================================\n");
	at_settings();
	DualSerial("====================================\n");

	init_serial_task();

	if (g_lorawan_settings.auto_join)
	{
		if (g_lorawan_settings.lorawan_enable)
		{
			init_lorawan();
		}
		else
		{
			init_lora();
		}
	}
}

void loop()
{
	loop_thread = osThreadGetId();


	// Sleep mode for RP2040 is not supported yet :-(
	// sleep();

	// This code part is power saving and sleeps as long as possible
	osEvent event = osSignalWait(0, osWaitForever);

	digitalWrite(LED_BUILTIN, HIGH);
	if (event.status == osEventSignal)
	{
		// Serial.printf("Signal %02X", event.value.signals);
		if ((event.value.signals & SIGNAL_JOIN) == SIGNAL_JOIN)
		{
			APP_LOG("APP", "Start Join");
			init_lora();
			digitalWrite(LED_BLUE, HIGH);
		}
		if ((event.value.signals & SIGNAL_JOIN_SUCCESS) == SIGNAL_JOIN_SUCCESS)
		{
			DualSerial("AT+JOIN=SUCCESS\n");
			digitalWrite(LED_BLUE, LOW);
		}
		if ((event.value.signals & SIGNAL_JOIN_FAIL) == SIGNAL_JOIN_FAIL)
		{
			DualSerial("AT+JOIN=FAIL\n");
			digitalWrite(LED_BLUE, LOW);
		}
		if ((event.value.signals & SIGNAL_UNCONF_TX) == SIGNAL_UNCONF_TX)
		{
			DualSerial("AT+SEND=SUCCESS\n");
			digitalWrite(LED_BLUE, LOW);
		}
		if ((event.value.signals & SIGNAL_CONF_TX_ACK) == SIGNAL_CONF_TX_ACK)
		{
			DualSerial("AT+SEND=SUCCESS\n");
			digitalWrite(LED_BLUE, LOW);
		}
		if ((event.value.signals & SIGNAL_CONF_TX_NAK) == SIGNAL_CONF_TX_NAK)
		{
			DualSerial("AT+SEND=FAIL\n");
			digitalWrite(LED_BLUE, LOW);
		}
		if ((event.value.signals & SIGNAL_RX) == SIGNAL_RX)
		{
			APP_LOG("APP", "RX finished %d bytes, RSSI %d, SNR %d\n", g_rx_data_len, g_last_rssi, g_last_snr);
			DualSerial("RX:%d:%d:%d:%d:", g_last_fport, g_rx_data_len, g_last_rssi, g_last_snr);
			for (int idx = 0; idx < g_rx_data_len; idx++)
			{
				DualSerial("%02X", g_rx_lora_data[idx]);
			}
			DualSerial("\nOK\n");
			digitalWrite(LED_BLUE, LOW);
		}
		if ((event.value.signals & SIGNAL_SEND) == SIGNAL_SEND)
		{
			// Start BME680 measurements
			start_bme();

			digitalWrite(LED_BLUE, HIGH);

			// Get battery level
			batt_s batt_level;

			// Read BME680
			read_bme();

			batt_level.batt16 = read_batt() / 10;
			g_env_data.batt_1 = batt_level.batt8[1];
			g_env_data.batt_2 = batt_level.batt8[0];

			if (g_lorawan_settings.lorawan_enable)
			{
				if (g_lpwan_has_joined)
				{
					lmh_error_status result = send_lora_packet((uint8_t *)&g_env_data, ENV_DATA_LEN);
					switch (result)
					{
					case LMH_SUCCESS:
						APP_LOG("APP", "Packet queued successful");
						break;
					case LMH_BUSY:
						APP_LOG("APP", "LoRa transceiver is busy, retry in next cycle!");
						break;
					case LMH_ERROR:
						APP_LOG("APP", "Packet error, too big to send with current DR");
						lmh_datarate_set(DR_5, false);
						APP_LOG("APP", "Trigger resending the packet with DR 5");
						osSignalSet(loop_thread, SIGNAL_SEND);
						break;
					}
				}
			}
			else
			{
				if (!send_p2p_packet((uint8_t *)&g_env_data, ENV_DATA_LEN))
				{
					APP_LOG("APP", "P2P send failed");
				}
				else
				{
					APP_LOG("APP", "P2P packet enqueued");
				}
			}
		}
		digitalWrite(LED_BUILTIN, LOW);
	}
	yield();
}