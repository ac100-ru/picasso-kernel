/*
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include "nvec.h"


struct nvec_event_device {
	struct input_dev *sleep;
	struct input_dev *power;
	struct input_dev *lid;
	struct notifier_block notifier;
	struct nvec_chip *nvec;
} event_handler;


static int nvec_event_notifier(struct notifier_block *nb,
			      unsigned long event_type, void *data)
{
	unsigned char *msg = (unsigned char *)data;
	int transfer_type;
	int data_size;

	if (event_type != NVEC_SYS_EVT)
	{
		print_hex_dump(KERN_WARNING, "evt - non sys evt: ",
			DUMP_PREFIX_NONE, 16, 1, msg, 2, true);
		return NOTIFY_DONE;
	}
	transfer_type = (msg[0] & (3 << 5)) >> 5;

	if (transfer_type != NVEC_VAR_SIZE)
	{
		print_hex_dump(KERN_WARNING, "evt - non var size evt: ",
			DUMP_PREFIX_NONE, 16, 1, msg, 2, true);
		return NOTIFY_DONE;
	}

	data_size = msg[1];
	print_hex_dump(KERN_WARNING, "evt - sys varsize msg: ",
			DUMP_PREFIX_NONE, 16, 1, msg, data_size + 2, true);

	if (data_size != 4)
		return NOTIFY_DONE;

	/* Check OEM byte 0 */
	switch (msg[4])
	{
	case 0x80:		/* short power button press */
		input_report_key(event_handler.power, KEY_POWER, 1);
		input_sync(event_handler.power);
		input_report_key(event_handler.power, KEY_POWER, 0);
		input_sync(event_handler.power);
		break;
	case 0x02:		/* lid close */
		input_report_switch(event_handler.lid, SW_LID, 1);
		input_sync(event_handler.lid);
		break;
	case 0x00:		/* lid open */
		input_report_switch(event_handler.lid, SW_LID, 0);
		input_sync(event_handler.lid);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_STOP;
}

#ifdef CONFIG_PM

static int nvec_event_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nvec_chip *nvec = dev_get_drvdata(pdev->dev.parent);
	(void)nvec;
	return 0;
}

static int nvec_event_resume(struct platform_device *pdev)
{
	struct nvec_chip *nvec = dev_get_drvdata(pdev->dev.parent);
	(void)nvec;
	return 0;
}

#else
#define nvec_event_suspend NULL
#define nvec_event_resume NULL
#endif


static int nvec_event_probe(struct platform_device *pdev)
{
	struct nvec_chip *nvec = dev_get_drvdata(pdev->dev.parent);
	int err;
	/*
	char	lid_evt_en[] = { NVEC_SYS, SET_LEDS, 0 },
		pwr_btn_evt_en[] = { NVEC_SYS, ENABLE_KBD },
		cnfg_wake[] = { NVEC_SYS, CNFG_WAKE, true, true },
						true };
	*/

	event_handler.nvec = nvec;
	event_handler.sleep = devm_input_allocate_device(&pdev->dev);
	event_handler.sleep->name = "nvec sleep button";
	event_handler.sleep->phys = "nvec";
	event_handler.sleep->evbit[0] = BIT_MASK(EV_KEY);
	set_bit(KEY_SLEEP, event_handler.sleep->keybit);

	event_handler.power = devm_input_allocate_device(&pdev->dev);
	event_handler.power->name = "nvec power button";
	event_handler.power->phys = "nvec";
	event_handler.power->evbit[0] = BIT_MASK(EV_KEY);
	set_bit(KEY_POWER, event_handler.power->keybit);

	event_handler.lid = devm_input_allocate_device(&pdev->dev);
	event_handler.lid->name = "nvec lid switch button";
	event_handler.lid->phys = "nvec";
	event_handler.lid->evbit[0] = BIT_MASK(EV_SW);
	set_bit(SW_LID, event_handler.lid->swbit);

	err = input_register_device(event_handler.sleep);
	if (err)
		goto fail;

	err = input_register_device(event_handler.power);
	if (err)
		goto fail;

	err = input_register_device(event_handler.lid);
	if (err)
		goto fail;

	event_handler.notifier.notifier_call = nvec_event_notifier;
	nvec_register_notifier(nvec, &event_handler.notifier, 0);

	/* enable lid switch event */
	nvec_write_async(nvec, "\x01\x01\x01\x00\x00\x02\x00", 7);

	/* enable power button event */
	nvec_write_async(nvec, "\x01\x01\x01\x00\x00\x80\x00", 7);

	return 0;

fail:
	input_free_device(event_handler.sleep);
	input_free_device(event_handler.power);
	input_free_device(event_handler.lid);
	return err;
}

static int nvec_event_remove(struct platform_device *pdev)
{
	struct nvec_chip *nvec = dev_get_drvdata(pdev->dev.parent);

	/*
	char disable_kbd[] = { NVEC_KBD, DISABLE_KBD },
	     uncnfg_wake_key_reporting[] = { NVEC_KBD, CNFG_WAKE_KEY_REPORTING,
						false };
	nvec_write_async(nvec, uncnfg_wake_key_reporting, 3);
	nvec_write_async(nvec, disable_kbd, 2);
	*/

	nvec_unregister_notifier(nvec, &event_handler.notifier);

	return 0;
}

static struct platform_driver nvec_event_driver = {
	.probe  = nvec_event_probe,
	.remove = nvec_event_remove,
	.suspend = nvec_event_suspend,
	.resume = nvec_event_resume,
	.driver = {
		.name = "nvec-event",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(nvec_event_driver);

MODULE_AUTHOR("Julian Andres Klode <jak@jak-linux.org>");
MODULE_DESCRIPTION("NVEC power/sleep/lid switch driver");
MODULE_ALIAS("platform:nvec-event");
MODULE_LICENSE("GPL");
