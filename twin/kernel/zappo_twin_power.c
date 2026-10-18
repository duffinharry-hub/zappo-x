// SPDX-License-Identifier: GPL-2.0
/*
 * Zappo X device twin: stands in for the RG40XX H's AXP2202 power chip.
 *
 * Registers "axp2202-battery" and "axp2202-usb" with the same names and
 * extra files (mcu_pwr, moto, work_led ...) the handheld's firmware uses,
 * so Knulli's scripts and Zappo X read and write them unchanged.
 * Everything is writable from inside the twin, e.g.
 *   echo 15 > /sys/class/power_supply/axp2202-battery/capacity
 *   echo 1  > /sys/class/power_supply/axp2202-usb/online      (plug in)
 * Plugging in sets the battery to Charging, unplugging to Discharging.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

static struct power_supply *bat, *usb;
static int capacity = 76, online, voltage_uv = 3900000, current_ua = -350000, temp = 310;
static int status = POWER_SUPPLY_STATUS_DISCHARGING;

static int level(void)
{
	if (capacity >= 100)
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	if (capacity > 80)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	if (capacity > 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	if (capacity > 5)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
}

static enum power_supply_property bat_props[] = {
	POWER_SUPPLY_PROP_STATUS, POWER_SUPPLY_PROP_PRESENT, POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY, POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL, POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW, POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, POWER_SUPPLY_PROP_MODEL_NAME,
};

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE, POWER_SUPPLY_PROP_PRESENT,
};

static int bat_get(struct power_supply *psy, enum power_supply_property p,
		   union power_supply_propval *v)
{
	switch (p) {
	case POWER_SUPPLY_PROP_STATUS: v->intval = status; break;
	case POWER_SUPPLY_PROP_PRESENT: v->intval = 1; break;
	case POWER_SUPPLY_PROP_HEALTH: v->intval = POWER_SUPPLY_HEALTH_GOOD; break;
	case POWER_SUPPLY_PROP_TECHNOLOGY: v->intval = POWER_SUPPLY_TECHNOLOGY_LIPO; break;
	case POWER_SUPPLY_PROP_CAPACITY: v->intval = capacity; break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL: v->intval = level(); break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW: v->intval = voltage_uv; break;
	case POWER_SUPPLY_PROP_CURRENT_NOW: v->intval = current_ua; break;
	case POWER_SUPPLY_PROP_TEMP: v->intval = temp; break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN: v->intval = 3200000; break;
	case POWER_SUPPLY_PROP_MODEL_NAME: v->strval = "RG40XX H twin"; break;
	default: return -EINVAL;
	}
	return 0;
}

static void follow_charger(void)
{
	if (online)
		status = capacity >= 100 ? POWER_SUPPLY_STATUS_FULL : POWER_SUPPLY_STATUS_CHARGING;
	else
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	current_ua = online ? 900000 : -350000;
}

static int bat_set(struct power_supply *psy, enum power_supply_property p,
		   const union power_supply_propval *v)
{
	switch (p) {
	case POWER_SUPPLY_PROP_STATUS: status = v->intval; break;
	case POWER_SUPPLY_PROP_CAPACITY:
		capacity = clamp(v->intval, 0, 100);
		voltage_uv = 3300000 + capacity * 9000;
		if (online)
			follow_charger();
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW: voltage_uv = v->intval; break;
	case POWER_SUPPLY_PROP_CURRENT_NOW: current_ua = v->intval; break;
	case POWER_SUPPLY_PROP_TEMP: temp = v->intval; break;
	default: return -EINVAL;
	}
	power_supply_changed(bat);
	return 0;
}

static int bat_writeable(struct power_supply *psy, enum power_supply_property p)
{
	return p == POWER_SUPPLY_PROP_STATUS || p == POWER_SUPPLY_PROP_CAPACITY ||
	       p == POWER_SUPPLY_PROP_VOLTAGE_NOW || p == POWER_SUPPLY_PROP_CURRENT_NOW ||
	       p == POWER_SUPPLY_PROP_TEMP;
}

static int usb_get(struct power_supply *psy, enum power_supply_property p,
		   union power_supply_propval *v)
{
	switch (p) {
	case POWER_SUPPLY_PROP_ONLINE: v->intval = online; break;
	case POWER_SUPPLY_PROP_PRESENT: v->intval = online; break;
	default: return -EINVAL;
	}
	return 0;
}

static int usb_set(struct power_supply *psy, enum power_supply_property p,
		   const union power_supply_propval *v)
{
	if (p != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;
	online = !!v->intval;
	follow_charger();
	power_supply_changed(usb);
	power_supply_changed(bat);
	return 0;
}

static int usb_writeable(struct power_supply *psy, enum power_supply_property p)
{
	return p == POWER_SUPPLY_PROP_ONLINE;
}

/* the AXP2202 driver's extra switches on the handheld: light controller power,
 * rumble motor, power LED and so on.  Stored, so the twin can show them. */
