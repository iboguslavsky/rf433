#include <linux/init.h>  
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/kdev_t.h>		// MKDEV definition
#include <asm/io.h>
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Igor Boguslavsky");
MODULE_DESCRIPTION("RF Remote Control driver for Orange Pi Zero"); 
MODULE_VERSION("0.1");           
 
#define PORTBASE 0x01c20800
#define PG_CFG0_REG	PORTBASE+0xD8 // Use PG06 / GPIO198 for FSK output
#define PG_PULL0_REG	PORTBASE+0xF4 // Configure pull up on the pin
#define PORTG_DATA_REG  PORTBASE+0xE8

static struct class rf433_class;
struct kobject *rf433_kobj;

static struct class_attribute rf433_class_attrs[] = {
  __ATTR_NULL
};

static struct class rf433_class = {
  .name =         "rf433",
  .owner =        THIS_MODULE,
  .class_attrs =  rf433_class_attrs,
};

struct packet_parts {
  char address[8];
  char command[4];
};

union packet {
  struct packet_parts ac;
  char packet[12];
};

struct rf433_channel {

  union packet pkt;
  unsigned int use_count;

  __u32 ctrl_reg, pullup_reg;

  void __iomem *datareg;
};

// Define baud (1.25ms)
#define TICK 1.25
#define MS_TO_NS(x) (x * 1E6L)

static ssize_t rf433_address_show (struct device *dev,struct device_attribute *attr, char *buf);
static ssize_t rf433_command_show (struct device *dev,struct device_attribute *attr, char *buf);
static ssize_t rf433_packet_show (struct device *dev,struct device_attribute *attr, char *buf);

static ssize_t rf433_send_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);
static ssize_t rf433_address_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);
static ssize_t rf433_command_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);
static ssize_t rf433_packet_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);

// no pulse mode for now; just cycle mode
static DEVICE_ATTR(send, 0644, NULL, rf433_send_store);
static DEVICE_ATTR(address, 0644, rf433_address_show, rf433_address_store);
static DEVICE_ATTR(command, 0644, rf433_command_show, rf433_command_store);
static DEVICE_ATTR(packet, 0644, rf433_packet_show, rf433_packet_store);

static const struct attribute *rf433_attrs[] = {
  &dev_attr_send.attr,
  &dev_attr_address.attr,
  &dev_attr_command.attr,
  &dev_attr_packet.attr,
  NULL,
};

static const struct attribute_group rf433_attr_group = {
  .attrs = (struct attribute **) rf433_attrs
};

struct device *rf433;
static struct rf433_channel channel;

static int __init rf433_init (void) {
int ret;
__u32 data;
void __iomem *addr;

  ret = class_register (&rf433_class);

  if (ret) {
    class_unregister (&rf433_class);
  }

  rf433 = device_create (&rf433_class, NULL, MKDEV(0,0), &channel, "rf0");
  rf433_kobj = &rf433 -> kobj;

  ret = sysfs_create_group (rf433_kobj, &rf433_attr_group);

  // Set up PG06 as output
  addr = ioremap (PG_CFG0_REG, 4);
  data = ioread32 (addr);
  
  // Store PINs configuration for the exit
  channel.ctrl_reg = data;
  data &= ~(1 << 26);     
  data &= ~(1 << 25);
  data |=  (1 << 24);
  iowrite32 (data, addr);
  iounmap (addr);

  // Enable pull-up
  addr = ioremap (PG_PULL0_REG, 4);
  data = ioread32 (addr);
  channel.pullup_reg = data;
  data &= ~(1 << 13);     
  data |=  (1 << 12);
  iowrite32 (data, addr);
  iounmap (addr);

  // Map data register to kernel address space and store it
  channel.datareg = ioremap (PORTG_DATA_REG, 4);

  printk(KERN_INFO "[%s] initialized ok\n", rf433_class.name);
  return ret;
}
 
