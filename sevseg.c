#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#define SEVSEG_DRV_NAME		"sevseg"

#define BB_DELAY			1
#define NUM_ELEMENTS		3
#define REFRESH_PERIOD_DEF	5

#define MS_TO_NS(x)			(x * 1000000L)

static struct gpio_descs* select_pins_array;
static struct gpio_desc* buffer_clk;
static struct gpio_desc* buffer_mosi;

static struct hrtimer refresh_timer;
static ktime_t refresh_period;
static unsigned char active_element; /* set to 0 by default */
static uint8_t fbuf[NUM_ELEMENTS] = {0xff, 0xff, 0xff};

static void set_clk(int val)
{
	gpiod_set_value(buffer_clk, val);
}

static void set_mosi(int val)
{
	gpiod_set_value(buffer_mosi, val);
}

static void element_send_data(uint8_t val)
{
	int idx;

	for(idx = 7; idx >= 0; idx -= 1)
	{
		set_mosi((val >> idx) & 1);
		udelay(BB_DELAY);
		set_clk(0);
		udelay(BB_DELAY);
		set_clk(1);
	}

	udelay(BB_DELAY);
	set_mosi(1);
}

static void element_on(int idx)
{
	gpiod_set_value(select_pins_array->desc[idx], 0);
}

static void element_off(int idx)
{
	gpiod_set_value(select_pins_array->desc[(idx + NUM_ELEMENTS - 1) % NUM_ELEMENTS], 1);
}

enum hrtimer_restart refresh_timer_handler(struct hrtimer *timer)
{
	ktime_t timer_time;
	
	element_off(active_element);
	element_send_data(fbuf[active_element]);
	element_on(active_element);
	active_element = (active_element + 1) % NUM_ELEMENTS;

	timer_time = hrtimer_cb_get_time(&refresh_timer);
	hrtimer_forward(&refresh_timer, timer_time, refresh_period);

	return HRTIMER_RESTART;
}

static ssize_t set_data_callback(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	if(count != NUM_ELEMENTS)
		printk("set_data_callback: Error! The data is %d bytes long, should be %d", count, NUM_ELEMENTS);
	else
		memcpy(fbuf, buf, 3);

	printk("set_data_callback: The data is set");

	return count;
}

static ssize_t set_period_callback(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	ktime_t timer_time;
	long period;

	if (kstrtol(buf, 10, &period) < 0)
		return -EINVAL;

	if (period > 0)
		refresh_period = ktime_set(0, MS_TO_NS(period));
	else
		refresh_period = ktime_set(0, MS_TO_NS(REFRESH_PERIOD_DEF));

	timer_time = hrtimer_cb_get_time(&refresh_timer);
	hrtimer_forward(&refresh_timer, timer_time, refresh_period);
	printk("set_period_callback: The timer period is %ld", period);

	return count;
}

/****************************/
/* Initialization / Cleanup */
/****************************/

static int sevseg_probe(struct platform_device *pdev);
static int sevseg_remove(struct platform_device *pdev);

static struct of_device_id sevseg_of_match[] = {
	{
		.compatible = SEVSEG_DRV_NAME,
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sevseg_of_match);

static struct platform_driver sevseg_driver = {
	.probe = sevseg_probe,
	.remove = sevseg_remove,
	.driver = {
		.name = SEVSEG_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sevseg_of_match,
	},
};

static DEVICE_ATTR(period, 0660, NULL, set_period_callback);
static DEVICE_ATTR(data, 0660, NULL, set_data_callback);

static struct attribute *sevseg_attr[] = {
		&dev_attr_period.attr,
		&dev_attr_data.attr,
		NULL
	};

static struct attribute_group sevseg_attr_group = {
	.attrs = sevseg_attr
};

static struct class *s_pDeviceClass;
static struct device *s_pDevObject;

static int sevseg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	printk("sevseg_probe: BEGIN\n");

	select_pins_array = gpiod_get_array(dev, "select", GPIOD_OUT_HIGH);
	if(IS_ERR(select_pins_array)) {
		printk("sevseg_probe - Error! Could not setup the GPIO\n");
		return -1 * IS_ERR(select_pins_array);
	}

	buffer_clk = gpiod_get(dev, "buffer-clk", GPIOD_OUT_HIGH);
	if(IS_ERR(buffer_clk)) {
		printk("sevseg_probe - Error! Could not setup the GPIO\n");
		return -1 * IS_ERR(buffer_clk);
	}

	buffer_mosi = gpiod_get(dev, "buffer-mosi", GPIOD_OUT_HIGH);
	if(IS_ERR(buffer_mosi)) {
		printk("sevseg_probe - Error! Could not setup the GPIO\n");
		return -1 * IS_ERR(buffer_mosi);
	}

	s_pDeviceClass = class_create(SEVSEG_DRV_NAME);
	if (s_pDeviceClass == NULL) {
		printk("sevseg_probe: Error! cannot create class");
		return -1;
	}

	s_pDevObject = device_create(s_pDeviceClass, NULL, 0, NULL,
					SEVSEG_DRV_NAME);
	if (s_pDevObject == NULL) {
		printk("sevseg_probe: Error! cannot create device");
		goto err_class_create;
	}

	if (sysfs_create_group(&s_pDevObject->kobj, &sevseg_attr_group)) {
		printk("sevseg_probe: Error! cannot create sysfs files");
		goto err_device_create;
	}

	refresh_period = ktime_set(0, MS_TO_NS(REFRESH_PERIOD_DEF));
	hrtimer_init(&refresh_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	refresh_timer.function = &refresh_timer_handler;
	hrtimer_start(&refresh_timer, refresh_period, HRTIMER_MODE_REL);

	printk("sevseg_probe: END\n");

	return 0;

err_device_create:
	device_destroy(s_pDeviceClass, 0);

err_class_create:
	class_destroy(s_pDeviceClass);

	printk("sevseg_probe: END with errors\n");

	return -1;
}

static int sevseg_remove(struct platform_device *pdev) {
	printk("sevseg_remove: BEGIN\n");

	gpiod_put_array(select_pins_array);
	gpiod_put(buffer_clk);
	gpiod_put(buffer_mosi);

	hrtimer_cancel(&refresh_timer);
	sysfs_remove_group(&s_pDevObject->kobj, &sevseg_attr_group);
	device_destroy(s_pDeviceClass, 0);
	class_destroy(s_pDeviceClass);

	printk("sevseg_remove: END\n");

	return 0;
}

int sevseg_init(void)
{
	int result;

	printk("sevseg_init: driver initialization...");

	result = platform_driver_register(&sevseg_driver);
	if (result) {
		printk("sevseg_init: driver register error %d\n", result);
		return result;
	}

	return 0;
}

void sevseg_cleanup(void)
{
	printk("sevseg_cleanup: driver removed!");

	platform_driver_unregister(&sevseg_driver);
}

MODULE_LICENSE("GPL");
module_init(sevseg_init);
module_exit(sevseg_cleanup);
