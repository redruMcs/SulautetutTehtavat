#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/timing/timing.h>

// 1. pisteen suoritus.
// Tavoittelen kolmea pistettä, jotka teen myöhemmin


// Config
#define STACKSIZE 500
#define PRIORITY 5
#define TRANSITION_PAUSE_MS 100   // Pause between steps when replaying a sequence
#define MAX_SEQUENCE 20           // Maximum number of commands in sequence

// State
volatile int tila = 0;         // 0 = idle, 1 = red, 2 = yellow, 3 = green, 4 = pause, 5 = flashing yellow 
volatile int saved_tila = 0;   // State saved when pause (button 0) is pressed
volatile int saved_tila5 = 0;  // State saved when flashing yellow (button 4) is pressed
volatile int led_time_ms = 0;  // Global LED timing variable

// Timing
uint64_t red_task_ns = 0;
uint64_t yellow_task_ns = 0;
uint64_t green_task_ns = 0;

// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

// Button configurations
#define BUTTON_0 DT_ALIAS(sw0)
#define BUTTON_1 DT_ALIAS(sw1)
#define BUTTON_2 DT_ALIAS(sw2)
#define BUTTON_3 DT_ALIAS(sw3)
#define BUTTON_4 DT_ALIAS(sw4)

static const struct gpio_dt_spec button_0 = GPIO_DT_SPEC_GET_OR(BUTTON_0, gpios, {0});
static const struct gpio_dt_spec button_1 = GPIO_DT_SPEC_GET_OR(BUTTON_1, gpios, {0});
static const struct gpio_dt_spec button_2 = GPIO_DT_SPEC_GET_OR(BUTTON_2, gpios, {0});
static const struct gpio_dt_spec button_3 = GPIO_DT_SPEC_GET_OR(BUTTON_3, gpios, {0});
static const struct gpio_dt_spec button_4 = GPIO_DT_SPEC_GET_OR(BUTTON_4, gpios, {0});

static struct gpio_callback button_0_data;
static struct gpio_callback button_1_data;
static struct gpio_callback button_2_data;
static struct gpio_callback button_3_data;
static struct gpio_callback button_4_data;

void red_led_task(void *, void *, void*);
void yellow_led_task(void *, void *, void*);
void green_led_task(void *, void *, void*);
void yellow_blink_task(void *, void *, void*);
void dispatcher_task(void *, void *, void*);
void uart_task(void *, void *, void*);

K_THREAD_DEFINE(red_thread,STACKSIZE,red_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(yellow_thread,STACKSIZE,yellow_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(green_thread,STACKSIZE,green_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(yellow_blink_thread,STACKSIZE,yellow_blink_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(dis_thread,STACKSIZE,dispatcher_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(uart_thread,STACKSIZE,uart_task,NULL,NULL,NULL,PRIORITY,0,0);

// UART initialization
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

// Condition Variables
K_MUTEX_DEFINE(red_mutex);
K_CONDVAR_DEFINE(red_signal);
K_MUTEX_DEFINE(yellow_mutex);
K_CONDVAR_DEFINE(yellow_signal);
K_MUTEX_DEFINE(green_mutex);
K_CONDVAR_DEFINE(green_signal);

// Create semaphore
K_SEM_DEFINE(release_sem, 0, 1);

// Create dispatcher FIFO buffer
K_FIFO_DEFINE(dispatcher_fifo);

// FIFO dispatcher data type
struct data_t {
	void *fifo_reserved;
	char msg[20];
};

// Sequence item
struct sequence_item {
	char color;
	int time_ms;
};

// Stored sequence
static struct sequence_item sequence[MAX_SEQUENCE];
static int sequence_length = 0;

// Prototypes
int init_uart(void);
int init_led(void);
int init_button(void);


// Button handlers
void button_0_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	printk("Button 0 pressed\n");
	if (tila == 4) {
		tila = saved_tila;
		printk("Pause stopped, returning to state %d\n", tila);
	} else {
		saved_tila = tila;
		tila = 4;
		printk("Pause started, saved state %d\n", saved_tila);
	}
}

void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (tila != 4) {
		printk("Button 1 ignored (not in pause)\n");
		return;
	}
	printk("Button 1 pressed (red toggle)\n");
	gpio_pin_toggle_dt(&red);
}

void button_2_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (tila != 4) {
		printk("Button 2 ignored (not in pause)\n");
		return;
	}
	printk("Button 2 pressed (yellow toggle)\n");
	gpio_pin_toggle_dt(&red);
	gpio_pin_toggle_dt(&green);
}

void button_3_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (tila != 4) {
		printk("Button 3 ignored (not in pause)\n");
		return;
	}
	printk("Button 3 pressed (green toggle)\n");
	gpio_pin_toggle_dt(&green);
}

void button_4_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (tila == 5) {
		gpio_pin_set_dt(&red, 0);
		gpio_pin_set_dt(&green, 0);
		tila = saved_tila5;
		printk("Blink yellow stopped, returning to state %d\n", tila);
		return;
	}
	if (tila != 4) {
		printk("Button 4 ignored (not in pause)\n");
		return;
	}
	saved_tila5 = tila;
	tila = 5;
	printk("Blink yellow started, saved state %d\n", saved_tila5);
}