static void __exit rf433_exit(void) {
void __iomem *addr;

  // Restore pin state and pullup
  addr = ioremap (PG_CFG0_REG, 4);
  iowrite32 (channel.ctrl_reg, addr);
  iounmap (addr);

  addr = ioremap (PG_PULL0_REG, 4);
  iowrite32 (channel.pullup_reg, addr);
  iounmap (addr);

  iounmap (channel.datareg);

  device_destroy (&rf433_class, rf433 -> devt);
  class_unregister (&rf433_class);

  printk (KERN_INFO "[%s] exiting\n", rf433_class.name);
}
 

// Accessors
//
static ssize_t rf433_address_show (struct device *dev, struct device_attribute *attr, char *buf) {
char buffer[9];

  const struct rf433_channel *channel = dev_get_drvdata (dev);

  strncpy (buffer, channel -> pkt.ac.address, 8);
  buffer[sizeof(buffer) - 1] = '\0';

  return sprintf (buf, "%s\n", buffer);
}

static ssize_t rf433_command_show (struct device *dev, struct device_attribute *attr, char *buf) {
char buffer[5];

  const struct rf433_channel *channel = dev_get_drvdata (dev);

  strncpy (buffer, channel -> pkt.ac.command, 4);
  buffer[sizeof(buffer) - 1] = '\0';

  return sprintf (buf, "%s\n", buffer);
}

static ssize_t rf433_packet_show (struct device *dev, struct device_attribute *attr, char *buf) {
char buffer[13];

  const struct rf433_channel *channel = dev_get_drvdata (dev);

  strncpy (buffer, channel -> pkt.packet, 12);
  buffer[sizeof(buffer) - 1] = '\0';

  return sprintf (buf, "%s\n", buffer);
}

// Modifiers
//
static ssize_t rf433_address_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
char buffer[9];

  // Successful sscanf?
  if (sscanf (buf, "%8s", buffer)) {

    strncpy (channel -> pkt.ac.address, buffer, 8);
    return size;
  }

  return -EINVAL;
}

static ssize_t rf433_command_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
char buffer[5];

  // Successful sscanf?
  if (sscanf (buf, "%4s", buffer)) {

    strncpy (channel -> pkt.ac.command, buffer, 4);
    return size;
  }

  return -EINVAL;
}

static ssize_t rf433_packet_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
char buffer[13];

  // Successful sscanf?
  if (sscanf (buf, "%12s", buffer)) {

    strncpy (channel -> pkt.packet, buffer, 12);
    return size;
  }

  return -EINVAL;
}

static ssize_t rf433_send_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
char buffer[13];
__u16 action;

  // Successful sscanf?
  if (sscanf (buf, "%hu", &action)) {

    // process the packet
    if (action > 1) {

      strncpy (buffer, channel -> pkt.packet, 12);
      buffer[sizeof(buffer) - 1] = '\0';

      printk(KERN_INFO "[%s] Sending [%s]\n", rf433_class.name, buffer);
      return size;
    }
  }

  return -EINVAL;
}

/*
enum hrtimer_restart effects_hrtimer_callback (struct hrtimer *timer) {
__u16 restart;


  if (channel.effect == EFFECT_ON) {

    channel.active_cycles = 0;
    restart = HRTIMER_NORESTART;
  }
  else (channel.effect == EFFECT_ATTACK) {

    channel.cycles.s.active_cycles += direction;
    hrtimer_forward (timer, ktime_get (), ktime_set (0, MS_TO_NS(TICK));

    restart = (channel.cycles.s.active_cycles == 256) ? HRTIMER_NORESTART : HRTIMER_RESTART;
  }
  else (channel.effect == EFFECT_FADE) {

    channel.cycles.s.active_cycles += direction;
    hrtimer_forward (timer, ktime_get (), ktime_set (0, MS_TO_NS(TICK));

    restart = (channel.cycles.s.active_cycles == 0) ? HRTIMER_NORESTART : HRTIMER_RESTART;
  }
  else (channel.effect == EFFECT_FULLCYCLE) {

    hrtimer_forward (timer, ktime_get (), ktime_set (0, MS_TO_NS(TICK));
    restart = HRTIMER_RESTART;
  }

  iowrite32 (channel.cycles.initializer, channel.period_reg_addr);
  return restart;
}
*/

module_init(rf433_init);
module_exit(rf433_exit);
