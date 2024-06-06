#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

static DEFINE_SPINLOCK(g_lock);
static int gpio_irq;
static struct timer_list debounce_timer;
static int debounce_active = 0;
#define DEBOUNCE_TIME 50
#define MEM_BASE_ADDRESS 0x1f00000000
#define MEM_LENGTH (64 * 1024 * 1024)

static uint32_t *mem;
static uint32_t *PERIBase;
static uint32_t *GPIOBase;
static uint32_t *RIOBase;
static uint32_t *PADBase;

typedef struct {
    uint32_t status;
    uint32_t ctrl;
} GPIOregs;
#define GPIO ((GPIOregs*)GPIOBase)

typedef struct {
    uint32_t Out;
    uint32_t OE;
    uint32_t In;
    uint32_t InSync;
} rioregs;
#define rio ((rioregs *)RIOBase)
#define rioXOR ((rioregs *)(RIOBase + 0x1000 / 4))
#define rioSET ((rioregs *)(RIOBase + 0x2000 / 4))
#define rioCLR ((rioregs *)(RIOBase + 0x3000 / 4))

typedef union {
    uint32_t all;
    struct {
        uint32_t SLEWFAST : 1;
        uint32_t SCHMITT : 1;
        uint32_t PDE : 1;
        uint32_t PUE : 1;
        uint32_t DRIVE : 2;
        uint32_t IE : 1;
        uint32_t OD : 1;
        uint32_t : 24;
    } bit;
} pad;
#define PAD ((pad*)(PADBase + 1))

typedef struct {
    uint32_t VOLTAGE : 1;
    uint32_t : 31;
} padv;
#define VOL ((padv*)(PADBase))

static void debounce_timer_callback(struct timer_list *t) {
    debounce_active = 0; // デバウンス解除
}

static void debounce_timer_callback(struct timer_list *t) {
    debounce_active = 0; // デバウンス解除
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id) {
    static unsigned long last_irq_time = 0;
    unsigned long irq_time = jiffies;
    unsigned long flags;

    //if (irq_time - last_irq_time < msecs_to_jiffies(150)) {
    //    return IRQ_HANDLED;
    //}

    last_irq_time = irq_time;

    if (!debounce_active) {
        debounce_active = 1;
        mod_timer(&debounce_timer, jiffies + msecs_to_jiffies(DEBOUNCE_TIME));

        spin_lock_irqsave(&g_lock, flags);
        pr_info("GPIO %d Interrupt triggered!\n", irq);
        spin_unlock_irqrestore(&g_lock, flags);
    }

    return IRQ_HANDLED;
}

static int __init gpio_irq_init(void) {
    struct device_node *node;
    int result;
    struct gpio_desc *desc;
    int gpio;
    printk(KERN_INFO "Loading GPIO IRQ DRV module...\n");

    spin_lock_init(&g_lock);

    mem = ioremap(MEM_BASE_ADDRESS, MEM_LENGTH);
    if (!mem) {
        printk(KERN_ERR "Failed to map memory\n");
        return -ENOMEM;
    } else {
        printk(KERN_INFO "mem=%p\n", (void *)mem);
        PERIBase = mem;
        GPIOBase = PERIBase + 0xd0000 / 4;
        RIOBase = PERIBase + 0xe0000 / 4;
        PADBase = PERIBase + 0xf0000 / 4;
    }

    VOL[0].VOLTAGE = 0; // 3.3V
    //GPIO[19].ctrl = 0b00000000000000000000000000000 | 5; // level low
    GPIO[19].ctrl = 1<<28|1<<24|0<<16|2<<14|0<<12|50<<5|5<<0;
    PAD[19].all = 0b11001010; // (input schmitt triggered)
    rioSET->In = 0x01 << 19;

    node = of_find_node_by_path("/axi/pcie@120000/rp1/gpio@d0000/pin19");
    if (!node) {
        pr_err("Failed to find node\n");
        return -ENODEV;
    }

    gpio_irq = irq_of_parse_and_map(node, 0);
    if (!gpio_irq) {
        pr_err("Failed to get IRQ for GPIO from DT\n");
        return -EINVAL;
    }

    result = request_irq(gpio_irq, gpio_irq_handler, IRQF_TRIGGER_FALLING, "gpio_irq_handler", N>
    if (result) {
        pr_err("Failed to request IRQ %d\n", gpio_irq);
        return result;
    }

    timer_setup(&debounce_timer, debounce_timer_callback, 0);

    pr_info("GPIO IRQ DRV loaded, IRQ %d\n", gpio_irq);
    return 0;
}

static void __exit gpio_irq_exit(void) {
    printk(KERN_INFO "Unloading GPIO IRQ DRV module...\n");

    if (mem) {
        iounmap(mem);
    }
    free_irq(gpio_irq, NULL);
    del_timer(&debounce_timer);
    pr_info("GPIO IRQ DRV module unloaded\n");
}

module_init(gpio_irq_init);
module_exit(gpio_irq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kazuteru Yamada");
MODULE_DESCRIPTION("Tester for GPIO kernel module (address loading)");
