#include <linux/init.h>            // For module_init, module_exit macros
#include <linux/module.h>          // For MODULE_LICENSE, MODULE_AUTHOR, etc.
#include <linux/kernel.h>          // For printk function
#include <linux/fs.h>              // For register_chrdev, unregister_chrdev
#include <linux/slab.h>            // For kmalloc, kfree
#include <linux/timer.h>            // timer API
#include <linux/jiffies.h>          // For jiffies and msecs_to_jiffies
#include <linux/uaccess.h>
#include <asm/uaccess.h>            /* copy_from/to_user */
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

/* DEFINE PIN VARIABLES */
#define RED_LED 67
#define YELLOW_LED 68
#define GREEN_LED 44
#define BTN0 26
#define BTN1 46


/* FUNCTION HEADERS */
MODULE_LICENSE("Dual BSD/GPL");
static ssize_t mytraffic_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t mytraffic_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
// static int mytraffic_release(struct inode *inode, struct file *filp);
void timer_callback(struct timer_list * data);
static irqreturn_t button_handler(int irq, void *dev_id);
static irqreturn_t button_handler_ped(int irq, void *dev_id); //ADDED
void prepareOutput(void);
static void traffic_exit(void);
static int traffic_init(void);

/* TIMER VARIABLES */
static struct timer_list * etx_timer;

/* SETUP VARIABLES */
static int traffic_major = 61;              // major number val

struct file_operations mytraffic_fops = {
	.owner = THIS_MODULE,
    .read = mytraffic_read, 
    .write = mytraffic_write,
};

/* STATE VARIABLES */
static int state;
static int mode; 
static int blink_red = 1;
static int blink_yellow = 1;
static int cycleDuration = 1000;
static int hzRate = 1;
static int ped_call_active = 0; //ADDED


/* BUFFER VARIABLES */
static char *return_buffer;
static char *receive_buffer;
static unsigned capacity = 128;
static char output_msg[128];
static char operationalMode[128];
static char currentStatus[128] = "";
static int buffer_len;
static char ped_call_msg[128] = ""; //ADDED




static int traffic_init(void)
{
    /* variable init */
    int result;
    int request0;
    int request1; //ADDED
    int red_response, yellow_response, green_response, btn0_response, btn1_response;
    int green_dir, red_dir, yellow_dir, btn0_dir, btn1_dir;
    
    /* register as a file */
    result = register_chrdev(traffic_major, "mytraffic", &mytraffic_fops);

    /* make space for for buffer */
    return_buffer = kmalloc(capacity, GFP_KERNEL);
    memset(return_buffer, 0, capacity);
    receive_buffer = kmalloc(capacity, GFP_KERNEL);
    memset(receive_buffer, 0, capacity);

    /* timer init */
    etx_timer = (struct timer_list *) kmalloc(sizeof(struct timer_list), GFP_KERNEL);

    /* Request all GPIOs */
    green_response = gpio_request(GREEN_LED, "led");
    red_response = gpio_request(RED_LED, "led");
    yellow_response = gpio_request(YELLOW_LED, "led");

    btn0_response = gpio_request(BTN0, "button");
    request0 = request_irq(gpio_to_irq(BTN0), button_handler, IRQF_TRIGGER_FALLING, "button_irq", NULL);

    btn1_response = gpio_request(BTN1, "button");
    request1 = request_irq(gpio_to_irq(BTN1), button_handler_ped, IRQF_TRIGGER_FALLING, "button_irq1", NULL); //ADDED


    /* Set GPIO direction to output */
    green_dir = gpio_direction_output(GREEN_LED, 0);
    red_dir = gpio_direction_output(RED_LED, 0);
    yellow_dir = gpio_direction_output(YELLOW_LED, 0);
    btn0_dir = gpio_direction_input(BTN0);
    btn1_dir = gpio_direction_input(BTN1);

    /* set green start */
    gpio_set_value(GREEN_LED,1);
    gpio_set_value(YELLOW_LED,0);
    gpio_set_value(RED_LED,0);

    timer_setup(etx_timer, timer_callback, 0);
    mod_timer(etx_timer, jiffies + msecs_to_jiffies( 3 * cycleDuration / hzRate));
    state = 0;
    mode = 0;

    /* succesfully inserted */
    printk(KERN_ALERT "Inserting mytraffic module\n");

    return 0;

}

