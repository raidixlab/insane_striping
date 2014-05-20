About
-----

**instane_striping** - framework to build RAID devices with arbitrary data placement
scheme. It allows you to create classic RAIDs with asymmetric data placement as
well as exotic one like pyramid RAID.

Writing new algorithms
----------------------

Algorithms are described in `insane_algorithm` struct. 
Each algorithm descriptor MUST have defined:

 * `name`
 * `p_blocks`
 * `e_blocks`
 * `map` function
 * `.module = THIS_MODULE` to prevent algorithm unloading.

If algorithm needs to configure any parameters at runtime - it must supply
`configure` callback which will be invoked during device creation. `configure`
callback gets initialized context and therefore can get device size, chunk
size, etc. This is needed for example to determine stripe size - it depends
on devices count.

Algorithm registration
----------------------

_insane\_striping_ module has `alg_list`. That list holds algorithms registered in
_insane\_striping_. When _insane\_striping_ is loaded it has empty
`alg_list`. Algorithms are registered on load of it's kernel module.

Algorithm registration:

1. Algorithm kernel module is loaded.
1. Linux kernel calls module's `init` function
1. `init` function calls `insane_register` and supply algorithm descriptor.
1. `insane_register` checks descriptor and adds algorithm to internal `alg_list`.

Device creation
---------------

1. Parse arguments - `ndev`, `chunk_size`, devices and new argument algorithm
   name, that must be supplied in `dmsetup` table.
2. Find algorithm by it's name in `alg_list`.
3. Create insane context and save algorithm descriptor in context.

bio mapping
-----------

1. `insane_map` is invoked by device mapper.
2. `insane_map` calls insane_map_sector to map sector to backend disk and
   determine block (chunk) and stripe.
3. `insane_map` calls 

       sc->alg->map(ctx, block, &sector, &dev_index);

   `map` callback will calculate final sector and device index by given block
   and block size (from insane context `ctx`)

4. `insane_map` will replace original bio sector and device and give it back to
   device mapper with `DM_MAPIO_REMAPPED`.

