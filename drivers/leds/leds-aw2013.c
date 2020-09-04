/*
 * Copyright (c) 2015, 2018 The Linux Foundation. All rights reserved.
 * Copyright (C) 2019 XiaoMi, Inc.
 * Copyright (C) 2020 Dimitar Yurukov <mscalindt@protonmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt)		"aw2013: " fmt

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/init.h>
#include <linux/mutex.h>

#define AW2013_I2C_NAME	"aw2013"

#define AW2013_RSTR	0x0

#define Rise_t		0x02
#define Fall_t		0x02
#define Hold_time	0x03
#define Off_time	0x03
#define Delay_time	0x00
#define Period_Num	0x00

#define MAX_BRIGHTNESS_RED	255
#define MAX_BRIGHTNESS_GREEN	255
#define MAX_BRIGHTNESS_BLUE	255

static int aw2013_has_inited;
bool blink_frequency_adjust;

enum led_colors {
	LED_RED,
	LED_GREEN,
	LED_BLUE,
	LED_WHITE,
	LED_COLORS_MAX
};

struct rgb_info {
	int blinking;
	int brightness;
	int on_time;
	int off_time;
};

struct aw2013_led_data {
	struct led_classdev cdev;
	enum led_colors color;
};

struct aw2013_leds_priv {
	int num_leds;
	struct aw2013_led_data leds[];
};

struct aw2013_dev_data {
	struct i2c_client	*i2c;
	struct regulator	*regulator;
	struct aw2013_leds_priv	*leds_priv;
	struct rgb_info		leds[LED_COLORS_MAX];
	enum led_colors		current_color;
	struct mutex		led_lock;
	struct work_struct	set_color_work;
};
static struct aw2013_dev_data *s_aw2013;

struct aw2013_led {
	const char	*name;
	const char	*default_trigger;
	unsigned	retain_state_suspended : 1;
};

static inline int sizeof_aw2013_leds_priv(int num_leds)
{
	return sizeof(struct aw2013_leds_priv) +
		     (sizeof(struct aw2013_led_data) * num_leds);
}

static int aw2013_i2c_write(unsigned char cmd, unsigned char data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(s_aw2013->i2c, cmd, data);

	return ret;
}

enum led_colors devname_to_color(const char *dev_name)
{
	if (!strcmp(dev_name, "red"))
		return LED_RED;
	else if (!strcmp(dev_name, "green"))
		return LED_GREEN;
	else if (!strcmp(dev_name, "blue"))
		return LED_BLUE;
	else if (!strcmp(dev_name, "white"))
		return LED_WHITE;

	return LED_COLORS_MAX;
}

int aw2013_set_color_singlecolor(struct aw2013_dev_data *aw2013,
				 enum led_colors color)
{
	unsigned char red_on = 0, green_on = 0, blue_on = 0;
	unsigned char blink_flag = 0;

	switch (color) {
	case LED_RED:
		red_on = 1;
		aw2013->leds[LED_GREEN].brightness = 0;
		aw2013->leds[LED_BLUE].brightness = 0;

		if (!aw2013->leds[LED_RED].brightness)
			red_on = 0;

		if (aw2013->leds[LED_RED].blinking) {
			blink_flag = 0x10;
			aw2013_i2c_write(0x0, 0x54);
		} else {
			blink_flag = 0x0;
		}

		break;
	case LED_GREEN:
		green_on = 1;
		aw2013->leds[LED_RED].brightness = 0;
		aw2013->leds[LED_BLUE].brightness = 0;

		if (!aw2013->leds[LED_GREEN].brightness)
			green_on = 0;

		if (aw2013->leds[LED_GREEN].blinking) {
			blink_flag = 0x10;
			aw2013_i2c_write(0x0, 0x54);
		} else {
			blink_flag = 0x0;
		}

		break;
	case LED_BLUE:
		blue_on = 1;
		aw2013->leds[LED_RED].brightness = 0;
		aw2013->leds[LED_GREEN].brightness = 0;

		if (!aw2013->leds[LED_BLUE].brightness)
			blue_on = 0;

		if (aw2013->leds[LED_BLUE].blinking) {
			blink_flag = 0x10;
			aw2013_i2c_write(0x0, 0x54);
		} else {
			blink_flag = 0x0;
		}

		break;
	default:
		goto rgb_exit;
	}

	if (red_on || green_on || blue_on) {
		if (!aw2013_has_inited) {
			aw2013_i2c_write(0x0, 0x55);
			aw2013_i2c_write(0x01, 0x1);
			mdelay(1);
			aw2013_has_inited = 1;
		}

		aw2013_i2c_write(0x01, 0xe1);
		aw2013_i2c_write(0x31, blink_flag | 0x61);
		aw2013_i2c_write(0x32, blink_flag | 0x61);
		aw2013_i2c_write(0x33, blink_flag | 0x61);
		aw2013_i2c_write(0x34, MAX_BRIGHTNESS_RED);
		aw2013_i2c_write(0x35, MAX_BRIGHTNESS_GREEN);
		aw2013_i2c_write(0x36, MAX_BRIGHTNESS_BLUE);
		aw2013_i2c_write(0x37, Rise_t << 4 | Hold_time);
		aw2013_i2c_write(0x38, Fall_t << 4 | Off_time);
		aw2013_i2c_write(0x39, Delay_time << 4 | Period_Num);
		aw2013_i2c_write(0x3a, Rise_t << 4 | Hold_time);
		aw2013_i2c_write(0x3b, Fall_t << 4 | Off_time);
		aw2013_i2c_write(0x3c, Delay_time << 4 | Period_Num);
		aw2013_i2c_write(0x3d, Rise_t << 4 | Hold_time);
		aw2013_i2c_write(0x3e, Fall_t << 4 | Off_time);
		aw2013_i2c_write(0x3f, Delay_time << 4 | Period_Num);
		aw2013_i2c_write(0x30, 0x7);
		mdelay(1);
	} else {
		goto led_off;
	}

	return 0;

led_off:
	aw2013_i2c_write(0x01, 0);
	mdelay(1);
	aw2013_i2c_write(0x30, 0);
	aw2013_i2c_write(0x34, 0);
	aw2013_i2c_write(0x35, 0);
	aw2013_i2c_write(0x36, 0);
	mdelay(1);
	aw2013_has_inited = 0;
rgb_exit:
	return 0;
}

static void set_color_delayed(struct work_struct *ws)
{
	struct aw2013_dev_data *aw2013 =
		container_of(ws, struct aw2013_dev_data, set_color_work);
	enum led_colors color;

	mutex_lock(&s_aw2013->led_lock);
	color = aw2013->current_color;
	pr_debug("color = %d, brightness = %d, blink = %d\n", color,
		 aw2013->leds[color].brightness, aw2013->leds[color].blinking);
	aw2013_set_color_singlecolor(aw2013, color);
	mutex_unlock(&s_aw2013->led_lock);
}

int aw2013_set_color(struct aw2013_dev_data *aw2013, enum led_colors color)
{
	aw2013->current_color = color;
	schedule_work(&s_aw2013->set_color_work);

	return 0;
}

static void aw2013_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	enum led_colors color;

	color = devname_to_color(led_cdev->name);
	s_aw2013->leds[color].brightness = value;
	s_aw2013->leds[color].blinking = 0;
	aw2013_set_color(s_aw2013, color);
}

static int aw2013_led_blink_set(struct led_classdev *led_cdev,
				unsigned long *delay_on,
				unsigned long *delay_off)
{
	enum led_colors color;

	color = devname_to_color(led_cdev->name);
	s_aw2013->leds[color].blinking = 1;
	s_aw2013->leds[color].brightness = 255;
	s_aw2013->leds[color].on_time = *delay_on;
	s_aw2013->leds[color].off_time = *delay_off;
	led_cdev->brightness = s_aw2013->leds[color].brightness;
	aw2013_set_color(s_aw2013, color);

	return 0;
}

static int aw2013_i2c_check_device(struct i2c_client *client)
{
	int retry_cnt = 0;
	int err;

	while (retry_cnt++ < 5) {
		msleep(10);
		err = aw2013_i2c_write(AW2013_RSTR, 0x55);
		if (!err)
			break;
	}

	return err;
}

static int aw2013_power_up(struct aw2013_dev_data *pdata,
			   struct i2c_client *client, bool enable)
{
	int err;

	pdata->regulator = devm_regulator_get(&client->dev, "rgb_led");
	if (IS_ERR(pdata->regulator)) {
		dev_err(&client->dev, "regulator get failed\n");
		err = PTR_ERR(pdata->regulator);
		pdata->regulator = NULL;
		goto exit;
	}

	if (enable) {
		err = regulator_enable(pdata->regulator);
		msleep(100);
	} else {
		err = regulator_disable(pdata->regulator);
	}

exit:
	return err;
}

static ssize_t blink_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", led_cdev->brightness);
}

static ssize_t blink_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	enum led_colors color;
	ssize_t ret;
	unsigned long blinking;

	ret = kstrtoul(buf, 10, &blinking);
	if (ret)
		return ret;

	color = devname_to_color(led_cdev->name);
	led_cdev->brightness =
			s_aw2013->leds[color].brightness = blinking ? 255 : 0;
	s_aw2013->leds[color].blinking = blinking;
	led_cdev->brightness = s_aw2013->leds[color].brightness;
	aw2013_set_color(s_aw2013, color);

	return count;
}

static DEVICE_ATTR(blink, 0664, blink_show, blink_store);

static int create_aw2013_led(const struct aw2013_led *template,
			     struct aw2013_led_data *led_dat,
			     struct device *parent)
{
	int err;

	led_dat->cdev.name = template->name;
	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->color = devname_to_color(template->name);
	led_dat->cdev.brightness = 0;
	if (!template->retain_state_suspended)
		led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led_dat->cdev.blink_set = aw2013_led_blink_set;
	led_dat->cdev.brightness_set = aw2013_led_set;

	err = led_classdev_register(parent, &led_dat->cdev);
	if (err < 0) {
		pr_err("Failed to create class (err=%d)\n", err);
		return err;
	}

	err = device_create_file(led_dat->cdev.dev, &dev_attr_blink);
	if (err < 0)
		pr_err("Failed to create sysfs file (err=%d)\n", err);

	return err;
}

static void delete_aw2013_led(struct aw2013_led_data *led)
{
	device_remove_file(led->cdev.dev, &dev_attr_blink);
	led_classdev_unregister(&led->cdev);
}

static struct aw2013_leds_priv *aw2013_leds_create_of(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	struct device_node *child;
	struct aw2013_leds_priv *priv;
	int count, ret;

	/* count LEDs in this device, so we know how much to allocate */
	count = of_get_child_count(np);
	if (!count)
		return ERR_PTR(-ENODEV);

	priv = devm_kzalloc(&client->dev, sizeof_aw2013_leds_priv(count),
			    GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	for_each_child_of_node(np, child) {
		struct aw2013_led led = {};

		led.name =
			of_get_property(child, "label", NULL) ? : child->name;
		led.default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);
		led.retain_state_suspended =
			of_property_read_bool(child, "retain-state-suspended");

		ret = create_aw2013_led(&led, &priv->leds[priv->num_leds++],
					&client->dev);
		if (ret < 0) {
			of_node_put(child);
			goto err;
		}
	}

	return 0;