static void traffic_exit(void)
{
    del_timer(etx_timer);

    /* Freeing the major number */
	unregister_chrdev(traffic_major, "mytraffic");
    
    /* turn off LEDs */

    gpio_set_value(RED_LED, 0);
    gpio_set_value(YELLOW_LED, 0);
    gpio_set_value(GREEN_LED, 0);

    /* free LEDs, IRQ, and BUTTONs*/
    gpio_free(RED_LED);
    gpio_free(YELLOW_LED);
    gpio_free(GREEN_LED);
    free_irq(gpio_to_irq(BTN0), NULL);
    free_irq(gpio_to_irq(BTN1), NULL);
    gpio_free(BTN0);
    gpio_free(BTN1);

    
    if (return_buffer)
	{
        kfree(return_buffer);
	}

    if (receive_buffer)
    {
        kfree(receive_buffer);
    }
    

    if (etx_timer) {
        kfree(etx_timer);
    }

    printk(KERN_ALERT "Removing mytraffic module\n");
}

static ssize_t mytraffic_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
    prepareOutput();

    if (*f_pos >= strlen(return_buffer)) {
        return 0;
    }

	// Make sure max length transferred to the user module is 124
	if (count > 128) 
	{
		count = 128;
	}


	if (copy_to_user(buf, return_buffer, count))
	{
        printk(KERN_ALERT "wrong");
		return -EFAULT;
	}


    *f_pos += strlen(return_buffer);

	return strlen(return_buffer);

}

static ssize_t mytraffic_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
    int temp;
    int returnVal;
    unsigned long intConversion;
	char tbuf[256], *tbptr = tbuf;

    memset(receive_buffer, 0, capacity);

	/* end of buffer reached */
	if (*f_pos >= capacity)
	{
		printk(KERN_INFO "write called");
		return -ENOSPC;
	}

	/* do not eat more than a bite */
	if (count > 128) count = 128;

	/* do not go over the end */
	if (count > capacity - *f_pos)
		count = capacity - *f_pos;

	if (copy_from_user(receive_buffer + *f_pos, buf, count))
	{
		return -EFAULT;
	}

    tbptr += sprintf(tbptr,								   
		"write called: process id %d, command %s, count %d, chars ",
		current->pid, current->comm, count);

	for (temp = *f_pos; temp < count + *f_pos; temp++)					  
		tbptr += sprintf(tbptr, "%c", receive_buffer[temp]);

	*f_pos += count;
	buffer_len = *f_pos;

    
    returnVal = kstrtol(receive_buffer, 10, &intConversion);
    hzRate = intConversion;

    return count;
}

