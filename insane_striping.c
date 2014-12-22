/*
 * Copyright (C) 2013-2014 Evgeniy Anastasiev, Alex Dzyoba
 * Copyright (C) 2013-2014 Raidix
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>

#include <linux/device-mapper.h>

#include <linux/time.h>

#include "insane.h"

// Driver parameter
int debug = 0;

// List of RAID algorithms
LIST_HEAD(alg_list);
DEFINE_SPINLOCK(alg_list_lock);

static void do_bio( sector_t sector, struct block_device *bdev, int bi_size, int bi_vcnt, int rw );
/*
 * An event is triggered whenever a drive drops out of a stripe volume.
 */
static void trigger_event(struct work_struct *work)
{
	struct insane_c *sc = container_of(work, struct insane_c, trigger_event);
	dm_table_event(sc->ti->table);
}

static inline struct insane_c *alloc_context(unsigned int ndev)
{
	size_t len;

	if (dm_array_too_big(sizeof(struct insane_c), sizeof(struct insane_dev),
				 ndev))
		return NULL;

	len = sizeof(struct insane_c) + (sizeof(struct insane_dev) * ndev);

	return kmalloc(len, GFP_KERNEL);
}

static void insane_recover(struct insane_c *ctx) {
    struct recover_stripe read_blocks;

    u64 i, blocks_quantity;
    int j, device_number, bi_vcnt;
    sector_t bi_size;

    unsigned long start_time, finish_time, difference;
    struct timeval tv;

    blocks_quantity = ctx->dev_width;
    sector_div(blocks_quantity, ctx->chunk_size);
    
    device_number = ctx->recovering_disk;
	
    bi_size = ctx->chunk_size_bytes;
    bi_vcnt = ctx->chunk_size_pages;

    do_gettimeofday(&tv);
    start_time = tv.tv_sec;

    for (i = 0; i < blocks_quantity; i++) {

        read_blocks = ctx->alg->recover(ctx, i, device_number);
        for ( j = 0; j < read_blocks.quantity; j++) {
            do_bio(read_blocks.read_sector[j], ctx->devs[read_blocks.read_device[j]].dev->bdev, bi_size, bi_vcnt, READ);
        }
        if (read_blocks.write_device != -1)  // may be it is empty block
            do_bio(read_blocks.write_sector, ctx->devs[read_blocks.write_device].dev->bdev, bi_size, bi_vcnt, WRITE);
    }
    do_gettimeofday(&tv);
    finish_time = tv.tv_sec;
    difference = finish_time - start_time;

    blocks_quantity = ctx->dev_width;
    sector_div(blocks_quantity, 2048);// Megabytes
    
    printk("Recovered %lld MegaBytes in %ld seconds\n", blocks_quantity, difference);
}


/*
 * Construct a insane mapping.
 * <algorithm name> <number of devices> <chunk size> <io_pattern> [<dev_path>]+
 */
