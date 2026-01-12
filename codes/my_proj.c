#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define DEVICE_NAME "my_proj"
#define CLASS_NAME  "my_proj_class"

#define RTC_RST 22
#define RTC_CLK 17
#define RTC_DAT 27
#define GPIO_DHT 4
#define ROTARY_A 23
#define ROTARY_B 24
#define ROTARY_SW 16

MODULE_LICENSE("GPL");

static struct class *my_class;
static dev_t dev_num;
static struct cdev my_cdev;

static int encoder_count = 0;
static int button_mode = 0;
static int last_temp = 25;
static int last_humi = 40;
static int is_adjusting = 0; 

static int irq_num_a = -1;
static int irq_num_sw = -1;

/* DS1302 RTC 제어 함수 */
void ds1302_write_byte(unsigned char dat) {
    int i;
    gpio_direction_output(RTC_DAT, 0);
    for (i = 0; i < 8; i++) {
        gpio_set_value(RTC_DAT, dat & 1);
        udelay(2);
        gpio_set_value(RTC_CLK, 1); udelay(2);
        gpio_set_value(RTC_CLK, 0); udelay(2);
        dat >>= 1;
    }
}

unsigned char ds1302_read_byte(void) {
    int i;
    unsigned char dat = 0;
    gpio_direction_input(RTC_DAT);
    for (i = 0; i < 8; i++) {
        dat >>= 1;
        if (gpio_get_value(RTC_DAT)) dat |= 0x80;
        gpio_set_value(RTC_CLK, 1); udelay(2);
        gpio_set_value(RTC_CLK, 0); udelay(2);
    }
    return dat;
}

unsigned char read_rtc_reg(unsigned char cmd) {
    unsigned char dat;
    gpio_set_value(RTC_RST, 1);
    ds1302_write_byte(cmd);
    dat = ds1302_read_byte();
    gpio_set_value(RTC_RST, 0);
    return dat;
}

/* DHT11 센서 제어 함수 */
static int read_dht11(int *temp, int *humi) {
    unsigned char data[5] = {0};
    int i, counter;
    unsigned long flags;

    gpio_direction_output(GPIO_DHT, 0);
    msleep(20);
    gpio_set_value(GPIO_DHT, 1);
    udelay(30);
    gpio_direction_input(GPIO_DHT);

    local_irq_save(flags);
    counter = 0; while (gpio_get_value(GPIO_DHT)) { if (++counter > 100) goto fail; udelay(1); }
    counter = 0; while (!gpio_get_value(GPIO_DHT)) { if (++counter > 100) goto fail; udelay(1); }
    counter = 0; while (gpio_get_value(GPIO_DHT)) { if (++counter > 100) goto fail; udelay(1); }

    for (i = 0; i < 40; i++) {
        counter = 0; while (!gpio_get_value(GPIO_DHT)) { if (++counter > 100) goto fail; udelay(1); }
        udelay(35);
        if (gpio_get_value(GPIO_DHT)) data[i / 8] |= (1 << (7 - (i % 8)));
        counter = 0; while (gpio_get_value(GPIO_DHT)) { if (++counter > 100) goto fail; udelay(1); }
    }
    local_irq_restore(flags);

    if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        *humi = data[0]; *temp = data[2];
        return 0;
    }
fail:
    local_irq_restore(flags);
    return -1;
}

static irqreturn_t rot_a_handler(int irq, void *dev_id) {
    if (gpio_get_value(ROTARY_A) == gpio_get_value(ROTARY_B)) encoder_count++;
    else encoder_count--;
    return IRQ_HANDLED;
}

static irqreturn_t sw_handler(int irq, void *dev_id) {
    static unsigned long last_j = 0;
    if (time_after(jiffies, last_j + msecs_to_jiffies(250))) {
        button_mode = (button_mode + 1) % 4;
    }
    last_j = jiffies;
    return IRQ_HANDLED;
}

static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *off) {
    char kbuf[128];
    int t, h;
    unsigned char rs=0, rm=0, rh=0, rd=0, rmo=0, ry=0;

    if (*off > 0) return 0;

    if (!is_adjusting) {
        rs = read_rtc_reg(0x81); rm = read_rtc_reg(0x83); rh = read_rtc_reg(0x85);
        rd = read_rtc_reg(0x87); rmo = read_rtc_reg(0x89); ry = read_rtc_reg(0x8D);
    }

    if (read_dht11(&t, &h) == 0) { last_temp = t; last_humi = h; }

    sprintf(kbuf, "%02X,%02X,%02X,%02X,%02X,%02X,%d,%d,%d,%d\n",
            ry, rmo, rd, rh, rm, rs, encoder_count, button_mode, last_temp, last_humi);

    if (copy_to_user(buf, kbuf, strlen(kbuf))) return -EFAULT;
    return strlen(kbuf);
}

