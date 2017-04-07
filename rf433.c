#include <linux/init.h>  
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/kdev_t.h>		// MKDEV definition
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <asm/io.h>
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Igor Boguslavsky");
MODULE_DESCRIPTION("RF Remote Control driver for Orange Pi Zero"); 
MODULE_VERSION("1.0");           
 
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

// Number of code words in a code frame in the frame
#define NORMAL_CODEFRAME 10 
#define ENDLESS_CODEFRAME 65535 

// Etekcity icodeword cosists of 10-bit addresses and 2-bit commands
#define ADDRESS_SIZE 10
#define COMMAND_SIZE 2

struct codeword_parts {
  char address[ADDRESS_SIZE];
  char command[COMMAND_SIZE];
};

union codeword {
  struct codeword_parts ac;
  char codeword[ADDRESS_SIZE + COMMAND_SIZE];
};

struct rf433_channel {

  union codeword pkt;
  unsigned int use_count;

  __u8  bitstring[49]; // 12 bits * 4 transitions each + 1 stop bit (narrow) transition; will turn into real bit string at some point
  __u8  codebits;      // current code bit in a code word
  __u16 codewords;     // current codeword in a codeframe

  __u32 ctrl_reg, pullup_reg;

  void __iomem *datareg;
};


// Transition width nanoseconds
#define NARROW 180000
#define WIDE (3 * NARROW)
#define SYNC (30 * NARROW)

#define INTERVAL(x) ((x == 0) ? NARROW : WIDE)

static ssize_t rf433_address_show (struct device *dev,struct device_attribute *attr, char *buf);
static ssize_t rf433_command_show (struct device *dev,struct device_attribute *attr, char *buf);
static ssize_t rf433_codeword_show (struct device *dev,struct device_attribute *attr, char *buf);

static ssize_t rf433_send_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);
static ssize_t rf433_address_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);
static ssize_t rf433_command_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);
static ssize_t rf433_codeword_store (struct device *dev,struct device_attribute *attr, const char *buf, size_t size);

// no pulse mode for now; just cycle mode
static DEVICE_ATTR(send, 0666, NULL, rf433_send_store);
static DEVICE_ATTR(address, 0666, rf433_address_show, rf433_address_store);
static DEVICE_ATTR(command, 0666, rf433_command_show, rf433_command_store);
static DEVICE_ATTR(codeword, 0666, rf433_codeword_show, rf433_codeword_store);

static const struct attribute *rf433_attrs[] = {
  &dev_attr_send.attr,
  &dev_attr_address.attr,
  &dev_attr_command.attr,
  &dev_attr_codeword.attr,
  NULL,
};

static const struct attribute_group rf433_attr_group = {
  .attrs = (struct attribute **) rf433_attrs
};

struct device *rf433;
static struct rf433_channel channel;
static struct hrtimer hr_timer;

enum hrtimer_restart waveform_hrtimer_callback (struct hrtimer *timer);

static DEFINE_MUTEX(rf433_mutex);

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

  mutex_init (&rf433_mutex);

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

  mutex_destroy (&rf433_mutex);

  printk (KERN_INFO "[%s] exiting\n", rf433_class.name);
}
 

// Accessors
//
static ssize_t rf433_address_show (struct device *dev, struct device_attribute *attr, char *buf) {
char buffer[9];

  const struct rf433_channel *channel = dev_get_drvdata (dev);

  strncpy (buffer, channel -> pkt.ac.address, ADDRESS_SIZE);
  buffer[sizeof(buffer) - 1] = '\0';

  return sprintf (buf, "%s\n", buffer);
}

static ssize_t rf433_command_show (struct device *dev, struct device_attribute *attr, char *buf) {
char buffer[5];

  const struct rf433_channel *channel = dev_get_drvdata (dev);

  strncpy (buffer, channel -> pkt.ac.command, COMMAND_SIZE);
  buffer[sizeof(buffer) - 1] = '\0';

  return sprintf (buf, "%s\n", buffer);
}

static ssize_t rf433_codeword_show (struct device *dev, struct device_attribute *attr, char *buf) {
char buffer[13];

  const struct rf433_channel *channel = dev_get_drvdata (dev);

  strncpy (buffer, channel -> pkt.codeword, ADDRESS_SIZE + COMMAND_SIZE);
  buffer[sizeof(buffer) - 1] = '\0';

  return sprintf (buf, "%s\n", buffer);
}