static int insane_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct insane_c *sc;
	struct insane_algorithm *alg;
	int found;
	sector_t width;
	int ndev;
	int chunk_size;
	int io_pattern;
        int recovering;
	int r = -ENXIO;
	int i;
	char *end;

	if (argc < 5) {
		ti->error = "Not enough arguments";
		return -EINVAL;
	}

	found = 0;
	spin_lock( &alg_list_lock );
	list_for_each_entry( alg, &alg_list, list )
	{
		if (!alg)
		{
			dm_log("Invalid algorithm entry. Skipping...\n");
			continue;
		}
		
		dm_log("argv[0] = %s, alg->name = %s\n", argv[0], alg->name);
		if (!strncmp(argv[0], alg->name, ALG_NAME_LEN))
		{
			dm_log("Found algorithm %s.\n", alg->name);
			found = 1;
			break;
		}
	}
	spin_unlock( &alg_list_lock );

	if (!found)
	{
		dm_log("Algorithm %s is not registered\n", argv[0]);
		return -EINVAL;
	}


	ndev = simple_strtoul( argv[1], &end, 10 );
	dm_log("ndev: %d\n",ndev);
	if ( *end || !ndev) {
		ti->error = "Invalid device count";
		return -EINVAL;
	}
	dm_debug("ndev = %d\n", ndev);

	chunk_size = simple_strtoul( argv[2], &end, 10 );
	if ( *end || !chunk_size) {
		ti->error = "Invalid chunk_size";
		return -EINVAL;
	}
	dm_debug("chunk_size is %d sectors, %d KiB\n", chunk_size, chunk_size / 2);

	for( i = 0; i < IO_PATTERN_NUM; i++ )
	{
		if( !strncmp( argv[3], io_patterns[i], ALG_NAME_LEN ) ) {
			io_pattern = i;
		break;
	}
	}
	if( i == IO_PATTERN_NUM )
	{
		dm_log("Invalid I/O pattern.\n");
		return -EINVAL;
	}

        if ( io_pattern == IO_PATTERN_NUM - 1) { // recover mode
            i = 1;
            recovering = simple_strtoul( argv[4], &end, 10 ); // Why 10?
            if ( *end || !recovering) {
		ti->error = "Invalid recovering disk";
		return -EINVAL;
	    }
        } else {
            i = 0;
            recovering = 0;
        }


	if (chunk_size & (chunk_size - 1)) {
		ti->error = "Chunk size should be a power of 2";
		return -EINVAL;
	}

	// Calculate device width
	width = ti->len;
	if (sector_div(width, ndev)) {
		ti->error = "Target length not divisible by number of devices";
		return -EINVAL;
	}
	dm_debug("Each disk width is %llu sectors\n", (u64)width);

	// Do we have enough arguments for that many devices ?
	if (argc != (4 + i + ndev)) {
		ti->error = "Not enough destinations specified";
		return -EINVAL;
	}

	sc = alloc_context(ndev);
	if (!sc) {
		ti->error = "Memory allocation for insane context failed";
		return -ENOMEM;
	}
	dm_debug("Insane context allocated (%p)\n", sc);

	sc->ti = ti;
	sc->io_pattern = io_pattern;
        sc->recovering_disk = recovering;
	sc->ndev = ndev;
	sc->dev_width = width;

	if (ndev & (ndev - 1))
		sc->ndev_shift = -1;
	else
		sc->ndev_shift = __ffs(ndev);

	sc->chunk_size = chunk_size;
	sc->chunk_size_shift = __ffs(chunk_size);

	// Configure algorithm
	if (alg->configure && alg->configure(sc))
	{
		dm_log("Failed to configure algorithm runtime params\n");
		return -EINVAL;
	}
	sc->alg = alg;
	if (!try_module_get(sc->alg->module))
	{
		dm_log("Failed to get module reference\n");
		return -EFAULT;
	}

	// Reduce device size to *addressable* LBAs only in data blocks 
	// (excluding parity and empty)
	dm_debug("Insane device original width is %llu\n", (u64)ti->len);
	sector_div(ti->len, alg->stripe_blocks);
	ti->len = ti->len * (alg->stripe_blocks - alg->e_blocks - alg->p_blocks);

	// Round to virtual stripe size in sectors.
	sector_div(ti->len, (sc->ndev * chunk_size * alg->stripe_blocks));
	ti->len = (ti->len * sc->ndev * chunk_size * alg->stripe_blocks);
	dm_debug("Insane device new width is %llu\n", (u64)ti->len);
	
	sc->chunk_size_bytes = sc->chunk_size * 512;
	sc->chunk_size_pages = sc->chunk_size_bytes;
	sector_div(sc->chunk_size_pages, PAGE_SIZE);
	
	// Event handler on dropped disk
	INIT_WORK(&sc->trigger_event, trigger_event);
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 3, 6, 0 )
	r = dm_set_target_max_io_len(ti, chunk_size);
	if (r)
		return r;
