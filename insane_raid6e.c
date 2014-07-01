#include <linux/module.h>
#include <linux/device-mapper.h>
#include "insane.h"

static struct parity_places algorithm_raid6e( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number );
static int raid6e_configure( struct insane_c *ctx );
static struct recover_stripe raid6e_recover(struct insane_c *ctx, u64 block, int device_number);

#define DEGRADED_DISK 1

struct insane_algorithm raid6e_alg = {
	.name = "raid6e",
	.p_blocks = 2,
	.e_blocks = 1,
	.map = algorithm_raid6e,
	.configure = raid6e_configure,
        .recover = raid6e_recover,
	.module = THIS_MODULE
};

struct block_place {
	u64 sector;
	int device_number;
};

static struct block_place get_degraded_block(u64 block, int total_disks, int block_size, u64 device_length)
{
	struct block_place degraded_place;
	u64 block_pos;
	u64 empty_zone_offset;
	
	block_pos = block;

	// Let's count device number in empty zone
	degraded_place.device_number = sector_div(block_pos, (total_disks - 1)); 
	// Little fix
	if (degraded_place.device_number >= DEGRADED_DISK)
		degraded_place.device_number++;
	
	// Now let's count sector number in empty_zone. 
	// First of all, we should count empty_zone_offset.
	empty_zone_offset = device_length;

	// p_blocks + e_block = 3.
	sector_div(empty_zone_offset, (total_disks - 3)); 
	// Now empty_zone_offset == one real disk capacity (including empty and parity)

	sector_div(empty_zone_offset, total_disks); // Almost ready.

	degraded_place.sector = empty_zone_offset + block_pos * block_size;
	
	return degraded_place;
}

/*
 * Algorithm of RAID6E with degraded drive
 */
static struct parity_places algorithm_raid6e( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number)
{
	struct parity_places parity;
	struct block_place degraded_place;

	u64 i, Y;
	u64 position;
	u64 local_gap;

	u64 data_block;
	u64 lane;
	u64 block_offset, block_start;

	int block_size;
	int total_disks;
	sector_t device_length;

	
	block_size = ctx->chunk_size;
	total_disks = raid6e_alg.ndisks;
	device_length = ctx->ti->len;
	
	data_block = *device_number + block * total_disks;
	lane = data_block;

	// NORMAL SITUATION
	// Everything like in RAID 6
	position = sector_div(lane, total_disks - raid6e_alg.p_blocks); 
	i = lane;
	Y = sector_div(i, total_disks);
 
	local_gap = 2;

	// If we are in last stripe in square then we skip 1 syndrome in current lane
	if (Y == (total_disks - 1))
		local_gap = 1;

	// If we didn't cross square diagonal then we don't skip syndromes in
	// current lane
	if (position + Y < (total_disks - raid6e_alg.p_blocks))
		local_gap = 0;

	// Remap block accounting all gaps
	position = data_block + local_gap + (raid6e_alg.p_blocks * lane);
		
	// Remap device_number
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
	*sector = position * block_size + i;
	
	// Now it's time to count, where our syndromes are

	parity.start_device = 0;
	parity.start_sector = block_start; 
	
	parity.sector_number[0] = block_start;
	parity.sector_number[1] = block_start;
	
	parity.device_number[1] = total_disks - 1 - Y;

	if (Y < total_disks - 1)
		parity.device_number[0] = parity.device_number[1] - 1;
	else
		parity.device_number[0] = total_disks - 1;

	if (*device_number == DEGRADED_DISK) {
		degraded_place = get_degraded_block(position, total_disks, block_size, device_length);
		*device_number = degraded_place.device_number;
		*sector = degraded_place.sector + i;
	}
	else if (parity.device_number[0] == DEGRADED_DISK) {
		degraded_place = get_degraded_block(position, total_disks, block_size, device_length);
		parity.device_number[0] = degraded_place.device_number;
		parity.sector_number[0] = degraded_place.sector;
	}
	else if (parity.device_number[1] == DEGRADED_DISK) {
		degraded_place = get_degraded_block(position, total_disks, block_size, device_length);
		parity.device_number[1] = degraded_place.device_number;
		parity.sector_number[1] = degraded_place.sector;
	}

	parity.device_number[2] = -1;
	return parity;
}

static struct recover_stripe raid6e_recover(struct insane_c *ctx, u64 block, int device_number) {
    struct recover_stripe result;
    struct block_place read_place;

    int total_disks, chunk_size;
    u64 position, device_length;

    total_disks = raid6e_alg.ndisks;
    chunk_size = ctx->chunk_size;
    device_length = ctx->ti->len;

    position = total_disks * block + device_number;

    read_place = get_degraded_block(position, total_disks, chunk_size, device_length);

    result.read_sector[0] = read_place.sector;
    result.read_device[0] = read_place.device_number;

    result.quantity = 1;

    return result;
}


static int raid6e_configure( struct insane_c *ctx )
{
	if (!ctx)
		return -EINVAL;

	raid6e_alg.ndisks = ctx->ndev;
	raid6e_alg.stripe_blocks = ctx->ndev;	 
	return 0;
}

static int __init insane_raid6e_init( void )
{
	int r;

	r = insane_register( &raid6e_alg );
	if (r)
		return r;

	return 0;
}

static void __exit insane_raid6e_exit( void )
{
	insane_unregister( &raid6e_alg );
}

module_init(insane_raid6e_init);
module_exit(insane_raid6e_exit);

MODULE_AUTHOR("Evgeniy Anastasiev");
MODULE_LICENSE("GPL");