static ssize_t my_write(struct file *file, const char __user *buf, size_t len, loff_t *off) {
    char kbuf[64];
    unsigned int y, mo, d, h, mi, s;
    if (copy_from_user(kbuf, buf, len)) return -EFAULT;
    kbuf[len] = 0;

    if (sscanf(kbuf, "%x,%x,%x,%x,%x,%x", &y, &mo, &d, &h, &mi, &s) == 6) {
        is_adjusting = 1;
        gpio_set_value(RTC_RST, 1);
        ds1302_write_byte(0x8E); ds1302_write_byte(0x00);
        gpio_set_value(RTC_RST, 0);

        gpio_set_value(RTC_RST, 1); ds1302_write_byte(0x8C); ds1302_write_byte(y); gpio_set_value(RTC_RST, 0);
        gpio_set_value(RTC_RST, 1); ds1302_write_byte(0x88); ds1302_write_byte(mo); gpio_set_value(RTC_RST, 0);
        gpio_set_value(RTC_RST, 1); ds1302_write_byte(0x86); ds1302_write_byte(d); gpio_set_value(RTC_RST, 0);
        gpio_set_value(RTC_RST, 1); ds1302_write_byte(0x84); ds1302_write_byte(h); gpio_set_value(RTC_RST, 0);
        gpio_set_value(RTC_RST, 1); ds1302_write_byte(0x82); ds1302_write_byte(mi); gpio_set_value(RTC_RST, 0);
        gpio_set_value(RTC_RST, 1); ds1302_write_byte(0x80); ds1302_write_byte(s & 0x7F); gpio_set_value(RTC_RST, 0);
        is_adjusting = 0;
    }
    return len;
}

static struct file_operations fops = { .owner = THIS_MODULE, .read = my_read, .write = my_write };

static int __init my_init(void) {
    int ret;
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret) return ret;

    cdev_init(&my_cdev, &fops);
    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret) goto err_chrdev;

    my_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(my_class)) { ret = PTR_ERR(my_class); goto err_cdev; }

    device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);

    gpio_request(RTC_RST, "RST"); gpio_request(RTC_CLK, "CLK"); gpio_request(RTC_DAT, "DAT");
    gpio_request(GPIO_DHT, "DHT"); gpio_request(ROTARY_A, "ROT_A");
    gpio_request(ROTARY_B, "ROT_B"); gpio_request(ROTARY_SW, "ROT_SW");

    gpio_direction_output(RTC_RST, 0); gpio_direction_output(RTC_CLK, 0);
    gpio_direction_input(ROTARY_A); gpio_direction_input(ROTARY_B); gpio_direction_input(ROTARY_SW);

    irq_num_a = gpio_to_irq(ROTARY_A);
    /* 경고 해결: request_irq 반환값 확인 */
    ret = request_irq(irq_num_a, rot_a_handler, IRQF_TRIGGER_RISING, "rot_a", NULL);
    if (ret) {
        printk(KERN_ERR "my_proj: Failed to request IRQ for rot_a\n");
        goto err_gpio;
    }

    irq_num_sw = gpio_to_irq(ROTARY_SW);
    ret = request_irq(irq_num_sw, sw_handler, IRQF_TRIGGER_FALLING, "rot_sw", NULL);
    if (ret) {
        printk(KERN_ERR "my_proj: Failed to request IRQ for rot_sw\n");
        free_irq(irq_num_a, NULL);
        goto err_gpio;
    }

    return 0;

err_gpio:
    gpio_free(RTC_RST); gpio_free(RTC_CLK); gpio_free(RTC_DAT);
    gpio_free(GPIO_DHT); gpio_free(ROTARY_A); gpio_free(ROTARY_B); gpio_free(ROTARY_SW);
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
err_cdev:
    cdev_del(&my_cdev);
err_chrdev:
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

static void __exit my_exit(void) {
    if (irq_num_a >= 0) free_irq(irq_num_a, NULL);
    if (irq_num_sw >= 0) free_irq(irq_num_sw, NULL);
    gpio_free(RTC_RST); gpio_free(RTC_CLK); gpio_free(RTC_DAT);
    gpio_free(GPIO_DHT); gpio_free(ROTARY_A); gpio_free(ROTARY_B); gpio_free(ROTARY_SW);
    device_destroy(my_class, dev_num); class_destroy(my_class);
    cdev_del(&my_cdev); unregister_chrdev_region(dev_num, 1);
}

module_init(my_init);
module_exit(my_exit);