#else
	ti->split_io = chunk_size;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION( 3, 9, 0 )
	ti->num_flush_requests = ndev;
	ti->num_discard_requests = ndev;
#else
	ti->num_flush_bios = ndev;
	ti->num_discard_bios = ndev;
#endif

#if LINUX_VERSION_CODE == KERNEL_VERSION( 3, 8, 0 )
	ti->num_write_same_requests = ndev;
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 3, 9, 0 )
	ti->num_write_same_bios = ndev;
#else
	// < 3.8 - empty
#endif
#endif

	dm_debug("Opening devices\n");
	argv += (4 + i);
	for (i = 0; i < ndev; i++) 
	{
		if (dm_get_device(ti, argv[i], dm_table_get_mode(ti->table), &sc->devs[i].dev))
		{
			ti->error = "Couldn't parse device struct destination";
			while (i--)
			{
				dm_put_device(ti, sc->devs[i].dev);
			}
			kfree(sc);
			return r;
		}
		atomic_set(&(sc->devs[i].error_count), 0);
		dm_debug("Got device %s(%p)\n", argv[i], sc->devs[i].dev);
	}

	ti->private = sc;
	dm_log("Insane constructor: %u devices, %lld device width, %u chunk size\n", 
		sc->ndev, (u64)sc->dev_width, sc->chunk_size);

        insane_recover(sc);
	return 0;
}


static void insane_dtr(struct dm_target *ti)
{
	unsigned int i;
	struct insane_c *sc = (struct insane_c *) ti->private;

	for (i = 0; i < sc->ndev; i++)
		dm_put_device(ti, sc->devs[i].dev);

	module_put(sc->alg->module);
	flush_work(&sc->trigger_event);
	kfree(sc);
}

// Maps sector from insane device to backend disk. 
//
// Sector on insane device is translated to block, then to stripe and then to
// sector on specific backend device.
//
// Input params:
// * sc - context.
// * sector - incoming sector.
//
// Output params:
// * block - block number in backend device.
// * lane_off - lane offset, i.e. block number in lane. Indicates device index.
// * result - mapped sector on specific backend disk.
static void insane_map_sector(struct insane_c *sc, sector_t sector, u64 *block, uint32_t *lane_off, sector_t *result)
{
	sector_t chunk = dm_target_offset(sc->ti, sector);
	sector_t chunk_offset;
	dm_debug("insane_map_sector: sector %lld, chung %lld\n", (u64)sector, (u64)chunk);
	
	// Get chunk number and chunk offset
	chunk_offset = chunk & (sc->chunk_size - 1);
	chunk >>= sc->chunk_size_shift;
	dm_debug("Chunk size shift >=0 (%d), chunk_offset = %lld, chunk = %lld\n", 
						 sc->chunk_size_shift, (u64)chunk_offset, (u64)chunk);
	
	// Get stripe number and stripe offset.
	if (sc->ndev_shift < 0) 
	{
		*lane_off = sector_div(chunk, sc->ndev);
		dm_debug("Stripes shift < 0 (%d), chunk = %lld, found lane_off = %u\n", 
						 sc->ndev_shift, (u64)chunk, *lane_off);
	}
	else 
	{
		*lane_off = chunk & (sc->ndev - 1);
		chunk >>= sc->ndev_shift;
		dm_debug("Stripes shift >=0 (%d), chunk = %lld, found lane_off = %u\n",
						 sc->ndev_shift, (u64)chunk, *lane_off);
	}
	
	*block = chunk;

	// Get sector on mapped disk.
	chunk <<= sc->chunk_size_shift;
	
	*result = chunk + chunk_offset;
	dm_debug("Final: Chunk = %lld, result = %lld\n", (u64)chunk, (u64)*result);
}

