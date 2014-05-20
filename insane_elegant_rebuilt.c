#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>
#include "insane.h"

static struct parity_places algorithm_elegant_rebuilt( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number );
static int elegant_rebuilt_configure( struct insane_c *ctx );

#define SUBSTRIPES 2      // Substripes in virtual stripe
#define SUBSTRIPE_DATA 5  // Substripe length without parity
#define EXTRA_DISKS 1     // Added disks
#define E_BLOCKS 1        // Empty blocks count

struct insane_algorithm elegant_rebuilt_alg = {
	.name       = "elegant_rebuilt",
	.p_blocks   = SUBSTRIPES + 1,
	.e_blocks   = E_BLOCKS,
	.stripe_blocks = (SUBSTRIPE_DATA + 1) * SUBSTRIPES + E_BLOCKS + EXTRA_DISKS + 1,
	.map        = algorithm_elegant_rebuilt,
	.configure  = elegant_rebuilt_configure,
};

/*
 * Elegant algorithm of rebuilt RAID.
 */
static struct parity_places algorithm_elegant_rebuilt( struct insane_c *ctx, u64 block, sector_t *sector, int *device_number )
{
	long long int length, ext_place;
	sector_t vs_pos, lane_pos, ds_pos, i, local_gap, global_gap, ext_mark, gs_pos, ls_pos, ND, OD;
	int block_size;
	int total_disks;
	int e_blocks;

	struct parity_places parity;
	/*
	if(!ctx)
		return -EINVAL;
	*/

	block_size = ctx->chunk_size;
	total_disks = elegant_rebuilt_alg.ndisks;
	e_blocks = elegant_rebuilt_alg.e_blocks;

	length = ctx->ti->len;
	sector_div(length, total_disks);
	ext_place = length * (total_disks - EXTRA_DISKS);
	sector_div(ext_place, block_size);

	ds_pos = *device_number + total_disks * block;

	// If (ds_pos < ext_place), then we should write to the "old" disks. Else - to the "new" ones.
		
	ext_mark = ds_pos - ext_place;
	
	ext_mark = ((ext_mark >> 31) && 1);

	/*
	 * If we are in zone of new disks, then ext_mark = 0.
	 * Else ext_mark == 1.
	 * It means, that sense of all these monstrous constructions is:
	 *
	 * EM - ext_mark;
	 * ND - case of NEW_DISKS;
	 * OD - case of OLD_DISKS;
	 *
	 * IF (EM == 1)
	 * 	EM * OD + (1 - EM) * ND == OD
	 * IF (EM == 0)
	 * 	EM * OD + (1 - EM) * ND == ND
	 *
	 * So, we have an algorithm without if-else constructions.
	*/

	i = ds_pos;
	vs_pos = sector_div(i, (SUBSTRIPE_DATA * SUBSTRIPES)); 

	global_gap = ext_mark * (i * (SUBSTRIPES + e_blocks + EXTRA_DISKS + 1)); // old_disks (used in this case only)

	local_gap = ext_mark * vs_pos;

	sector_div(local_gap, SUBSTRIPE_DATA);

	lane_pos = 	ds_pos 
			+ ext_mark * (global_gap + local_gap)	// old_disks
			- (1 - ext_mark) * ext_place;		// new_disks

	// Let's count parity
	
	gs_pos = lane_pos;
	
	sector_div(gs_pos,
			ext_mark * (elegant_rebuilt_alg.stripe_blocks - EXTRA_DISKS)	// old disks
			+ (1 - ext_mark) * (EXTRA_DISKS));			// new_disks

	// gs_pos now is number of virtual stripe

	parity.start_sector = gs_pos * elegant_rebuilt_alg.stripe_blocks;

	parity.start_device = sector_div(parity.start_sector, total_disks - EXTRA_DISKS);
	parity.start_sector = parity.start_sector * block_size;

	// Now gs_pos is real position of the global syndrome
	gs_pos = (gs_pos + 1) * elegant_rebuilt_alg.stripe_blocks - 1;
	
	// It's time to count position of local syndrome!
	
	sector_div(vs_pos, SUBSTRIPE_DATA); 
	vs_pos++;
	// Now vs_pos is number of the local syndrome in current virtual stripe.
	
	// Firstly, let's find, where is the local syndrome, if we are in zone of old disks.
	
	OD = gs_pos - elegant_rebuilt_alg.stripe_blocks + EXTRA_DISKS + 1;
	// Now OD is the position of the first block in current virtual stripe.
	
	OD = OD + vs_pos * (SUBSTRIPE_DATA + 1);
	// Now OD is the position of the first block after sought-for local syndrome
	
	OD--;
	// Now OD is the position of the local syndrome.

	// Secondly, let's find, where is the local syndrome, if we are in the zone of new disks.
	ND = gs_pos - E_BLOCKS - 1; 

	ls_pos = ext_mark * OD 	+ (1 - ext_mark) * ND;


	parity.device_number[0] = sector_div(ls_pos, total_disks - EXTRA_DISKS);
	parity.sector_number[0] = ls_pos * block_size;

	parity.device_number[1] = sector_div(gs_pos, total_disks - EXTRA_DISKS);
	parity.sector_number[1] = gs_pos * block_size;

	// counting parity finished.


	i = sector_div(lane_pos, 
			ext_mark * (total_disks - EXTRA_DISKS)	// old_disks
			+ (1 - ext_mark) * EXTRA_DISKS		// new_disks
			);

	*device_number = i;
	// In case of old disks, it's enough
	*device_number += (1 - ext_mark) * (total_disks - EXTRA_DISKS);
	// But if we are in the zone of new disks, we should add the gap

	i = sector_div(*sector, block_size);

	*sector = lane_pos * block_size + i;

	parity.device_number[2] = -1;

	return parity;
}


static int elegant_rebuilt_configure( struct insane_c *ctx )
{
	if (!ctx)
		return -EINVAL;

	elegant_rebuilt_alg.ndisks = ctx->ndev;
	return 0;
}

static int __init insane_elegant_rebuilt_init( void )
{
	int r;
	
	r = insane_register( &elegant_rebuilt_alg );
	if (r)
		return r;
	
	return 0;
}

static void __exit insane_elegant_rebuilt_exit( void )
{
	insane_unregister( &elegant_rebuilt_alg );
}

module_init(insane_elegant_rebuilt_init);
module_exit(insane_elegant_rebuilt_exit);

MODULE_AUTHOR("Evgeniy Anastasiev");
MODULE_LICENSE("GPL");