// Modifiers
//
static ssize_t rf433_address_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
char buffer[ADDRESS_SIZE + 1];
__u8 i;

  if (mutex_is_locked (&rf433_mutex)) { // Are we in a middle of sending a frame out?
    printk(KERN_INFO "[%s] rf433 is busy\n", rf433_class.name);
    return -EBUSY;
  }

  // Successful sscanf?
  if (sscanf (buf, "%s", buffer)) {

    for (i = 0; i < ADDRESS_SIZE; i++) 
      if (buffer[i] != '0' && buffer[i] != '1' && buffer[i] != 'f' && buffer[i] != 'F')
        return -EINVAL;

    // Copy chars up to ADDRESS_SIZE chars
    strncpy (channel -> pkt.ac.address, buffer, ADDRESS_SIZE);
    return size;
  }

  return -EINVAL;
}

static ssize_t rf433_command_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
char buffer[COMMAND_SIZE + 1];
__u8 i;

  if (mutex_is_locked (&rf433_mutex)) { // busy sending a frame out?
    printk(KERN_INFO "[%s] rf433 is busy\n", rf433_class.name);
    return -EBUSY;
  }

  // Successful sscanf?
  if (sscanf (buf, "%s", buffer)) {

    // only 0s and 1s are allowed in the command section of the codeword (as per PT2262 part spec)
    // use rf433_codeword_store() for arbitrary codewords
    for (i = 0; i < COMMAND_SIZE; i++) 
      if (buffer[i] != '0' && buffer[i] != '1')
        return -EINVAL;

    // Copy chars up to COMMAND_SIZE chars
    strncpy (channel -> pkt.ac.command, buffer, COMMAND_SIZE);
    return size;
  }

  return -EINVAL;
}

static ssize_t rf433_codeword_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
char buffer[ADDRESS_SIZE + COMMAND_SIZE + 1];
__u8 i;

  if (mutex_is_locked (&rf433_mutex)) { // busy sending a frame out?
    printk(KERN_INFO "[%s] rf433 is busy\n", rf433_class.name);
    return -EBUSY;
  }

  // Successful sscanf?
  if (sscanf (buf, "%s", buffer)) {

    for (i = 0; i < ADDRESS_SIZE + COMMAND_SIZE; i++) 
      if (buffer[i] != '0' && buffer[i] != '1' && buffer[i] != 'f' && buffer[i] != 'F')
        return -EINVAL;

    strncpy (channel -> pkt.codeword, buffer, ADDRESS_SIZE + COMMAND_SIZE);
    return size;
  }

  return -EINVAL;
}

