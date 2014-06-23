#ifndef INSANE_H
#define INSANE_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#define DM_MSG_PREFIX "insane:"
#define DM_IO_ERROR_THRESHOLD 15

#if LINUX_VERSION_CODE < KERNEL_VERSION( 2, 6, 39 )
#define kstrtouint( ARG, BASE, END ) simple_strtoul( ARG, END, BASE )
#endif

// printk format 
// insane: <function>:<line> <message>
#define dm_log(fmt, args...) printk( DM_MSG_PREFIX " [%s:%d] " fmt, __FUNCTION__, __LINE__, ##args )
#define dm_debug(fmt, args...) if(debug) { dm_log(fmt, ##args); }

// Backend device
struct insane_dev 
{
	struct dm_dev *dev;
	atomic_t error_count;
};

// insane context
// Each mapped device(frontend device) has it's own context.
struct insane_c 
{
	struct dm_target *ti;

	// How many sectors are being used on single backend disk
	sector_t dev_width; 

	int ndev;
	int ndev_shift;

	int chunk_size;
	int chunk_size_bytes;
	int chunk_size_pages;
	int chunk_size_shift;
	
	int io_pattern;

	// RAID algorithm descriptor
	struct insane_algorithm *alg;

	struct work_struct trigger_event;

	// This field should always be the last in this structure
	struct insane_dev devs[0]; // Homo style
};

const char *io_patterns[] = 
{
	"sequential",
	"random"
};

enum {
	SEQUENTIAL = 0, 
	RANDOM,
	IO_PATTERN_NUM
};

#define MAX_SYNDROMES 64
struct parity_places 
{
	int       device_number[MAX_SYNDROMES];
	sector_t  sector_number[MAX_SYNDROMES];
	int       start_device; // First device in current stripe
	sector_t  start_sector; // First block sector in current stripe
	bool      last_block;
};

struct recover_stripe
{
    int         quantity;
    int         read_dev[MAX_SYNDROMES];
    sector_t    read_sector[MAX_SYNDROMES];
};

// RAID algorithm descriptor
#define ALG_NAME_LEN 20
struct insane_algorithm 
{
	char name[ALG_NAME_LEN];
	unsigned int ndisks;
	unsigned int stripe_blocks;
	unsigned int p_blocks; // Parity blocks count
	unsigned int e_blocks; // Empty blocks count

	struct parity_places (*map)(struct insane_c *ctx, u64 block, sector_t *sector, int *device_number);
	int (*configure)(struct insane_c *ctx);
        struct recover_stripe (*recover)(struct insane_c *ctx, u64 block, int device_number);
	struct module *module;
	struct list_head list;
};

int insane_register(struct insane_algorithm *alg);
int insane_unregister(struct insane_algorithm *alg);

#endif // INSANE_H
