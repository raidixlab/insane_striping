#define SUBSTRIPES 3
#define SUBSTRIPE_DATA 3
#define E_BLOCKS 1
#define GLOBAL_S 1

const unsigned char lrc_scheme[(SUBSTRIPE_DATA + 1) * SUBSTRIPES + E_BLOCKS + GLOBAL_S] =
{0x0, 0x0, 0x0, 0xc0, 0x1, 0x1, 0x1, 0xc1, 0x2, 0x2, 0x2, 0xc2, 0xee, 0xff};

// it is just lrc_scheme without 0xee, 0xff and 0xcN
const unsigned char lrc_data[SUBSTRIPE_DATA * SUBSTRIPES] =
{0x0, 0x0, 0x0, 0x1, 0x1, 0x1, 0x2, 0x2, 0x2};

// it is place of global syndrome
const int lrc_gs[GLOBAL_S] = {13};

// places of all local syndromes
const int lrc_ls[SUBSTRIPES] = {3, 7, 11};
// empty place
const int lrc_eb = 12;
// not-data blocks, ordered by increasing
const int lrc_offset[SUBSTRIPES + E_BLOCKS + GLOBAL_S] = {3, 7, 11, 12, 13};
// number of the last data block
const int lrc_ldb = 10;

