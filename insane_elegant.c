#include <linux/module.h>
#include <linux/slab.h>
#include "insane.h"

static struct parity_places algorithm_elegant( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number );
static int elegant_configure( struct insane_c *ctx );

#define SUBSTRIPES 2      // Substripes in virtual stripe
#define SUBSTRIPE_DATA 5  // Substripe length without parity
#define E_BLOCKS 1        // Empty blocks count

struct insane_algorithm elegant_alg = {
	.name       = "elegant",
	.p_blocks   = SUBSTRIPES + 1,
	.e_blocks   = E_BLOCKS,
	.stripe_blocks = (SUBSTRIPE_DATA + 1) * SUBSTRIPES + E_BLOCKS + 1,
	.map        = algorithm_elegant,
	.configure  = elegant_configure,
    	.module     = THIS_MODULE
};

/*
 * Elegant RAID algorithm
 */
static struct parity_places algorithm_elegant( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number )
{
	struct parity_places parity;
	
	u64 virtual_stripe;	
	u64 vs_position;
	u64 data_block, lane_pos;
	u64 global_gap, local_gap;
	u64 last_block;

	sector_t local_parity, global_parity;
	int block_size;
	int total_disks;
	int i;
	
	block_size = ctx->chunk_size;
	total_disks = elegant_alg.ndisks;

	data_block = *device_number + block * total_disks;
	dm_log("data_block: %lld\n", data_block);
	/*
	 * Let's get position of block in VS.
	 * SUBSTRIPE_DATA - quantity of data blocks in substripe;
	 * SUBSTRIPES - quantity of substripes in VS;
	 */
	
	virtual_stripe = data_block;
	vs_position = sector_div(virtual_stripe, (SUBSTRIPE_DATA * SUBSTRIPES)); 
	// virtual_stripe  	- number of vs.
	// vs_position 		- offset in vs without counting parity.

	// Now let's count positions of syndromes.
	local_parity = vs_position;
	sector_div(local_parity, SUBSTRIPE_DATA);
	local_parity = (virtual_stripe * elegant_alg.stripe_blocks) + (local_parity + 1) * (SUBSTRIPE_DATA + 1) - 1;

	global_parity = (virtual_stripe + 1) * (elegant_alg.stripe_blocks) - 1;
	
	// Parity in sequential	mode
	if (ctx->io_pattern == SEQUENTIAL)
	{
		parity.start_sector = virtual_stripe * elegant_alg.stripe_blocks;
		parity.start_device = sector_div(parity.start_sector, total_disks);
		parity.start_sector = parity.start_sector * block_size;

		for (i = 0; i < elegant_alg.p_blocks - 1; i++) {
			parity.device_number[i] = (parity.start_device + SUBSTRIPE_DATA + i*(SUBSTRIPE_DATA + 1)) % ctx->ndev;
			if (parity.device_number[i] < parity.start_device) {
				parity.sector_number[i] = parity.start_sector + ctx->chunk_size;
			} else {
				parity.sector_number[i] = parity.start_sector;
			}
		}
		parity.device_number[i] = (parity.start_device + elegant_alg.stripe_blocks - 1) % ctx->ndev;
		if (parity.device_number[i] < parity.start_device) {
			parity.sector_number[i] = parity.start_sector + ctx->chunk_size;
		} else {
			parity.sector_number[i] = parity.start_sector;
		}
		parity.device_number[i+1] = -1;
	
		last_block = parity.start_device + elegant_alg.stripe_blocks - 4;
		i = sector_div(last_block, total_disks);
		last_block = i;
	}
	// Parity in random mode
	else {	
		parity.device_number[0] = sector_div(local_parity, total_disks);
		parity.device_number[1] = sector_div(global_parity, total_disks);

		parity.sector_number[0] = local_parity * block_size;
		parity.sector_number[1] = global_parity * block_size;
	
		parity.device_number[2] = -1;

		parity.start_sector = virtual_stripe * elegant_alg.stripe_blocks;
		parity.start_device = sector_div(parity.start_sector, total_disks);
		parity.start_sector = parity.start_sector * block_size;
	}

	// Okay, let's return to counting position of data block
	// now we can calculate global_gap...
	global_gap = virtual_stripe * (SUBSTRIPES + elegant_alg.e_blocks + 1);
	
	// ...and local_gap
	local_gap = vs_position;	
	sector_div(local_gap, SUBSTRIPE_DATA);

	// Almost ready.
	lane_pos = data_block + global_gap + local_gap;
	
	/*
	 * Finally, we can get new device_number and sector number from lane_pos.
	 */

	*device_number = sector_div(lane_pos, total_disks);

	parity.last_block = false;
	if (ctx->io_pattern == SEQUENTIAL)
	{
		if (*device_number == last_block)
			parity.last_block = true;
	}
	
	i = sector_div(*sector, block_size);
	*sector = lane_pos * block_size + i;
	
	return parity;
}

static int elegant_configure( struct insane_c *ctx )
{
	if (!ctx)
		return -EINVAL;

	elegant_alg.ndisks = ctx->ndev;
	return 0;
}

static int __init insane_elegant_init( void )
{
	int r;
	
	r = insane_register( &elegant_alg );
	if (r)
		return r;
	
	return 0;
}

static void __exit insane_elegant_exit( void )
{
	insane_unregister( &elegant_alg );
}

module_init(insane_elegant_init);
module_exit(insane_elegant_exit);

MODULE_AUTHOR("Evgeniy Anastasiev");
MODULE_LICENSE("GPL");