// insane_map_sector wrapper to map ranges of sectors.
static void insane_map_range_sector(struct insane_c *sc, sector_t sector, uint32_t target_dev, sector_t *result)
{
	uint32_t dev_index;
	u64 block;

	insane_map_sector(sc, sector, &block, &dev_index, result);
	if (dev_index == target_dev)
		return;

	/* round down */
	sector = *result;
	*result = sector & ~(sector_t)(sc->chunk_size - 1);

	if (target_dev < dev_index)
		*result += sc->chunk_size;	  // next chunk
}

// Maps range of sectors on REQ_DISCARD and REQ_WRITE_SAME.
static int insane_map_range(struct insane_c *sc, struct bio *bio, uint32_t target_dev)
{
	sector_t begin, end, from, to;

	from = bio->bi_sector;
	to	 = bio->bi_sector + bio_sectors(bio);

	insane_map_range_sector(sc, from , target_dev, &begin);
	insane_map_range_sector(sc, to	 , target_dev, &end);
	if (begin < end) {
		bio->bi_bdev = sc->devs[target_dev].dev->bdev;
		bio->bi_sector = begin;
		bio->bi_size = to_bytes(end - begin);
		return DM_MAPIO_REMAPPED;
	} else {
		/* The range doesn't map to the target device */
		bio_endio(bio, 0);
		return DM_MAPIO_SUBMITTED;
	}
}

static int insane_map_special(struct insane_c *sc, struct bio *bio
#if LINUX_VERSION_CODE <= KERNEL_VERSION( 3, 7, 0 )
				  , union map_info *map_context
#endif
	)
{
	unsigned target_request_nr;

	if (bio->bi_rw & REQ_FLUSH) {
#if LINUX_VERSION_CODE == KERNEL_VERSION( 3, 8, 0 )
		target_request_nr = dm_bio_get_target_request_nr(bio);
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION( 3, 8, 0 )
		target_request_nr = dm_bio_get_target_bio_nr(bio);
		//target_request_nr = dm_bio_get_target_request_nr(bio);
#else // <= 3.7
		target_request_nr = map_context->target_request_nr;
#endif
#endif
		BUG_ON(target_request_nr >= sc->ndev);
		bio->bi_bdev = sc->devs[target_request_nr].dev->bdev;
		dm_log("! REQ_FLUSH on bio->bi_sector = %lld\n", (u64)bio->bi_sector);
		return DM_MAPIO_REMAPPED;
	}

	if (unlikely(bio->bi_rw & REQ_DISCARD)
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 3, 7, 0 )
		 || unlikely(bio->bi_rw & REQ_WRITE_SAME)
#endif
		 ) 
	{
#if LINUX_VERSION_CODE == KERNEL_VERSION( 3, 8, 0 )
		target_request_nr = dm_bio_get_target_request_nr(bio);
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION( 3, 8, 0 )
		target_request_nr = dm_bio_get_target_bio_nr(bio);
		//target_request_nr = dm_bio_get_target_request_nr(bio);
#else // <= 3.7
		target_request_nr = map_context->target_request_nr;
#endif
#endif
		BUG_ON(target_request_nr >= sc->ndev);
		dm_log("! REQ_DISCARD on bio->bi_sector = %lld\n", (u64)bio->bi_sector);
		return insane_map_range(sc, bio, target_request_nr);
	}

	return DM_MAPIO_REMAPPED;
}

static void insane_bi_end_io( struct bio *bio, int err )
{
	int i;
	for( i = 0; i < bio->bi_vcnt; i++ )
	{
		__free_page(bio->bi_io_vec[i].bv_page);
	}

	bio_put(bio);
}