static ssize_t rf433_send_store (struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
struct rf433_channel *channel = dev_get_drvdata (dev);
ktime_t ktime;
char buffer[13];
__u16 action;
__u8 i, j;
__u32 reg;

  // Successful sscanf?
  if (sscanf (buf, "%hu", &action)) {

    // Stop the presses asap (do not care for mutex being up)
    if (!action) {

      // Don't care if it's still running, just cancel (unless it was never initialized)
      if (hr_timer.start_pid != 0)
        hrtimer_cancel (&hr_timer);

      // release mutex - ready to send again
      mutex_unlock (&rf433_mutex);

      // Turn the radio off
      reg = ioread32 (channel -> datareg);
      reg &= ~(1 << 0x06);      
      iowrite32 (reg, channel -> datareg);

      printk(KERN_INFO "[%s] Stopping timer pid: %d\n", rf433_class.name, hr_timer.start_pid);

      return size;
    }

    if (!mutex_trylock (&rf433_mutex)) { // returns 1 if successful and 0 if there is contention
      printk(KERN_INFO "[%s] rf433 is busy, bailing\n", rf433_class.name);
      return -EBUSY;
    }

    // process the codeword
    strncpy (buffer, channel -> pkt.codeword, ADDRESS_SIZE + COMMAND_SIZE);
    buffer[sizeof(buffer) - 1] = '\0';

    printk(KERN_INFO "[%s] Sending [%s]\n", rf433_class.name, buffer);

    for (i = 0; i < ADDRESS_SIZE + COMMAND_SIZE; i++) {

      j = 0;
      switch (buffer[i]) {
	  case '0':
	 	channel -> bitstring[i * 4 + j++] = 0; // 0 is a narrow interval, 1 - wide interval
	 	channel -> bitstring[i * 4 + j++] = 1;
	 	channel -> bitstring[i * 4 + j++] = 0;
	 	channel -> bitstring[i * 4 + j]   = 1;
		break;
	  case '1':
	 	channel -> bitstring[i * 4 + j++] = 1;
	 	channel -> bitstring[i * 4 + j++] = 0;
	 	channel -> bitstring[i * 4 + j++] = 1;
	 	channel -> bitstring[i * 4 + j]   = 0;
		break;
	  case 'f':
	  case 'F':
	 	channel -> bitstring[i * 4 + j++] = 0;
	 	channel -> bitstring[i * 4 + j++] = 1;
	 	channel -> bitstring[i * 4 + j++] = 1;
	 	channel -> bitstring[i * 4 + j]   = 0;
		break;
      }
    }

    // Finish the codeword with sync bit (first half; narrow interval)
    channel -> bitstring[48] = 0;

    if (action > 0) {

      // Start timer
      ktime = ktime_set (0, INTERVAL(channel -> bitstring[0]));
      hrtimer_init (&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
      hr_timer.function = &waveform_hrtimer_callback;

      if (action == 1) 
        channel -> codewords = NORMAL_CODEFRAME;
      
      // "Endless" number of codewords (continuous send). Useful for programming of controlled devices
      if (action == 2)
        channel -> codewords = ENDLESS_CODEFRAME;

      channel -> codebits = 0;

      // Set out bit high 
      reg = ioread32 (channel -> datareg);
      reg |= (1 << 0x06);      
      iowrite32 (reg, channel -> datareg);

      hrtimer_start (&hr_timer, ktime, HRTIMER_MODE_REL);

      // printk(KERN_INFO "[%s] [part #%d]: %d\n", rf433_class.name, channel -> codebits, channel -> bitstring[channel -> codebits]);

      return size;
    }

  }

  return -EINVAL;
}

enum hrtimer_restart waveform_hrtimer_callback (struct hrtimer *timer) {
// __u16 restart;
__u32 reg;

  // Flip output bit  (Manchester encoding - every tramsition is a flip of the bit)
  reg = ioread32 (channel.datareg);
  reg ^= (1 << 0x06);
  iowrite32 (reg, channel.datareg);

  channel.codebits++;

  if (channel.codebits > sizeof (channel.bitstring)) {

    channel.codewords--;

    // Code frame is completed
    if (!channel.codewords) {

      // Turn the radio off
      reg = ioread32 (channel.datareg);
      reg &= ~(1 << 0x06);      
      iowrite32 (reg, channel.datareg);

      // release mutex - ready to send again
      mutex_unlock (&rf433_mutex);

      printk(KERN_INFO "[%s] Command #%d sent\n", rf433_class.name, ++channel.use_count);

      return HRTIMER_NORESTART;
    }

    channel.codebits = 0;

    // Set out bit high 
    reg = ioread32 (channel.datareg);
    reg |= (1 << 0x06);      
    iowrite32 (reg, channel.datareg);

    hrtimer_forward (timer, ktime_get (), ktime_set (0, INTERVAL(channel.bitstring[0])));

    return HRTIMER_RESTART;
  }
  else
  if (channel.codebits == sizeof (channel.bitstring)) {

    // send sync bit
    hrtimer_forward (timer, ktime_get (), ktime_set (0, SYNC));

    // printk(KERN_INFO "[%s] [part #%d]: %d\n", rf433_class.name, channel.codebits, 0);

    return HRTIMER_RESTART;
  }
  else {

    hrtimer_forward (timer, ktime_get (), ktime_set (0, INTERVAL(channel.bitstring[channel.codebits])));

    // printk(KERN_INFO "[%s] [part #%d]: %d\n", rf433_class.name, channel.codebits, channel.bitstring[channel.codebits]);

    return HRTIMER_RESTART;
  }

  return HRTIMER_NORESTART;
}

module_init(rf433_init);
module_exit(rf433_exit);
