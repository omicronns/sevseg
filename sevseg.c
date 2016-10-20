#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#define INVALID_NUMBER 0xFFFFFFFF

#define SEG1_PIN 17
#define SEG2_PIN 27
#define SEG3_PIN 22
#define SEG4_PIN 13
#define SEG5_PIN 19
#define SEG6_PIN 26
#define SEG7_PIN 20
#define SEG8_PIN 21

#define SELECT_A_PIN 23
#define SELECT_B_PIN 24
#define SELECT_C_PIN 25

#define SEVSEG_DRV_NAME      "sevseg"
#define SEG_ARRAY_FLAGS      GPIOF_OUT_INIT_HIGH
#define SEG_ARRAY_LABEL      SEVSEG_DRV_NAME
#define SELECT_ARRAY_FLAGS   GPIOF_OUT_INIT_LOW
#define SELECT_ARRAY_LABEL   SEVSEG_DRV_NAME

#define SEG_ON  0
#define SEG_OFF 1

#define SEG_ARRAY_SIZE     8
#define SEG_MAP_SIZE      11
#define SELECT_ARRAY_SIZE  3
#define SELECT_MAP_SIZE    6

#define NUM_ELEMENTS_DEF   6
#define REFRESH_PERIOD_DEF 3

#define MS_TO_NS(x)        (x * 1000000L)

static struct gpio select_pins_array[SELECT_ARRAY_SIZE] = {
	{SELECT_A_PIN, SELECT_ARRAY_FLAGS, SELECT_ARRAY_LABEL},
	{SELECT_B_PIN, SELECT_ARRAY_FLAGS, SELECT_ARRAY_LABEL},
	{SELECT_C_PIN, SELECT_ARRAY_FLAGS, SELECT_ARRAY_LABEL},
};

static unsigned char map_select_pins[SELECT_MAP_SIZE][SELECT_ARRAY_SIZE] = {
	  {0, 0, 0}
	, {1, 0, 0}
	, {0, 1, 0}
	, {1, 1, 0}
	, {0, 0, 1}
	, {1, 0, 1}
};

static struct gpio seg_pins_array[SEG_ARRAY_SIZE] = {
	{SEG1_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL},
	{SEG2_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL},
	{SEG3_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL},
	{SEG4_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL},
	{SEG5_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL},
	{SEG6_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL},
	{SEG7_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL},
	{SEG8_PIN, SEG_ARRAY_FLAGS, SEG_ARRAY_LABEL}
};

/*
*  1_
*6 | | 2
* 7 -
*5 |_| 3
*   4
*/

static unsigned char map_num_to_seg_pins[SEG_MAP_SIZE][SEG_ARRAY_SIZE] = {
{SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_OFF, SEG_OFF}, /*0*/
{SEG_OFF, SEG_ON,  SEG_ON,  SEG_OFF, SEG_OFF, SEG_OFF, SEG_OFF, SEG_OFF}, /*1*/
{SEG_ON,  SEG_ON,  SEG_OFF, SEG_ON,  SEG_ON,  SEG_OFF, SEG_ON,  SEG_OFF}, /*2*/
{SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_OFF, SEG_OFF, SEG_ON,  SEG_OFF}, /*3*/
{SEG_OFF, SEG_ON,  SEG_ON,  SEG_OFF, SEG_OFF, SEG_ON,  SEG_ON,  SEG_OFF}, /*4*/
{SEG_ON,  SEG_OFF, SEG_ON,  SEG_ON,  SEG_OFF, SEG_ON,  SEG_ON,  SEG_OFF}, /*5*/
{SEG_ON,  SEG_OFF, SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_OFF}, /*6*/
{SEG_ON,  SEG_ON,  SEG_ON,  SEG_OFF, SEG_OFF, SEG_OFF, SEG_OFF, SEG_OFF}, /*7*/
{SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_OFF}, /*8*/
{SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_OFF, SEG_ON,  SEG_ON,  SEG_OFF}, /*9*/
{SEG_ON,  SEG_OFF, SEG_OFF, SEG_ON,  SEG_ON,  SEG_ON,  SEG_ON,  SEG_OFF}  /*E*/
};

static struct hrtimer refresh_timer;
static ktime_t refresh_period;
static long nb_of_elements = NUM_ELEMENTS_DEF;
static long sevseg_number = 123456;
static unsigned char active_element; /* set to 0 by default */

static void set_sevseg_digit(unsigned char digit)
{
	int idx;

	for (idx = 0; idx <  SEG_ARRAY_SIZE; idx++)
		gpio_set_value(seg_pins_array[idx].gpio,
				map_num_to_seg_pins[digit][idx]);
}

static void activate_element(unsigned char active_element)
{
	int idx;

	for (idx = 0; idx < SELECT_ARRAY_SIZE; idx++)
		gpio_set_value(select_pins_array[idx].gpio,
				map_select_pins[active_element][idx]);
}

enum hrtimer_restart refresh_timer_handler(struct hrtimer *timer)
{
	ktime_t timer_time;
	unsigned char idx;
	long active_digit;

	if (sevseg_number != INVALID_NUMBER)  {
		active_digit = sevseg_number;
		for (idx = 0; idx < nb_of_elements - active_element - 1; idx++)
			active_digit = active_digit / 10;
		active_digit = active_digit % 10;
	} else
		active_digit = SEG_MAP_SIZE - 1;

	activate_element(active_element);
	set_sevseg_digit(active_digit);
	if (active_element > 0)
		active_element--;
	else
		active_element = nb_of_elements - 1;