static void do_bio( sector_t sector, struct block_device *bdev, int bi_size, int bi_vcnt, int rw )
{
	struct bio *bio;
	struct page *parity_page;

	int page_counter;
	//int cur_len;
	//int bio_added;

	//bool remaining=true;

	
	bio = bio_alloc(GFP_NOIO, bi_vcnt);
	bio->bi_bdev = bdev;
	bio->bi_sector = sector;
	bio->bi_vcnt = bi_vcnt;
	bio->bi_size = bi_size;
	bio->bi_end_io = insane_bi_end_io;
	bio->bi_idx = 0;

	for (page_counter = 0; page_counter < bi_vcnt; page_counter++) 
	{
		parity_page = alloc_page(GFP_KERNEL);
		bio->bi_io_vec[page_counter].bv_len = PAGE_SIZE;
		bio->bi_io_vec[page_counter].bv_page = parity_page;
		bio->bi_io_vec[page_counter].bv_offset = 0;
	}
	/*
	while (remaining) {
		bio = bio_alloc(GFP_NOIO, bi_vcnt);
		bio->bi_bdev = bdev;
		bio->bi_sector = sector;
		bio->bi_end_io = insane_bi_end_io;
		for (page_counter = 0; bi_size > 0 && page_counter < bi_vcnt; page_counter++)
		{
			parity_page = alloc_page(GFP_KERNEL);
			cur_len = bi_size > PAGE_SIZE ? PAGE_SIZE : bi_size;
			bio_added = bio_add_page(bio, parity_page, bi_size, 0);
			if (bio_added < bi_size) {
				__free_page(parity_page);
				//dm_log("Error! bi_size: %d; bio_added: %d, bi_vcnt: %d, page_counter: %d, bio->bi_size: %u\n", bi_size, bio_added, bi_vcnt, page_counter, bio->bi_size);
				break;
			}
			bi_size -= cur_len;
			if ((bi_size <= 0) || (page_counter >= bi_vcnt)) {
				remaining = false;
				break;
			}
		}
		bio->bi_vcnt = page_counter;
		submit_bio(rw,bio);
	}
	
//			dm_log("All right! bi_size: %d; bio_added: %d, bi_vcnt: %d, page_counter: %d, bio->bi_size: %u\n", bi_size, bio_added, bi_vcnt, page_counter, bio->bi_size);
	*/
	submit_bio(rw, bio);
}

// Trace current LBA and submit syndrom update on stripe change.
// Used on sequential write to prevent performance degrade.
static void insane_seq_syndromes (struct bio *bio, struct parity_places *syndromes, struct insane_c *sc, int dev_index)
{
	sector_t sector_number, bi_size;
	sector_t current_block, next_block;
	int device_number, p_blocks, bi_vcnt;

	int parity_counter;

	current_block = bio->bi_sector;
	sector_div(current_block, sc->chunk_size);

	next_block = bio->bi_sector + bio->bi_size;
	sector_div(next_block, sc->chunk_size);

	if (current_block != next_block) {

		p_blocks = sc->alg->p_blocks;	
		bi_vcnt = sc->chunk_size_pages;
		bi_size = sc->chunk_size_bytes;

		for (parity_counter = 0; parity_counter < p_blocks; parity_counter++)
		{
			device_number = syndromes->device_number[parity_counter];
			sector_number = syndromes->sector_number[parity_counter];
			do_bio( sector_number, sc->devs[device_number].dev->bdev, bi_size, bi_vcnt, WRITE );
		}
	}

/*
	static sector_t stripe_num = 0;
	static sector_t stripe_sector = 0;
	sector_t prev_stripe;
	sector_t d_sectors, bio_size;

	int p_blocks, bi_vcnt, bi_size;
	int device_number;
	sector_t sector_number;
	int parity_counter;

	p_blocks = sc->alg->p_blocks;
	bi_vcnt  = sc->chunk_size_pages;
	bi_size  = sc->chunk_size_bytes;

	// Calculate stripe number
	prev_stripe = stripe_num;
	stripe_num = bio->bi_sector;
	sector_div(stripe_num, sc->chunk_size);
	stripe_num = (stripe_num * sc->ndev + dev_index);
	sector_div(stripe_num, sc->alg->stripe_blocks);

	// Update stripe sector
	bio_size = bio->bi_size;
	bio_size >>= SECTOR_SHIFT;
	stripe_sector = stripe_sector + bio_size;

	d_sectors = sc->chunk_size;
	d_sectors = d_sectors * (sc->alg->stripe_blocks - sc->alg->p_blocks - sc->alg->e_blocks);
	
	// Write when stripe ends
	if ((stripe_sector >= d_sectors) || (prev_stripe != stripe_num))
	{
		for (parity_counter = 0; parity_counter < p_blocks; parity_counter++)
		{
			device_number = syndromes->device_number[parity_counter];
			sector_number = syndromes->sector_number[parity_counter];
			do_bio( sector_number, sc->devs[device_number].dev->bdev, bi_size, bi_vcnt, WRITE );
		}
		stripe_sector = stripe_sector % d_sectors;
	}

	if (prev_stripe != stripe_num) 
	{
		stripe_sector = 0;
	}
*/
}

