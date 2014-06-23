#include <linux/module.h>
#include "insane.h"

static struct parity_places algorithm_raid6( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number );
static int raid6_configure( struct insane_c *ctx );

struct insane_algorithm raid6_alg = {
	.name = "raid6",
	.p_blocks = 2,
	.e_blocks = 0,
	.map = algorithm_raid6,
	.configure = raid6_configure,
        .recover = raid6_recover,
	.module = THIS_MODULE
};

// Sector and device mapping callback
static struct parity_places algorithm_raid6(struct insane_c *ctx, u64 block, sector_t *sector, int *device_number)
{
	struct parity_places parity;

	u64 position;
	u64 i, Y;

	u64 data_block; // Data block number
	u64 local_gap;	// Parity blocks skipped in current lane
	u64 lane;
	u64 block_start, block_offset;

	int block_size;
	int total_disks;

	block_size = ctx->chunk_size;
	total_disks = raid6_alg.ndisks;

	data_block = *device_number + block * total_disks;

	lane = data_block;
	position = sector_div(lane, total_disks - raid6_alg.p_blocks); 

	i = lane;
	Y = sector_div(i, total_disks);

	local_gap = 2;

/* Now we have "square" of blocks. 
 * Position is horisontal coordinate, Y - vertical coordinate
 *
 * We would like to see something like this (D - data block, S - syndrome).
 *			_
 *	DDDDDSS  |
 *	DDDDSSD  |
 *	DDDSSDD  |
 *	DDSSDDD   > Square 1  
 *	DSSDDDD  |
 *	SSDDDDD  |
 *	SDDDDDS _|
 =	DDDDDSS  |
 *	DDDDSSD  |
 *	DDDSSDD  |
 *	DDSSDDD   > Square 2
 *	DSSDDDD  |
 *	SSDDDDD  |
 *	SDDDDDS _|
 *
 *
 * Local gap - how many syndromes we should skip in current stripe.
 * For example, in this masterpiece scheme local gap equals: 
 *	0 in the top left corner; 
 *	1 in the last stripe; 
 *	2 in all other positions.
 *
 * So, local_gap_scheme looks like:
 *	
 *	0000022
 *	0000222
 *	0002222
 *	0022222
 *	0222222
 *	2222222
 *	1111111
 *
 * Default local gap equals 2. 
 * At first, we will decrease local gap in the last stripe (Inner clause)
 * Secondly, we will decrease local gap in the top left corner (Outer clause)
 */

	// If we are in last stripe in square then we skip 1 syndrom in current lane
	if( Y == (total_disks - 1) )
		local_gap = 1;

	// If we didn't cross square diagonal then we don't skip syndromes in
	// current lane
	if( position + Y < (total_disks - 2) )
		local_gap = 0;

	// Remap block accounting all gaps
	position = data_block + local_gap + (2 * lane);

	// Remap device number
	*device_number = sector_div(position, total_disks);

	// For sequential writing: let's check number of current block
		parity.last_block = false;
	if (ctx->io_pattern == SEQUENTIAL)
	{
		if (*device_number + (2 - local_gap) == (total_disks - 1))
			parity.last_block = true;
	}

	// Get offset in block and remap sector
	block_offset = sector_div(*sector, block_size);
	block_start = position * block_size;
	*sector = block_start + block_offset;

	// Now it's time to count, where our syndromes are.
	parity.start_device = 0;
	parity.start_sector = block_start;
	
	// Parities have same sector, different devices
	parity.sector_number[0] = block_start;
	parity.sector_number[1] = block_start;

	parity.device_number[1] = total_disks - 1 - Y;
	if( Y < total_disks - 1 )
		parity.device_number[0] = parity.device_number[1] - 1;
	 else
		parity.device_number[0] = total_disks - 1;

	parity.device_number[2] = -1;
	return parity;
}

static struct recover_stripe raid6_recover(struct insane_c *ctx, u64 block, int device_number) {
    struct recover_stripe result;
   
    int block_place, counter, device, total_disks, chunk_size;

    total_disks = ctx->raid6_alg.ndisks;
    chunk_size = ctx->chunk_size;

    // place of block in current stripe
    block_place = (block + device_number) % total_disks;

    // starting block
    device = (total_disks - block) % total_disks;

    counter = 0;
    // we should read (total_disks - 2) blocks to recover
    while (counter < total_disks - 2) {
        if (device != block_place) {
            result.read_sector[counter] = block * chunk_size;
            result.read_device[counter] = device;
            counter++;
        }
        device = (device + 1) % total_disks; 
    }

    result.quantity = total_disks - 2;
    return result;
}

static int raid6_configure( struct insane_c *ctx )
{
	if (!ctx)
		return -EINVAL;

	raid6_alg.ndisks = ctx->ndev;
	raid6_alg.stripe_blocks = ctx->ndev;
	return 0;
}

static int __init insane_raid6_init( void )
{
	int r;

	r = insane_register( &raid6_alg );
	if (r)
		return r;

	return 0;
}

static void __exit insane_raid6_exit( void )
{
	insane_unregister( &raid6_alg );
}

module_init(insane_raid6_init);
module_exit(insane_raid6_exit);

MODULE_AUTHOR("Evgeniy Anastasiev");
MODULE_LICENSE("GPL");