// Main program
int main(void)
{
	int ret;

	timing_init();
    timing_start();
	timing_t start_time = timing_counter_get();

	ret = init_button();
	if (ret < 0) {
		return 0;
	}

	ret = init_uart();
	if (ret != 0) {
		printk("UART initialization failed!\n");
		return ret;
	}

	ret = init_led();
	if (ret != 0) {
		printk("LED initialization failed!\n");
		return ret;
	}

	timing_t end_time = timing_counter_get();
	timing_stop();
    uint64_t timing_ns = timing_cycles_to_ns(timing_cycles_get(&start_time, &end_time));
	printk("Initialization: %u us\n", (uint32_t)(timing_ns / 1000));

	// init state
	tila = 1;
	return 0;
}

// UART initialization
int init_uart(void)
{
	if (!device_is_ready(uart_dev)) {
		return 1;
	}
	return 0;
}

// Button initialization
int init_button(void)
{
	int ret;

	// Pause button
	if (!gpio_is_ready_dt(&button_0)) {
		printk("Error: button 0 is not ready\n");
		return -1;
	}
	ret = gpio_pin_configure_dt(&button_0, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin (button 0)\n");
		return -1;
	}
	ret = gpio_pin_interrupt_configure_dt(&button_0, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin (button 0)\n");
		return -1;
	}
	gpio_init_callback(&button_0_data, button_0_handler, BIT(button_0.pin));
	gpio_add_callback(button_0.port, &button_0_data);
	printk("Set up button 0 ok\n");

	// Red button
	if (!gpio_is_ready_dt(&button_1)) {
		printk("Error: button 1 is not ready\n");
		return -1;
	}
	ret = gpio_pin_configure_dt(&button_1, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin (button 1)\n");
		return -1;
	}
	ret = gpio_pin_interrupt_configure_dt(&button_1, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin (button 1)\n");
		return -1;
	}
	gpio_init_callback(&button_1_data, button_1_handler, BIT(button_1.pin));
	gpio_add_callback(button_1.port, &button_1_data);
	printk("Set up button 1 ok\n");

	// Yellow button
	if (!gpio_is_ready_dt(&button_2)) {
		printk("Error: button 2 is not ready\n");
		return -1;
	}
	ret = gpio_pin_configure_dt(&button_2, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin (button 2)\n");
		return -1;
	}
	ret = gpio_pin_interrupt_configure_dt(&button_2, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin (button 2)\n");
		return -1;
	}
	gpio_init_callback(&button_2_data, button_2_handler, BIT(button_2.pin));
	gpio_add_callback(button_2.port, &button_2_data);
	printk("Set up button 2 ok\n");

	// Green button
	if (!gpio_is_ready_dt(&button_3)) {
		printk("Error: button 3 is not ready\n");
		return -1;
	}
	ret = gpio_pin_configure_dt(&button_3, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin (button 3)\n");
		return -1;
	}
	ret = gpio_pin_interrupt_configure_dt(&button_3, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin (button 3)\n");
		return -1;
	}
	gpio_init_callback(&button_3_data, button_3_handler, BIT(button_3.pin));
	gpio_add_callback(button_3.port, &button_3_data);
	printk("Set up button 3 ok\n");

	// Flashing-yellow button
	if (!gpio_is_ready_dt(&button_4)) {
		printk("Error: button 4 is not ready\n");
		return -1;
	}
	ret = gpio_pin_configure_dt(&button_4, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin (button 4)\n");
		return -1;
	}
	ret = gpio_pin_interrupt_configure_dt(&button_4, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin (button 4)\n");
		return -1;
	}
	gpio_init_callback(&button_4_data, button_4_handler, BIT(button_4.pin));
	gpio_add_callback(button_4.port, &button_4_data);
	printk("Set up button 4 ok\n");

	return 0;
}

// Initialize LEDs
int init_led(void)
{
	int ret;

	ret = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Error: Led configure failed\n");
		return ret;
	}
	gpio_pin_set_dt(&red, 0);

	ret = gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Error: Led configure failed\n");
		return ret;
	}
	gpio_pin_set_dt(&green, 0);

	printk("Led initialized ok\n");
	return 0;
}