void timer_callback(struct timer_list * t)
{
    // current state is green
    if(mode == 0){
        if(state == 0) {
            // turn green off and turn yellow on
            gpio_set_value(GREEN_LED, 0);
            gpio_set_value(YELLOW_LED, 1);
            gpio_set_value(RED_LED, 0); //ADDED last state for each of below states

            mod_timer(etx_timer, jiffies + msecs_to_jiffies( cycleDuration / hzRate ));
            state = 1; 

        }
        // current state is yellow
        else if(state == 1) {
            if (ped_call_active == 0){ //ADDED IF addition to check if ped_call_active, if not active go to red
                gpio_set_value(GREEN_LED, 0); //ADDED
                gpio_set_value(YELLOW_LED, 0);
                gpio_set_value(RED_LED, 1);
                mod_timer(etx_timer, jiffies + msecs_to_jiffies( 2 * cycleDuration / hzRate ));
            }
            else if (ped_call_active == 1){ //ADDED full block here
                gpio_set_value(RED_LED, 1);
                gpio_set_value(YELLOW_LED, 1);
                gpio_set_value(GREEN_LED, 0);

                mod_timer(etx_timer, jiffies + msecs_to_jiffies(5 * cycleDuration / hzRate)); 
            }

            state = 2; 
            ped_call_active = 0; 
        }
        // current state is red
        else if(state == 2) {
            gpio_set_value(RED_LED, 0);
            gpio_set_value(GREEN_LED, 1);
            gpio_set_value(YELLOW_LED, 0); //ADDED


            mod_timer(etx_timer, jiffies + msecs_to_jiffies( 3 * cycleDuration / hzRate ));
            state = 0; 
            
        }

        
    }
    else if (mode == 1){ //flashingRed
        gpio_set_value(RED_LED, blink_red);
        gpio_set_value(YELLOW_LED, 0);
        gpio_set_value(GREEN_LED, 0);
        blink_red = !blink_red;
        mod_timer(etx_timer, jiffies + msecs_to_jiffies(cycleDuration / hzRate)); // Toggle every 1 second

        
    }
    else if (mode == 2){ //flashingYellow
        gpio_set_value(YELLOW_LED, blink_yellow);
        gpio_set_value(RED_LED, 0);
        gpio_set_value(GREEN_LED, 0);
        blink_yellow = !blink_yellow;
        mod_timer(etx_timer, jiffies + msecs_to_jiffies(cycleDuration / hzRate)); // Toggle every 1 second

        
    }

}

static irqreturn_t button_handler(int irq, void *dev_id) {
    if (mode == 0){
        mode = 1; 
        gpio_set_value(RED_LED, 0);
        gpio_set_value(YELLOW_LED, 0);
        gpio_set_value(GREEN_LED, 0);
        

    }
    else if (mode == 1){
        mode  = 2; 
        gpio_set_value(RED_LED, 0);
        gpio_set_value(YELLOW_LED, 0);
        gpio_set_value(GREEN_LED, 0);
    }
    else if (mode == 2){
        mode = 0; 
        gpio_set_value(RED_LED, 0);
        gpio_set_value(YELLOW_LED, 0);
        gpio_set_value(GREEN_LED, 0);
    }


    return IRQ_HANDLED;
}

static irqreturn_t button_handler_ped(int irq, void *dev_id) { //added function

    if (mode == 0){ //if in mode 0, allow for pedestrian call, turn all lights off and change timer to switch to ped call mode
        ped_call_active = 1; 
    }


    return IRQ_HANDLED;
}

void prepareOutput(void) {
    /* set-up operational mode string */

    char red_var[5];
    char green_var[5];
    char yellow_var[5];

    memset(return_buffer, '\0', 128);
    memset(operationalMode, '\0', 128); //ADDED


    if(mode == 0) {
        sprintf(operationalMode,"normal");
    }
    else if(mode == 1) {
        sprintf(operationalMode,"flashing-red");
    }
    else if(mode == 2) {
        sprintf(operationalMode,"flashing-yellow");
    }

    /* set-up current LED status string */

    // RED
    if(gpio_get_value(RED_LED) == 1){
        sprintf(red_var, "on");
    }
    else{
        sprintf(red_var, "off");
    }

    // GREEN
    if(gpio_get_value(GREEN_LED) == 1){
        sprintf(green_var, "on");
    }
    else{
        sprintf(green_var, "off");
    }

    // YELLOW
    if(gpio_get_value(YELLOW_LED) == 1){
        sprintf(yellow_var, "on");
    }
    else{
        sprintf(yellow_var, "off");
    }

    
    /* ped_call_active string */
    if (ped_call_active == 1){
        sprintf(ped_call_msg,"present"); 
    }
    else{
        sprintf(ped_call_msg,"not present"); 
    }
    
    /* prepare final return buffer */
    sprintf(output_msg,"operational mode: %s - cycle rate: %d - LED status: red %s, yellow %s, green %s, Pedestrian: %s\n",operationalMode,cycleDuration/1000,red_var,yellow_var,green_var,ped_call_msg); //modified
    memset(currentStatus,0,128);
    strncpy(return_buffer, output_msg, 128);
}

module_init(traffic_init);
module_exit(traffic_exit);