	timer_time = hrtimer_cb_get_time(&refresh_timer);
	hrtimer_forward(&refresh_timer, timer_time, refresh_period);

	return HRTIMER_RESTART;
}

static ssize_t set_number_callback(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t count)
{
	long max_number = 1;
	unsigned char idx;

	if (kstrtol(buf, 10, &sevseg_number) < 0)
		return -EINVAL;

	for (idx = 0; idx < nb_of_elements; idx++)
		max_number = max_number * 10;

	if (sevseg_number > max_number)
		sevseg_number = INVALID_NUMBER;

	pr_info(SEVSEG_DRV_NAME ": The sevseg number is %ld", sevseg_number);

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
	pr_info(SEVSEG_DRV_NAME ": The timer period is %ld", period);

	return count;
}

static ssize_t set_elements_callback(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	long elements;

	if (kstrtol(buf, 10, &elements) < 0)
		return -EINVAL;

	if ((elements > 0) && (elements <= SELECT_MAP_SIZE))
		nb_of_elements = elements;
	else
		nb_of_elements = NUM_ELEMENTS_DEF;

	active_element = nb_of_elements - 1;
	pr_info(SEVSEG_DRV_NAME ": Number of elements %ld", nb_of_elements);

	return count;
}

/****************************/
/* Initialization / Cleanup */
/****************************/

static DEVICE_ATTR(period, 0660, NULL, set_period_callback);
static DEVICE_ATTR(elements, 0660, NULL, set_elements_callback);
static DEVICE_ATTR(number, 0660, NULL, set_number_callback);

static struct attribute *sevseg_attr[] = {
		&dev_attr_period.attr,
		&dev_attr_elements.attr,
		&dev_attr_number.attr,
		NULL
	};

static struct attribute_group sevseg_attr_group = {
	.attrs = sevseg_attr
};

static struct class *s_pDeviceClass;
static struct device *s_pDevObject;
static const struct of_device_id sevseg_of_match[] = {
	{ .compatible = "sevseg", },
	{},
};
MODULE_DEVICE_TABLE(of, sevseg_of_match);

static struct platform_driver sevseg_driver = {
	.driver = {
		.name  = SEVSEG_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sevseg_of_match),
	}
};

int sevseg_init(void)
{
	int result;

	pr_info(SEVSEG_DRV_NAME ": driver initialization...");

	active_element = nb_of_elements - 1;

	result = platform_driver_register(&sevseg_driver);
	if (result) {
		pr_info(SEVSEG_DRV_NAME ": driver register error %d\n", result);
		goto err_platform_reg;
	}

	result = gpio_request_array(seg_pins_array, SEG_ARRAY_SIZE);
	if (result) {
		pr_info(SEVSEG_DRV_NAME ": Error requesting segment GPIOs\n");
		goto err_gpio_req_seg;
	}

	result = gpio_request_array(select_pins_array, SELECT_ARRAY_SIZE);
	if (result) {
		pr_info(SEVSEG_DRV_NAME ": Error requesting select GPIOs\n");
		goto err_gpio_req_sel;
	}

	s_pDeviceClass = class_create(THIS_MODULE, SEVSEG_DRV_NAME);
	if (s_pDeviceClass == NULL) {
		pr_info(SEVSEG_DRV_NAME ": cannot create class");
		goto err_class_create;
	}

	s_pDevObject = device_create(s_pDeviceClass, NULL, 0, NULL,
					SEVSEG_DRV_NAME);
	if (s_pDevObject == NULL) {
		pr_info(SEVSEG_DRV_NAME ": cannot create device");
		goto err_device_create;
	}

	result = sysfs_create_group(&s_pDevObject->kobj, &sevseg_attr_group);
	if (result) {
		pr_info(SEVSEG_DRV_NAME ": cannot create sysfs files");
		goto err_sysfs_file;
	}

	refresh_period = ktime_set(0, MS_TO_NS(REFRESH_PERIOD_DEF));
	hrtimer_init(&refresh_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	refresh_timer.function = &refresh_timer_handler;
	hrtimer_start(&refresh_timer, refresh_period, HRTIMER_MODE_REL);

	return 0;


err_sysfs_file:
	sysfs_remove_group(&s_pDevObject->kobj, &sevseg_attr_group);

err_device_create:
	device_destroy(s_pDeviceClass, 0);

err_class_create:
	class_destroy(s_pDeviceClass);

err_gpio_req_sel:
	gpio_free_array(select_pins_array, SELECT_ARRAY_SIZE);

err_gpio_req_seg:
	gpio_free_array(seg_pins_array, SEG_ARRAY_SIZE);

err_platform_reg:
	platform_driver_unregister(&sevseg_driver);

	return result;
}

void sevseg_cleanup(void)
{
	pr_info(SEVSEG_DRV_NAME ": driver removed!");

	hrtimer_cancel(&refresh_timer);
	sysfs_remove_group(&s_pDevObject->kobj, &sevseg_attr_group);
	device_destroy(s_pDeviceClass, 0);
	class_destroy(s_pDeviceClass);

	gpio_free_array(seg_pins_array, SEG_ARRAY_SIZE);
	gpio_free_array(select_pins_array, SELECT_ARRAY_SIZE);

	platform_driver_unregister(&sevseg_driver);
}

MODULE_LICENSE("GPL");
module_init(sevseg_init);
module_exit(sevseg_cleanup);