// Task to handle red LED
void red_led_task(void *, void *, void*)
{
	printk("Red led thread started\n");
	while (true) {
		if (tila == 1) {
			timing_start();
			timing_t red_start_time = timing_counter_get();

			gpio_pin_set_dt(&red, 1);
			printk("Red on\n");

			k_sleep(K_SECONDS(1));

			gpio_pin_set_dt(&red, 0);
			printk("Red off\n");

			k_sleep(K_SECONDS(1));

			// Measure task duration
			timing_t red_end_time = timing_counter_get();
			timing_stop();
			red_task_ns = timing_cycles_to_ns(timing_cycles_get(&red_start_time, &red_end_time));
			printk("Red task: %u us\n", (uint32_t)(red_task_ns / 1000));

			if (tila != 4) tila = 2;

		} else if (tila == 4) {
			k_mutex_lock(&red_mutex, K_FOREVER);
			int rc = k_condvar_wait(&red_signal, &red_mutex, K_MSEC(50));
			k_mutex_unlock(&red_mutex);
			if (rc == 0) {
				gpio_pin_set_dt(&red, 1);
				printk("Red on (uart, %d ms)\n", led_time_ms);

				k_msleep(led_time_ms);

				gpio_pin_set_dt(&red, 0);
				printk("Red off (uart)\n");

				k_sem_give(&release_sem);
			}
		}
		k_yield();
	}
}

// Task to handle yellow LED
void yellow_led_task(void *, void *, void*)
{
	printk("Yellow led thread started\n");
	while (true) {
		timing_start();
		timing_t yellow_start_time = timing_counter_get();
		if (tila == 2) {
			gpio_pin_set_dt(&red, 1);
			gpio_pin_set_dt(&green, 1);
			printk("Yellow on\n");

			k_sleep(K_SECONDS(1));

			gpio_pin_set_dt(&red, 0);
			gpio_pin_set_dt(&green, 0);
			printk("Yellow off\n");

			k_sleep(K_SECONDS(1));

			timing_t yellow_end_time = timing_counter_get();
			timing_stop();
			yellow_task_ns = timing_cycles_to_ns(timing_cycles_get(&yellow_start_time, &yellow_end_time));
			printk("Yellow task: %u us\n", (uint32_t)(yellow_task_ns / 1000));

			if (tila != 4) tila = 3;

		} else if (tila == 4) {
			k_mutex_lock(&yellow_mutex, K_FOREVER);
			int rc = k_condvar_wait(&yellow_signal, &yellow_mutex, K_MSEC(50));
			k_mutex_unlock(&yellow_mutex);
			if (rc == 0) {
				gpio_pin_set_dt(&red, 1);
				gpio_pin_set_dt(&green, 1);
				printk("Yellow on (uart, %d ms)\n", led_time_ms);

				k_msleep(led_time_ms);

				gpio_pin_set_dt(&red, 0);
				gpio_pin_set_dt(&green, 0);
				printk("Yellow off (uart)\n");

				k_sem_give(&release_sem);
			}
		}
		k_yield();
	}
}

// Task to handle green LED
void green_led_task(void *, void *, void*)
{
	printk("Green led thread started\n");
	while (true) {
		timing_start();
		timing_t green_start_time = timing_counter_get();
		if (tila == 3) {
			gpio_pin_set_dt(&green, 1);
			printk("Green on\n");

			k_sleep(K_SECONDS(1));

			gpio_pin_set_dt(&green, 0);
			printk("Green off\n");

			k_sleep(K_SECONDS(1));

			timing_t green_end_time = timing_counter_get();
			timing_stop();
			green_task_ns = timing_cycles_to_ns(timing_cycles_get(&green_start_time, &green_end_time));
			printk("Green task: %u us\n", (uint32_t)(green_task_ns / 1000));

			// Measure entire sequence
			uint64_t total_ns = red_task_ns + yellow_task_ns + green_task_ns;
			printk("Sequence total: %u us\n", (uint32_t)(total_ns / 1000));

			if (tila != 4) tila = 1;

		} else if (tila == 4) {
			k_mutex_lock(&green_mutex, K_FOREVER);
			int rc = k_condvar_wait(&green_signal, &green_mutex, K_MSEC(50));
			k_mutex_unlock(&green_mutex);
			if (rc == 0) {
				gpio_pin_set_dt(&green, 1);
				printk("Green on (uart, %d ms)\n", led_time_ms);

				k_msleep(led_time_ms);

				gpio_pin_set_dt(&green, 0);
				printk("Green off (uart)\n");

				k_sem_give(&release_sem);
			}
		}
		k_yield();
	}
}