err:
	for (count = priv->num_leds - 2; count >= 0; count--)
		delete_aw2013_led(&priv->leds[count]);

	devm_kfree(&client->dev, priv);

	return ERR_PTR(-ENODEV);
}

static int aw2013_led_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct aw2013_leds_priv *priv;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality failed\n");
		err = -ENODEV;
		goto exit0;
	}

	s_aw2013 = kzalloc(sizeof(struct aw2013_dev_data), GFP_KERNEL);
	if (!s_aw2013) {
		pr_err("Memory allocation failed\n");
		err = -ENOMEM;
		goto exit0;
	}

	s_aw2013->i2c = client;
	i2c_set_clientdata(client, s_aw2013);

	mutex_init(&s_aw2013->led_lock);

	INIT_WORK(&s_aw2013->set_color_work, set_color_delayed);

	err = aw2013_power_up(s_aw2013, client, true);
	if (err) {
		pr_err("Regulator failed\n");
		goto exit1;
	}

	err = aw2013_i2c_check_device(client);
	if (err) {
		pr_err("Cannot write data to device\n");
		goto exit1;
	}

	priv = aw2013_leds_create_of(client);
	if (IS_ERR(priv)) {
		err = -EINVAL;
		goto exit1;
	}

	s_aw2013->leds_priv = priv;

	pr_info("Probed successfully\n");
	return 0;