enum { MCU_PWR, MCU_ESCKEY, WORK_LED, MOTO, NDS_PWRKEY, HALLKEY, NEXTRA };
static int extra[NEXTRA] = { 0, 0, 1, 0, 0, 0 };

#define EXTRA_ATTR(_name, _idx)							\
static ssize_t _name##_show(struct device *d, struct device_attribute *a, char *buf) \
{ return sysfs_emit(buf, "%d\n", extra[_idx]); }				\
static ssize_t _name##_store(struct device *d, struct device_attribute *a,	\
			     const char *buf, size_t n)				\
{										\
	int v, r = kstrtoint(buf, 0, &v);					\
	if (r)									\
		return r;							\
	extra[_idx] = v;							\
	sysfs_notify(&d->kobj, NULL, #_name);					\
	return n;								\
}										\
static DEVICE_ATTR_RW(_name)

EXTRA_ATTR(mcu_pwr, MCU_PWR);
EXTRA_ATTR(mcu_esckey, MCU_ESCKEY);
EXTRA_ATTR(work_led, WORK_LED);
EXTRA_ATTR(moto, MOTO);
EXTRA_ATTR(nds_pwrkey, NDS_PWRKEY);
EXTRA_ATTR(hallkey, HALLKEY);

static struct attribute *bat_attrs[] = {
	&dev_attr_mcu_pwr.attr, &dev_attr_mcu_esckey.attr, &dev_attr_work_led.attr,
	&dev_attr_moto.attr, &dev_attr_nds_pwrkey.attr, &dev_attr_hallkey.attr, NULL,
};
ATTRIBUTE_GROUPS(bat);

static const struct power_supply_desc bat_desc = {
	.name = "axp2202-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = bat_props,
	.num_properties = ARRAY_SIZE(bat_props),
	.get_property = bat_get,
	.set_property = bat_set,
	.property_is_writeable = bat_writeable,
};

static const struct power_supply_desc usb_desc = {
	.name = "axp2202-usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = usb_props,
	.num_properties = ARRAY_SIZE(usb_props),
	.get_property = usb_get,
	.set_property = usb_set,
	.property_is_writeable = usb_writeable,
};

static int __init twin_power_init(void)
{
	struct platform_device *pdev;
	struct power_supply_config cfg = { .attr_grp = bat_groups };
	struct power_supply_config ucfg = { };

	pdev = platform_device_register_simple("zappo-twin-power", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);
	bat = power_supply_register(&pdev->dev, &bat_desc, &cfg);
	if (IS_ERR(bat))
		return PTR_ERR(bat);
	usb = power_supply_register(&pdev->dev, &usb_desc, &ucfg);
	if (IS_ERR(usb))
		return PTR_ERR(usb);
	pr_info("zappo-twin: AXP2202 stand-in ready (battery %d%%)\n", capacity);
	return 0;
}
late_initcall(twin_power_init);

MODULE_DESCRIPTION("Zappo X twin: RG40XX H power chip stand-in");
MODULE_LICENSE("GPL");