// Task to handle Blinking yellow LED
void yellow_blink_task(void *, void *, void*)
{
	printk("Blink yellow thread started\n");
	while (true) {
		if (tila == 5) {
			gpio_pin_set_dt(&red, 1);
			gpio_pin_set_dt(&green, 1);
			printk("Blink yellow on\n");

			k_sleep(K_MSEC(1000));

			gpio_pin_set_dt(&red, 0);
			gpio_pin_set_dt(&green, 0);
			printk("Blink yellow off\n");

			k_sleep(K_MSEC(1000));
		}
		k_yield();
	}
}

// UART task
void uart_task(void *unused1, void *unused2, void *unused3)
{
	char rc = 0;			// Stores one received character
	char uart_msg[20];		// Stores the entire command.
	memset(uart_msg, 0, sizeof(uart_msg));
	int uart_msg_cnt = 0;	// Keeps track where the next character should go

	while (true) {
		// Ask UART if data available
		if (uart_poll_in(uart_dev, &rc) == 0) {
			if (rc != '\r') {
				// Prevent buffer overflow
				if (uart_msg_cnt < (int)sizeof(uart_msg) - 1) {
					uart_msg[uart_msg_cnt] = rc;
					uart_msg_cnt++;
				}
			} else {
				uart_msg[uart_msg_cnt] = '\0';
				printk("UART msg: %s\n", uart_msg);

				// Allocate memory
				struct data_t *buf = k_malloc(sizeof(struct data_t));
				if (buf == NULL) {
					return;
				}
				// Copy UART message to dispatcher data
				snprintf(buf->msg, sizeof(buf->msg), "%s", uart_msg);

				// Put dispatcher data to FIFO buffer
				k_fifo_put(&dispatcher_fifo, buf);

				// Reset UART message counter
				uart_msg_cnt = 0;

				// Clear UART message buffer
				memset(uart_msg, 0, sizeof(uart_msg));
			}
		}
		k_msleep(10);
	}
}

// Dispatcher task
void dispatcher_task(void *unused1, void *unused2, void *unused3)
{
	while (true) {
		struct data_t *rec_item = k_fifo_get(&dispatcher_fifo, K_FOREVER);
		char command[20];
		memcpy(command, rec_item->msg, sizeof(command));
		k_free(rec_item);

		printk("Dispatcher: %s\n", command);

		if (tila != 4) {
			printk("Ignored '%s': not in pause mode\n", command);
			continue;
		}

		if (strcmp(command, "T") == 0) {
			printk("Repeat sequence (%d steps)\n", sequence_length);
			if (sequence_length == 0) {
				printk("No sequence stored\n");
				continue;
			}
			for (int i = 0; i < sequence_length; i++) {
				if (tila != 4) {
					printk("Pause ended, aborting sequence\n");
					break;
				}
				led_time_ms = sequence[i].time_ms;
				switch (sequence[i].color) {
				case 'R':
					printk("Sequence: R,%d\n", led_time_ms);
					k_condvar_broadcast(&red_signal);
					break;
				case 'Y':
					printk("Sequence: Y,%d\n", led_time_ms);
					k_condvar_broadcast(&yellow_signal);
					break;
				case 'G':
					printk("Sequence: G,%d\n", led_time_ms);
					k_condvar_broadcast(&green_signal);
					break;
				}
				// Wait until LED task finishes
				k_sem_take(&release_sem, K_FOREVER);

				// Pause before light changes to next color in the sequence
				k_msleep(TRANSITION_PAUSE_MS);
			}
			continue;
		}

		char color;
		int time_ms;

		if (sscanf(command, "%c,%d", &color, &time_ms) == 2) {
			if (color != 'R' && color != 'Y' && color != 'G') {
				printk("Unknown color: %c\n", color);
				continue;
			}

			if (sequence_length < MAX_SEQUENCE) {
				sequence[sequence_length].color = color;
				sequence[sequence_length].time_ms = time_ms;
				sequence_length++;
				printk("Added to sequence: %c,%d\n", color, time_ms);
			} else {
				printk("Sequence full!\n");
			}

			led_time_ms = time_ms;

			switch (color) {
			case 'R':
				k_condvar_broadcast(&red_signal);
				break;
			case 'Y':
				k_condvar_broadcast(&yellow_signal);
				break;
			case 'G':
				k_condvar_broadcast(&green_signal);
				break;
			}
			// Wait until LED task finishes
			k_sem_take(&release_sem, K_FOREVER);
		} else {
			printk("Unrecognized command: %s\n", command);
		}
	}
}