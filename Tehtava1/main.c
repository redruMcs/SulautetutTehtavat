#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

// Tavoittelen kolmea pistettä. Tein kaikki 3 kohtaa viikkotehtävästä.

int tila = 0; // 0 = idle, 1 = red, 2 = yellow, 3 = green, 4 = pause, 5 = flashing yellow 
int saved_tila = 0; // tallennettu entinen tila
int saved_tila5 = 0;

// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
// static const struct gpio_dt_spec blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

// Configure buttons
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

// Red led thread initialization
#define STACKSIZE 500
#define PRIORITY 5

void red_led_task(void *, void *, void*);
K_THREAD_DEFINE(red_thread,STACKSIZE,red_led_task,NULL,NULL,NULL,PRIORITY,0,0);
void yellow_led_task(void *, void *, void*);
K_THREAD_DEFINE(yellow_thread,STACKSIZE,yellow_led_task,NULL,NULL,NULL,PRIORITY,0,0);
void green_led_task(void *, void *, void*);
K_THREAD_DEFINE(green_thread,STACKSIZE,green_led_task,NULL,NULL,NULL,PRIORITY,0,0);
void yellow_blink_task(void *, void *, void*);
K_THREAD_DEFINE(yellow_blink_thread,STACKSIZE,yellow_blink_task,NULL,NULL,NULL,PRIORITY,0,0);


void button_0_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	printk("Button 0 pressed\n");
	if (tila == 4) { // jos ollaan jo pause-tilassa
		tila = saved_tila; // palautetaan vanha tila
		printk("Pause stopped, returning to state %d\n", tila);
	} else { // muutoin
		saved_tila = tila; // nykyinen tila talteen
		tila = 4; // vaihdetaan pause tilaan
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
	init_led();

	int ret = init_button();
	if (ret < 0) {
		return 0;
	}

	// init state
	tila = 1;

	return 0;
}

// Button initialization
int init_button() {

	int ret;

	// Pause nappi

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

	// Punainen nappi

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

	// Keltainen nappi

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

	// Vihreä nappi

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
	
	// Vilkkuva keltainen nappi

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

// Initialize leds
int  init_led() {

	// Led pin initialization
	int ret;
	
	ret = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Error: Led configure failed\n");		
		return ret;
	}
	
	gpio_pin_set_dt(&red,0);

	// Led pin initialization
	ret = gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Error: Led configure failed\n");		
		return ret;
	}
	
	gpio_pin_set_dt(&green,0);

	printk("Led initialized ok\n");
	
	return 0;
}

// Task to handle red led
void red_led_task(void *, void *, void*) {
	
	printk("Red led thread started\n");
	while (true) {
		if (tila == 1) {

			gpio_pin_set_dt(&red,1);
			printk("Red on\n");
			
			k_sleep(K_SECONDS(1));
			
			gpio_pin_set_dt(&red,0);
			printk("Red off\n");
			
			k_sleep(K_SECONDS(1));
			
			if (tila != 4) tila = 2;
		}
		k_yield();
	}
}

// Task to handle yellow led
void yellow_led_task(void *, void *, void*) {
	
	printk("Yellow led thread started\n");
	while (true) {
		if (tila == 2) {

			gpio_pin_set_dt(&red,1);
			gpio_pin_set_dt(&green,1);
			printk("Yellow on\n");
			
			k_sleep(K_SECONDS(1));
			
			gpio_pin_set_dt(&red,0);
			gpio_pin_set_dt(&green,0);
			printk("Yellow off\n");
			
			k_sleep(K_SECONDS(1));
			
			if (tila != 4) tila = 3;
		}
		k_yield();
	}
}

// Task to handle green led
void green_led_task(void *, void *, void*) {
	
	printk("Green led thread started\n");
	while (true) {
		if (tila == 3) {
			
			gpio_pin_set_dt(&green,1);
			printk("Green on\n");
			
			k_sleep(K_SECONDS(1));
			
			gpio_pin_set_dt(&green,0);
			printk("Green off\n");
			
			k_sleep(K_SECONDS(1));

			if (tila != 4) tila = 1;
		}
		k_yield();
	}
}

// Task to handle blinking yellow led
void yellow_blink_task(void *, void *, void*) {
 
	printk("Blink yellow thread started\n");
	while (true) {
		if (tila == 5) { // 5 = vilkkuva keltainen
 
			gpio_pin_set_dt(&red,1);
			gpio_pin_set_dt(&green,1);
			printk("Blink yellow on\n");
			
			k_sleep(K_MSEC(1000));
			
			gpio_pin_set_dt(&red,0);
			gpio_pin_set_dt(&green,0);
			printk("Blink yellow off\n");
			
			k_sleep(K_MSEC(1000));
		}
		k_yield();
	}
}