#include <linux/module.h>
#include "insane.h"

static struct parity_places algorithm_raid7( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number );
static int raid7_configure( struct insane_c *ctx );
static struct recover_stripe raid7_recover(struct insane_c *ctx, u64 block, int device_number);

struct insane_algorithm raid7_alg = {
	.name = "raid7",
	.p_blocks = 3,
	.e_blocks = 0,
	.map = algorithm_raid7,
	.configure = raid7_configure,
        .recover = raid7_recover,
	.module = THIS_MODULE
};

static struct parity_places algorithm_raid7(struct insane_c *ctx, u64 block, sector_t *sector, int *device_number)
{
	u64 position;

	u64 i, Y;

	u64 data_block; //Data block number
	u64 local_gap;
	u64 lane;
	u64 block_start, block_offset;

	int block_size;
	int total_disks;

	struct parity_places parity;

	block_size = ctx->chunk_size;
	total_disks = raid7_alg.ndisks;

	data_block = *device_number + block * total_disks;

	lane = data_block;
	position = sector_div(lane, total_disks - raid7_alg.p_blocks); 

	i = lane;
	Y = sector_div(i, total_disks);
	
	local_gap = 3;

	if (Y == (total_disks - 1))
		local_gap = 1;

	if (Y == (total_disks - 2))
		local_gap = 2;

	if ((position + Y) < (total_disks - 3))
		local_gap = 0;

	position = data_block + local_gap + (3 * lane);
	
	*device_number = sector_div(position, total_disks);

	block_offset = sector_div(*sector, block_size);
	block_start = position * block_size;
	*sector = block_start + block_offset;

	parity.last_block = false;
	if (ctx->io_pattern == SEQUENTIAL)
	{
		if (*device_number + (3 - local_gap) == (total_disks - 1))
			parity.last_block = true;
	}

	// Time to count parity places
	
	parity.start_device = 0;
	parity.start_sector = block_start;
	
	parity.sector_number[0] = block_start;
	parity.sector_number[1] = block_start;
	parity.sector_number[2] = block_start;

	parity.device_number[2] = total_disks - 1 - Y;
	
	if (Y == (total_disks - 1)) {
		parity.device_number[1] = total_disks - 1;
		parity.device_number[0] = total_disks - 2;
	}
	else if (Y == (total_disks - 2)) {
		parity.device_number[1] = 0;
		parity.device_number[0] = total_disks - 1;
	}
	else {
		parity.device_number[1] = parity.device_number[2] - 1;
		parity.device_number[0] = parity.device_number[2] - 2;
	}

	parity.device_number[3] = -1;
	
	return parity;
}

static struct recover_stripe raid7_recover(struct insane_c *ctx, u64 block, int device_number) {
    struct recover_stripe result;

    int block_place, counter, device, total_disks, chunk_size;
    
    u64 onotole;

    total_disks = raid7_alg.ndisks;
    chunk_size = ctx->chunk_size;

    // place of block in current stripe
    onotole = block + device_number;
    block_place = sector_div(onotole, total_disks);

    // starting block
    onotole = block;
    device = sector_div(onotole, total_disks);
    if (device != 0)
        device = total_disks - device;
    else
        device = 0;

    counter = 0;
    // we should read (total_disks - 3) blocks to recover
    while (counter < total_disks - 3) {
        if (device != device_number) {
            result.read_sector[counter] = block * chunk_size;
            result.read_device[counter] = device;
            counter++;
        }
        device++;
        onotole = sector_div(device, total_disks);
        device = onotole;
    }
    
    result.quantity = total_disks - 3;

    return result;
}

static int raid7_configure( struct insane_c *ctx )
{
	if (!ctx)
		return -EINVAL;

	raid7_alg.ndisks = ctx->ndev;
	raid7_alg.stripe_blocks = ctx->ndev;    
	return 0;
}

static int __init insane_raid7_init( void )
{
	int r;

	r = insane_register( &raid7_alg );
	if (r)
		return r;
	
	return 0;
}

static void __exit insane_raid7_exit( void )
{
	insane_unregister( &raid7_alg );
}

module_init(insane_raid7_init);
module_exit(insane_raid7_exit);

MODULE_AUTHOR("Evgeniy Anastasiev");
MODULE_LICENSE("GPL");