// Syndrom updating on random write
static void insane_finish_syndromes (struct bio *bio, struct parity_places *syndromes, struct insane_c *sc)
{
	sector_t sector;
	int device_number;
	struct block_device *bi_bdev;

	int p_blocks;
	int bi_vcnt;
	int bi_size;

	int parity_counter;

	bi_vcnt = sc->chunk_size_pages;
	bi_size = sc->chunk_size_bytes;
	p_blocks = sc->alg->p_blocks;

	// To update syndrome we need:
	// 1. New data (already have in bio)
	// 2. Read old data
	// 3. Read syndrome
	// 4. Write syndrome
	// 5. ??????
	// 6. PROFIT!
	//
	// We are emulating so we don't calculate anything and write garbage.

	// Align sector to chunk size and read old data
	sector = bio->bi_sector;
	sector_div(sector, sc->chunk_size);
	sector = sector << sc->chunk_size_shift;
	bi_bdev = bio->bi_bdev;
	do_bio(sector, bi_bdev, bi_size, bi_vcnt, READ);
	
	// Read and write each syndrome
	for (parity_counter = 0; parity_counter < p_blocks; parity_counter++)
	{
		sector = syndromes->sector_number[parity_counter];
		device_number = syndromes->device_number[parity_counter];

		if (device_number > -1) 
		{
			bi_bdev = sc->devs[device_number].dev->bdev;
			do_bio(sector, bi_bdev, bi_size, bi_vcnt, READ);
			do_bio(sector, bi_bdev, bi_size, bi_vcnt, WRITE);
		} 
		else 
		{ 
			break;
		}
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION( 3, 8, 0 )
static int insane_map(struct dm_target *ti, struct bio *bio, union map_info *map_context)
#else
static int insane_map(struct dm_target *ti, struct bio *bio)
#endif
{
	struct insane_c *sc = ti->private;
	struct parity_places syndromes;
	int dev_index;
	u64 block;

	if (   unlikely(bio->bi_rw & REQ_FLUSH) 
		|| unlikely(bio->bi_rw & REQ_DISCARD)
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 3, 7, 0)
		|| unlikely(bio->bi_rw & REQ_WRITE_SAME)
#endif
		)
	{
		return insane_map_special(sc, bio
#if LINUX_VERSION_CODE < KERNEL_VERSION( 3, 8, 0)
			, map_context
#endif
			);
	}

	// First, map sector to specific disk and calculate block and lane offset.
	insane_map_sector(sc, bio->bi_sector, &block, &dev_index, &bio->bi_sector);

	// Second, remap sector again according to algorithm data placement scheme.
	syndromes = sc->alg->map(sc, block, &bio->bi_sector, &dev_index);

	// Don't forget to change device.
	bio->bi_bdev = sc->devs[dev_index].dev->bdev;
        
	if( bio->bi_rw & WRITE )
	{
		if( sc->io_pattern == SEQUENTIAL ) {
		if (syndromes.last_block == true)
				insane_seq_syndromes(bio, &syndromes, sc, dev_index);
	}
		else
			insane_finish_syndromes(bio, &syndromes, sc);
	}
        
	dm_debug("bi_sector: %lld\n", (u64)bio->bi_sector);
	return DM_MAPIO_REMAPPED;
}

/*
 * Stripe status:
 *
 * INFO
 * #devices [device_name <device_name>] [group word count]
 * [error count 'A|D' <error count 'A|D'>]
 *
 * TABLE
 * #devices [device chunk size]
 * [device_name <device_name>]
 *
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0) // 2.6.32 - 3.6
static int insane_status(struct dm_target *ti, status_type_t type, char *result, unsigned int maxlen)
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0) // 3.6 - 3.7
static void insane_status(struct dm_target *ti, status_type_t type, unsigned status_flags, char *result, unsigned maxlen)
#else // 3.8 - last
static int insane_status(struct dm_target *ti, status_type_t type, unsigned status_flags, char *result, unsigned maxlen)
#endif
#endif
{
	struct insane_c *sc = (struct insane_c *) ti->private;
	char buffer[sc->ndev + 1];
	unsigned int sz = 0;
	unsigned int i;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%d ", sc->ndev);
		for (i = 0; i < sc->ndev; i++)	
		{
			DMEMIT("%s ", sc->devs[i].dev->name);
			buffer[i] = atomic_read(&(sc->devs[i].error_count)) ?  'D' : 'A';
		}
		buffer[i] = '\0';
		DMEMIT("1 %s", buffer);
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%d %llu", sc->ndev, (u64)sc->chunk_size);
		for (i = 0; i < sc->ndev; i++)
		{
			DMEMIT(" %s", sc->devs[i].dev->name);
		}
		break;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
	return 0;
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
static int insane_end_io(struct dm_target *ti, struct bio *bio, int error, union map_info *map_context)
#else
static int insane_end_io(struct dm_target *ti, struct bio *bio, int error)
#endif
{
	unsigned i;
	char major_minor[16];
	struct insane_c *sc = ti->private;

	
	// ------------------------
	// No errors - complete I/O
	// ------------------------
	if (!error)
		return 0;

	// --------------------------------------
	// Here and after - handle various errors
	// --------------------------------------

	if ((error == -EWOULDBLOCK) && 
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 2, 6, 36 )
		(bio->bi_rw & REQ_RAHEAD))
#else
		bio_rw_flagged(bio, BIO_RW_AHEAD))
#endif
	{
		return error;
	}

	if (error == -EOPNOTSUPP)
		return error;

	memset(major_minor, 0, sizeof(major_minor));
	sprintf(major_minor, "%d:%d",
		MAJOR(disk_devt(bio->bi_bdev->bd_disk)),
		MINOR(disk_devt(bio->bi_bdev->bd_disk)));

	// Test to see which drive triggered the event
	// and increment error count for all blocks on that device.
	// If the error count for a given device exceeds the threshold
	// value we will no longer trigger any further events.
	for (i = 0; i < sc->ndev; i++)
	{
		if (!strcmp(sc->devs[i].dev->name, major_minor)) 
		{
			atomic_inc(&(sc->devs[i].error_count));
			if ( atomic_read( &(sc->devs[i].error_count) ) <
					DM_IO_ERROR_THRESHOLD )
			{
				schedule_work(&sc->trigger_event);
			}
		}
	}

	return error;
}

// Device mapper callback needed in some special merge cases.
static int insane_iterate_devices(struct dm_target *ti, iterate_devices_callout_fn fn, void *data)
{
	struct insane_c *sc;
	int ret = 0;
	unsigned i = 0;

	if(!ti)
		return -EINVAL;

	sc = ti->private;
	if(!sc)
		return -EINVAL;

	do {
		// The callout function is called once for each 
		// _contiguous_ section of an underlying device.
		// That's why we call with dev_width.
		ret = fn( ti, sc->devs[i].dev, 0, sc->dev_width, data );
	} while (!ret && ++i < sc->ndev);

	return ret;
}

// I/O hints for upper layer.
static void insane_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct insane_c *sc = ti->private;
	unsigned chunk_size = sc->chunk_size << SECTOR_SHIFT;

	// Set minimal and optimal request sizes
	blk_limits_io_min(limits, chunk_size);
	blk_limits_io_opt(limits, chunk_size * sc->ndev);
}

static int insane_merge(struct dm_target *ti, struct bvec_merge_data *bvm, struct bio_vec *biovec, int max_size)
{
	struct insane_c *sc = ti->private;
	sector_t bvm_sector = bvm->bi_sector;
	uint32_t dev_index;
	struct request_queue *q;
	u64 block;

	insane_map_sector(sc, bvm_sector, &block, &dev_index, &bvm_sector);

	q = bdev_get_queue(sc->devs[dev_index].dev->bdev);
	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = sc->devs[dev_index].dev->bdev;
	bvm->bi_sector = bvm_sector;

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static struct target_type insane_target = {
	.name	= "insane",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr	= insane_ctr,
	.dtr	= insane_dtr,
	.map	= insane_map,
	.end_io = insane_end_io,
	.status = insane_status,
	.iterate_devices = insane_iterate_devices,
	.io_hints = insane_io_hints,
	.merge	= insane_merge,
};

int insane_register(struct insane_algorithm *alg)
{
	struct insane_algorithm *cur;

	if (!alg || !alg->map) {
		dm_log("Invalid algorithm to register\n");
		return -EINVAL;
	}

	spin_lock( &alg_list_lock );
	list_for_each_entry(cur, &alg_list, list)
	{
		if (!cur) 
		{
			dm_log("Invalid algorithm entry\n");
			continue;
		}

		if (!strncmp(cur->name, alg->name, ALG_NAME_LEN))
		{
			dm_log("Algorithm %s is already registered\n", cur->name);
			return -EEXIST;
		}
	}
	list_add( &alg->list, &alg_list );
	spin_unlock( &alg_list_lock );

	dm_log("Algorithm %s successfully registered\n", alg->name);
	return 0;
}
EXPORT_SYMBOL(insane_register);

int insane_unregister(struct insane_algorithm *alg)
{
	struct insane_algorithm *cur;
	int unregistered = 0;

	if (!alg ) {
		dm_log("Invalid algorithm to unregister\n");
		return -EINVAL;
	}

	spin_lock( &alg_list_lock );
	list_for_each_entry(cur, &alg_list, list)
	{
		if( !cur ) {
			dm_log("Failed to unregister algorithm %s: invalid entry\n", alg->name);
			continue;
		}

		if(!strncmp(cur->name, alg->name, ALG_NAME_LEN))
		{
			list_del(&alg->list);
			unregistered = 1;
			break;
		}
	}
	spin_unlock( &alg_list_lock );

	if (!unregistered) {
		dm_log("Failed to unregister algorithm %s: not found\n", alg->name);
		return -ESRCH;
	}

	dm_log("Algorithm %s successfully unregistered\n", alg->name);
	return 0;
}
EXPORT_SYMBOL(insane_unregister);

int __init insane_init(void)
{
	int r;

	r = dm_register_target( &insane_target );
	if (r < 0) {
		dm_log("target registration failed");
		return r;
	}

	dm_log("Insane striping successfully loaded\n");
	return r;
}

void insane_exit(void)
{
	dm_log("Exiting insane striping\n");
	dm_unregister_target(&insane_target);
}

module_init(insane_init);
module_exit(insane_exit);

module_param( debug, int, S_IRUGO | S_IWUSR );

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Anastasiev");
MODULE_AUTHOR("Alex Dzyoba");
