#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Johannes 4 GNU/Linux");
MODULE_DESCRIPTION("A simple LKM for a gpio interrupt");

/*
mount /dev/mmcblk0p3 /mnt
ln -s /mnt/lib/modules /lib/
modprobe gpio-irq
mount -t debugfs none /sys/kernel/debug/
cat /sys/kernel/debug/gpio
*/

/*
gpio3 RK_PC2 = 96+18=114

# mount -t debugfs none /sys/kernel/debug/
# cat /sys/kernel/debug/gpio
gpiochip0: GPIOs 0-31, parent: platform/fdd60000.gpio, gpio0:
gpiochip1: GPIOs 32-63, parent: platform/fe740000.gpio, gpio1:
gpiochip2: GPIOs 64-95, parent: platform/fe750000.gpio, gpio2:
gpiochip3: GPIOs 96-127, parent: platform/fe760000.gpio, gpio3:
 gpio-114 (                    |Headphone detection ) in  lo IRQ 
gpiochip4: GPIOs 128-159, parent: platform/fe770000.gpio, gpio4:

*/
unsigned int gpio_number=114;
/** variable contains pin number o interrupt controller to which GPIO 17 is mapped to */
unsigned int irq_number;

/**
 * @brief Interrupt service routine is called, when interrupt is triggered
 */
static irqreturn_t gpio_irq_handler(int irq, void *dev_id) {
	printk("gpio_irq: Interrupt was triggered!\n");
/*

	ret = iio_read_channel_raw(headset->chan, &val);
	if (ret < 0) {
		pr_err("read hook adc channel() error: %d\n", ret);
		goto out;
	} else {
		DBG("hook_work_callback read adc value=%d\n", val);
	}
	old_status = headset->hook_status;
	if (val < HOOK_LEVEL_LOW && val >= 0)
		headset->hook_status = HOOK_DOWN;
	else if (val > HOOK_LEVEL_HIGH && val < HOOK_DEFAULT_VAL)
		headset->hook_status = HOOK_UP;
	if (old_status != headset->hook_status) {
		input_report_key(headset->input_dev,
				 HOOK_KEY_CODE, headset->hook_status);
		input_sync(headset->input_dev);
	}
*/
	return  IRQ_HANDLED;
}

/**
 * @brief This function is called, when the module is loaded into the kernel
 */
static int __init ModuleInit(void) {
	printk("qpio_irq: Loading module... ");

	/* Setup the gpio */
	if(gpio_request(gpio_number, "my-gpio")) {
		printk("Error!\nCan not allocate GPIO %d\n",gpio_number);
		return -1;
	}

	/* Set GPIO 17 direction */
	if(gpio_direction_input(gpio_number)) {
		printk("Error!\nCan not set GPIO %d to input!\n",gpio_number);
		gpio_free(gpio_number);
		return -1;
	}

/*
	pdata->chan = iio_channel_get(&pdev->dev, NULL);
	if (IS_ERR(pdata->chan)) {
		pdata->chan = NULL;
		dev_warn(&pdev->dev, "have not set adc chan\n");
	}
	headset->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!headset->input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		ret = -ENOMEM;
		goto failed;
	}
*/

	/* Setup the interrupt */
	irq_number = gpio_to_irq(gpio_number);

	if(request_irq(irq_number, (irq_handler_t) gpio_irq_handler, IRQF_TRIGGER_RISING, "my_gpio_irq", NULL) != 0){
		printk("Error!\nCan not request interrupt nr.: %d\n", irq_number);
		gpio_free(gpio_number);
		return -1;
	}

	printk("Done!\n");
	printk("GPIO %d is mapped to IRQ Nr.: %d\n",gpio_number, irq_number);
	return 0;
}

/**
 * @brief This function is called, when the module is removed from the kernel
 */
static void __exit ModuleExit(void) {
	printk("gpio_irq: Unloading module... ");
	free_irq(irq_number, NULL);
	gpio_free(gpio_number);

}

module_init(ModuleInit);
module_exit(ModuleExit);