exit1:
	kfree(s_aw2013);
exit0:
	return err;
}

static int aw2013_led_remove(struct i2c_client *client)
{
	struct aw2013_dev_data *aw2013 = i2c_get_clientdata(client);
	int count = 0;

	for (count = aw2013->leds_priv->num_leds - 2; count >= 0; count--)
		delete_aw2013_led(&(aw2013->leds_priv->leds[count]));

	devm_kfree(&client->dev, aw2013->leds_priv);
	kfree(aw2013);

	dev_info(&client->dev, "Removed successfully\n");
	return 0;
}

static const struct i2c_device_id aw2013_led_id[] = {
	{ AW2013_I2C_NAME, 0 },
	{ }
};

static struct of_device_id aw2013_led_match_table[] = {
	{ .compatible = "awinic,aw2013", },
	{ },
};

static struct i2c_driver aw2013_led_driver = {
	.probe		= aw2013_led_probe,
	.remove		= aw2013_led_remove,
	.id_table	= aw2013_led_id,
	.driver = {
		.name	= AW2013_I2C_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = aw2013_led_match_table,
	},
};

static int __init aw2013_led_init(void)
{
	return i2c_add_driver(&aw2013_led_driver);
}

static void __exit aw2013_led_exit(void)
{
	i2c_del_driver(&aw2013_led_driver);
}

module_init(aw2013_led_init);
module_exit(aw2013_led_exit);

MODULE_AUTHOR("ming he <heming@wingtech.com>");
MODULE_DESCRIPTION("aw2013 driver");
MODULE_LICENSE("GPL");
